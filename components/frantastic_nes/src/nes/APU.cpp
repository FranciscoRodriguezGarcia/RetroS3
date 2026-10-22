#include "frantastic/nes/APU.hpp"
#include <algorithm>

namespace frantastic::nes {

namespace {
constexpr std::uint8_t kLengthTable[32] = {10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
                                           12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
constexpr std::uint8_t kDuty[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0}, {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};
constexpr std::uint8_t kTriangle[32] = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                                        0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
// NTSC periods in CPU cycles.
constexpr std::uint16_t kNoisePeriod[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
constexpr std::uint16_t kDmcRate[16] = {428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54};

// NTSC frame sequencer step positions in CPU cycles after a reset.
constexpr std::uint32_t kStep1 = 7457, kStep2 = 14913, kStep3 = 22371, kStep4 = 29829, kPeriod4 = 29830;
constexpr std::uint32_t kStep5 = 37281, kPeriod5 = 37282;
} // namespace

// ---- Channel units ----------------------------------------------------------
void APU::Envelope::clock() {
    if (start) {
        start = false;
        decay = 15;
        divider = period;
    } else if (divider == 0) {
        divider = period;
        if (decay) --decay;
        else if (loop) decay = 15;
    } else {
        --divider;
    }
}

std::uint16_t APU::Pulse::targetPeriod() const {
    const std::uint16_t change = static_cast<std::uint16_t>(period >> sweepShift);
    if (!sweepNegate) return static_cast<std::uint16_t>(period + change);
    // Pulse 1 subtracts in ones' complement (one extra), pulse 2 in two's.
    const int target = static_cast<int>(period) - change - (ones ? 1 : 0);
    return static_cast<std::uint16_t>(std::max(target, 0));
}

void APU::Pulse::clockTimer() {
    if (timer == 0) {
        timer = period;
        step = static_cast<std::uint8_t>((step + 7) & 7);  // The sequencer counts down.
    } else {
        --timer;
    }
}

void APU::Pulse::clockSweep() {
    if (sweepDivider == 0 && sweepEnabled && sweepShift && !muted()) period = targetPeriod();
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        --sweepDivider;
    }
}

std::uint8_t APU::Pulse::output() const {
    if (!length || muted() || !kDuty[duty][step]) return 0;
    return env.volume();
}

void APU::Triangle::clockTimer() {
    if (timer == 0) {
        timer = period;
        if (length && linear) step = static_cast<std::uint8_t>((step + 1) & 31);
    } else {
        --timer;
    }
}

void APU::Triangle::clockLinear() {
    if (reloadFlag) linear = linearReload;
    else if (linear) --linear;
    if (!control) reloadFlag = false;
}

std::uint8_t APU::Triangle::output() const { return kTriangle[step]; }

void APU::Noise::clockTimer() {
    if (timer == 0) {
        timer = static_cast<std::uint16_t>(period - 1);
        const unsigned tap = mode ? 6u : 1u;
        const std::uint16_t feedback = static_cast<std::uint16_t>((shift ^ (shift >> tap)) & 1u);
        shift = static_cast<std::uint16_t>((shift >> 1) | (feedback << 14));
    } else {
        --timer;
    }
}

std::uint8_t APU::Noise::output() const { return (!length || (shift & 1u)) ? 0 : env.volume(); }

void APU::Dmc::clockTimer() {
    if (timer) {
        --timer;
        return;
    }
    timer = static_cast<std::uint16_t>(rate - 1);
    if (!silence) {
        if (shift & 1u) {
            if (level <= 125) level = static_cast<std::uint8_t>(level + 2);
        } else if (level >= 2) {
            level = static_cast<std::uint8_t>(level - 2);
        }
    }
    shift >>= 1;
    if (--bitsLeft == 0) {
        bitsLeft = 8;
        if (bufferFull) {
            silence = false;
            shift = buffer;
            bufferFull = false;
        } else {
            silence = true;
        }
    }
}

// ---- APU ----------------------------------------------------------------------
APU::APU(double sampleRate) {
    setSampleRate(sampleRate);
    powerOn();
}

void APU::setSampleRate(double rate) {
    sampleRate_ = rate > 0 ? rate : 48000.0;
    cyclesPerSample_ = CpuClockHz / sampleRate_;
}

void APU::powerOn() {
    pulse1_ = Pulse{};
    pulse1_.ones = true;
    pulse2_ = Pulse{};
    triangle_ = Triangle{};
    noise_ = Noise{};
    dmc_ = Dmc{};
    fiveStep_ = irqInhibit_ = frameIrq_ = false;
    frameCycle_ = 0;
    frameResetDelay_ = -1;
    apuCycle_ = false;
    cpuCycles_ = 0;
    samples_.clear();
    sampleClock_ = accumulator_ = 0;
    accumulated_ = 0;
    hp90Prev_ = hp90Out_ = hp440Prev_ = hp440Out_ = lpOut_ = 0;
}

void APU::reset() {
    // Reset silences all channels ($4015 = 0) and restarts the frame counter
    // with the last mode written to $4017.
    writeRegister(0x4015, 0);
    frameIrq_ = false;
    dmc_.irq = false;
    triangle_.step = 0;
    dmc_.level &= 1u;
    frameCycle_ = 0;
    frameResetDelay_ = -1;
}

void APU::writeRegister(std::uint16_t address, std::uint8_t v) {
    switch (address) {
    case 0x4000: case 0x4004: {
        Pulse& p = address == 0x4000 ? pulse1_ : pulse2_;
        p.duty = v >> 6;
        p.env.loop = (v & 0x20u) != 0;
        p.env.constant = (v & 0x10u) != 0;
        p.env.period = v & 0x0Fu;
        break;
    }
    case 0x4001: case 0x4005: {
        Pulse& p = address == 0x4001 ? pulse1_ : pulse2_;
        p.sweepEnabled = (v & 0x80u) != 0;
        p.sweepPeriod = (v >> 4) & 7u;
        p.sweepNegate = (v & 0x08u) != 0;
        p.sweepShift = v & 7u;
        p.sweepReload = true;
        break;
    }
    case 0x4002: case 0x4006: {
        Pulse& p = address == 0x4002 ? pulse1_ : pulse2_;
        p.period = static_cast<std::uint16_t>((p.period & 0x700u) | v);
        break;
    }
    case 0x4003: case 0x4007: {
        Pulse& p = address == 0x4003 ? pulse1_ : pulse2_;
        p.period = static_cast<std::uint16_t>((p.period & 0xFFu) | ((v & 7u) << 8));
        if (p.enabled) p.length = kLengthTable[v >> 3];
        p.step = 0;
        p.env.start = true;
        break;
    }
    case 0x4008:
        triangle_.control = (v & 0x80u) != 0;
        triangle_.linearReload = v & 0x7Fu;
        break;
    case 0x400A: triangle_.period = static_cast<std::uint16_t>((triangle_.period & 0x700u) | v); break;
    case 0x400B:
        triangle_.period = static_cast<std::uint16_t>((triangle_.period & 0xFFu) | ((v & 7u) << 8));
        if (triangle_.enabled) triangle_.length = kLengthTable[v >> 3];
        triangle_.reloadFlag = true;
        break;
    case 0x400C:
        noise_.env.loop = (v & 0x20u) != 0;
        noise_.env.constant = (v & 0x10u) != 0;
        noise_.env.period = v & 0x0Fu;
        break;
    case 0x400E:
        noise_.mode = (v & 0x80u) != 0;
        noise_.period = kNoisePeriod[v & 0x0Fu];
        break;
    case 0x400F:
        if (noise_.enabled) noise_.length = kLengthTable[v >> 3];
        noise_.env.start = true;
        break;
    case 0x4010:
        dmc_.irqEnable = (v & 0x80u) != 0;
        if (!dmc_.irqEnable) dmc_.irq = false;
        dmc_.loop = (v & 0x40u) != 0;
        dmc_.rate = kDmcRate[v & 0x0Fu];
        break;
    case 0x4011: dmc_.level = v & 0x7Fu; break;
    case 0x4012: dmc_.sampleAddress = static_cast<std::uint16_t>(0xC000u + v * 64u); break;
    case 0x4013: dmc_.sampleLength = static_cast<std::uint16_t>(v * 16u + 1u); break;
    case 0x4015:
        pulse1_.enabled = (v & 0x01u) != 0;
        pulse2_.enabled = (v & 0x02u) != 0;
        triangle_.enabled = (v & 0x04u) != 0;
        noise_.enabled = (v & 0x08u) != 0;
        dmc_.enabled = (v & 0x10u) != 0;
        if (!pulse1_.enabled) pulse1_.length = 0;
        if (!pulse2_.enabled) pulse2_.length = 0;
        if (!triangle_.enabled) triangle_.length = 0;
        if (!noise_.enabled) noise_.length = 0;
        if (!dmc_.enabled) dmc_.remaining = 0;
        else if (dmc_.remaining == 0) dmc_.restart();
        dmc_.irq = false;
        updateDmcRequest();
        break;
    case 0x4017: writeFrameCounter(v); break;
    default: break;
    }
}

void APU::writeFrameCounter(std::uint8_t value) {
    irqInhibit_ = (value & 0x40u) != 0;
    if (irqInhibit_) frameIrq_ = false;
    pendingFrameValue_ = value;
    // The sequencer restarts 3 CPU cycles after a write on an APU cycle, 4
    // when written between APU cycles.
    frameResetDelay_ = apuCycle_ ? 3 : 4;
}

std::uint8_t APU::peekStatus() const {
    std::uint8_t s = 0;
    if (pulse1_.length) s |= 0x01u;
    if (pulse2_.length) s |= 0x02u;
    if (triangle_.length) s |= 0x04u;
    if (noise_.length) s |= 0x08u;
    if (dmc_.remaining) s |= 0x10u;
    if (frameIrq_) s |= 0x40u;
    if (dmc_.irq) s |= 0x80u;
    return s;
}

std::uint8_t APU::readStatus() {
    const std::uint8_t s = peekStatus();
    frameIrq_ = false;
    return s;
}

void APU::clockQuarterFrame() {
    pulse1_.env.clock();
    pulse2_.env.clock();
    noise_.env.clock();
    triangle_.clockLinear();
}

void APU::clockHalfFrame() {
    if (pulse1_.length && !pulse1_.env.loop) --pulse1_.length;
    if (pulse2_.length && !pulse2_.env.loop) --pulse2_.length;
    if (triangle_.length && !triangle_.control) --triangle_.length;
    if (noise_.length && !noise_.env.loop) --noise_.length;
    pulse1_.clockSweep();
    pulse2_.clockSweep();
}

void APU::updateDmcRequest() { dmc_.dmaRequest = !dmc_.bufferFull && dmc_.remaining > 0; }

void APU::dmcDmaComplete(std::uint8_t value) {
    dmc_.buffer = value;
    dmc_.bufferFull = true;
    dmc_.address = dmc_.address == 0xFFFFu ? 0x8000u : static_cast<std::uint16_t>(dmc_.address + 1u);
    if (dmc_.remaining && --dmc_.remaining == 0) {
        if (dmc_.loop) dmc_.restart();
        else if (dmc_.irqEnable) dmc_.irq = true;
    }
    updateDmcRequest();
}

void APU::tick() {
    ++cpuCycles_;
    apuCycle_ = !apuCycle_;

    if (frameResetDelay_ >= 0 && frameResetDelay_-- == 0) {
        fiveStep_ = (pendingFrameValue_ & 0x80u) != 0;
        frameCycle_ = 0;
        if (fiveStep_) {
            clockQuarterFrame();
            clockHalfFrame();
        }
    }

    ++frameCycle_;
    if (!fiveStep_) {
        if (frameCycle_ == kStep1 || frameCycle_ == kStep3) clockQuarterFrame();
        if (frameCycle_ == kStep2 || frameCycle_ == kStep4) { clockQuarterFrame(); clockHalfFrame(); }
        if (frameCycle_ >= kStep4 - 1 && frameCycle_ <= kPeriod4 && !irqInhibit_) frameIrq_ = true;
        if (frameCycle_ >= kPeriod4) frameCycle_ = 0;
    } else {
        if (frameCycle_ == kStep1 || frameCycle_ == kStep3) clockQuarterFrame();
        if (frameCycle_ == kStep2 || frameCycle_ == kStep5) { clockQuarterFrame(); clockHalfFrame(); }
        if (frameCycle_ >= kPeriod5) frameCycle_ = 0;
    }

    triangle_.clockTimer();
    noise_.clockTimer();
    dmc_.clockTimer();
    if (apuCycle_) {
        pulse1_.clockTimer();
        pulse2_.clockTimer();
    }
    updateDmcRequest();

    accumulator_ += mixerOutput() + (expansionAudio_ ? expansionAudio_() : 0.0f);
    ++accumulated_;
    sampleClock_ += 1.0;
    if (sampleClock_ >= cyclesPerSample_) {
        sampleClock_ -= cyclesPerSample_;
        emitSample();
    }
}

float APU::mixerOutput() const {
    const double p = pulse1_.output() + pulse2_.output();
    const double pulse = p ? 95.88 / (8128.0 / p + 100.0) : 0.0;
    const double t = triangle_.output() / 8227.0 + noise_.output() / 12241.0 + dmc_.level / 22638.0;
    const double tnd = t > 0 ? 159.79 / (1.0 / t + 100.0) : 0.0;
    return static_cast<float>(pulse + tnd);
}

void APU::emitSample() {
    const double in = accumulated_ ? accumulator_ / accumulated_ : 0.0;
    accumulator_ = 0;
    accumulated_ = 0;
    // First-order high-pass at ~90 Hz and ~440 Hz and low-pass at ~14 kHz,
    // approximating the console's analogue output stage.
    const double dt = 1.0 / sampleRate_;
    const double pi2 = 6.283185307179586;
    const double a90 = 1.0 / (1.0 + pi2 * 90.0 * dt);
    const double a440 = 1.0 / (1.0 + pi2 * 440.0 * dt);
    const double rc14 = 1.0 / (pi2 * 14000.0);
    const double b14 = dt / (rc14 + dt);
    hp90Out_ = a90 * (hp90Out_ + in - hp90Prev_);
    hp90Prev_ = in;
    hp440Out_ = a440 * (hp440Out_ + hp90Out_ - hp440Prev_);
    hp440Prev_ = hp90Out_;
    lpOut_ += b14 * (hp440Out_ - lpOut_);
    if (samples_.size() < 1u << 16) samples_.push_back(static_cast<float>(std::clamp(lpOut_ * 1.5, -1.0, 1.0)));
}

std::size_t APU::readSamples(float* out, std::size_t max) {
    const std::size_t n = std::min(max, samples_.size());
    std::copy(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(n), out);
    samples_.erase(samples_.begin(), samples_.begin() + static_cast<std::ptrdiff_t>(n));
    return n;
}

std::uint8_t APU::lengthCounter(unsigned channel) const {
    switch (channel) {
    case 0: return pulse1_.length;
    case 1: return pulse2_.length;
    case 2: return triangle_.length;
    case 3: return noise_.length;
    default: return 0;
    }
}

std::uint8_t APU::channelOutput(unsigned channel) const {
    switch (channel) {
    case 0: return pulse1_.output();
    case 1: return pulse2_.output();
    case 2: return triangle_.output();
    case 3: return noise_.output();
    case 4: return dmc_.level;
    default: return 0;
    }
}

} // namespace frantastic::nes

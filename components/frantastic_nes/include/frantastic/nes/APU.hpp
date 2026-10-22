#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace frantastic::nes {

// 2A03 audio processing unit (NTSC). Clocked once per CPU cycle; pulse and
// noise timers run on APU cycles (every other CPU cycle), the triangle timer
// on every CPU cycle. Output is mixed with the documented non-linear DAC
// approximation, filtered like the console's output stage, and resampled to
// the host rate by box-filter averaging.
class APU {
    friend class NesStateCodec;
public:
    static constexpr double CpuClockHz = 1789773.0;

    explicit APU(double sampleRate = 48000.0);
    void powerOn();
    void reset();
    void tick();  // One CPU cycle.
    void setExpansionAudio(std::function<float()> output) { expansionAudio_ = std::move(output); }

    void writeRegister(std::uint16_t address, std::uint8_t value);  // $4000-$4013, $4015, $4017
    std::uint8_t readStatus();                                      // $4015 (clears frame IRQ)
    std::uint8_t peekStatus() const;
    bool irqAsserted() const { return frameIrq_ || dmc_.irq; }
    // DMA "get" cycles are the first half of an APU cycle, "put" the second.
    bool nextCycleIsPut() const { return !apuCycle_; }

    // DMC sample fetches are performed by the bus (they steal CPU cycles).
    bool dmcDmaRequested() const { return dmc_.dmaRequest; }
    std::uint16_t dmcDmaAddress() const { return dmc_.address; }
    void dmcDmaComplete(std::uint8_t value);

    void setSampleRate(double rate);
    double sampleRate() const { return sampleRate_; }
    std::size_t samplesAvailable() const { return samples_.size(); }
    std::size_t readSamples(float* out, std::size_t max);
    void discardSamples() { samples_.clear(); }

    // Instantaneous DAC output in [0, ~1) before filtering (for tests).
    float mixerOutput() const;
    std::uint8_t lengthCounter(unsigned channel) const;  // 0 pulse1, 1 pulse2, 2 triangle, 3 noise
    std::uint8_t channelOutput(unsigned channel) const;  // Current 4-bit (DMC 7-bit) level.
    bool frameIrqFlag() const { return frameIrq_; }

private:
    struct Envelope {
        bool start = false, loop = false, constant = false;
        std::uint8_t period = 0, divider = 0, decay = 0;
        void clock();
        std::uint8_t volume() const { return constant ? period : decay; }
    };
    struct Pulse {
        bool enabled = false, ones = false;  // ones: pulse 1 negates with ones' complement.
        std::uint8_t duty = 0, step = 0, length = 0;
        std::uint16_t period = 0, timer = 0;
        Envelope env;
        bool sweepEnabled = false, sweepNegate = false, sweepReload = false;
        std::uint8_t sweepPeriod = 0, sweepShift = 0, sweepDivider = 0;
        std::uint16_t targetPeriod() const;
        bool muted() const { return period < 8 || targetPeriod() > 0x7FF; }
        void clockTimer();
        void clockSweep();
        std::uint8_t output() const;
    };
    struct Triangle {
        bool enabled = false, control = false, reloadFlag = false;
        std::uint8_t length = 0, linear = 0, linearReload = 0, step = 0;
        std::uint16_t period = 0, timer = 0;
        void clockTimer();
        void clockLinear();
        std::uint8_t output() const;
    };
    struct Noise {
        bool enabled = false, mode = false;
        std::uint8_t length = 0;
        std::uint16_t period = 4, timer = 0, shift = 1;
        Envelope env;
        void clockTimer();
        std::uint8_t output() const;
    };
    struct Dmc {
        bool enabled = false, irqEnable = false, loop = false, irq = false;
        std::uint16_t rate = 428, timer = 0;
        std::uint8_t level = 0;
        std::uint16_t sampleAddress = 0xC000, sampleLength = 1;
        std::uint16_t address = 0xC000, remaining = 0;
        std::uint8_t buffer = 0;
        bool bufferFull = false, dmaRequest = false;
        std::uint8_t shift = 0, bitsLeft = 8;
        bool silence = true;
        void restart() { address = sampleAddress; remaining = sampleLength; }
        void clockTimer();
    };

    void clockQuarterFrame();
    void clockHalfFrame();
    void writeFrameCounter(std::uint8_t value);
    void emitSample();
    void updateDmcRequest();

    Pulse pulse1_, pulse2_;
    Triangle triangle_;
    Noise noise_;
    Dmc dmc_;

    bool fiveStep_ = false, irqInhibit_ = false, frameIrq_ = false;
    std::uint32_t frameCycle_ = 0;
    int frameResetDelay_ = -1;
    std::uint8_t pendingFrameValue_ = 0;
    bool apuCycle_ = false;  // Toggles each CPU cycle; true on "put" half.
    std::uint64_t cpuCycles_ = 0;

    double sampleRate_ = 48000.0, cyclesPerSample_ = CpuClockHz / 48000.0, sampleClock_ = 0.0;
    double accumulator_ = 0.0;
    unsigned accumulated_ = 0;
    double hp90Prev_ = 0, hp90Out_ = 0, hp440Prev_ = 0, hp440Out_ = 0, lpOut_ = 0;
    std::vector<float> samples_;
    std::function<float()> expansionAudio_;
};

} // namespace frantastic::nes

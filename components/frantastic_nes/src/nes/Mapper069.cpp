#include "frantastic/nes/Mapper069.hpp"
#include <algorithm>
#include <stdexcept>

namespace frantastic::nes {

Mapper069::Mapper069(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty() ? std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize, 0x2000), 0) : image.chr),
      ram_(image.prgRamSize ? image.prgRamSize : 0x2000, 0),
      chrRam_(image.chr.empty()),
      fourScreen_(image.mirroring == Mirroring::FourScreen),
      mirroring_(image.mirroring) {
    if (prg_.size() < 0x8000 || prg_.size() % 0x2000 || chr_.size() % 0x400)
        throw RomFormatError("NES: Sunsoft FME-7 requires 8 KiB-aligned PRG/1 KiB-aligned CHR");
    reset();
}
void Mapper069::reset() {
    chrBanks_.fill(0);
    prgBanks_.fill(0);
    command_ = ramSelect_ = irqControl_ = 0;
    irqCounter_ = 0;
    irqPending_ = false;
    soundSelect_ = 0;
    soundRegs_.fill(0);
    toneTimers_.fill(0);
    toneOutput_.fill(true);
    noiseTimer_ = envelopeTimer_ = 0;
    noiseShift_ = 1;
    noiseOutput_ = true;
    envelopeStep_ = 0;
    envelopeHolding_ = envelopeAttack_ = false;
}
std::uint8_t Mapper069::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x8000) {
        const std::size_t total = prg_.size() / 0x2000;
        const unsigned slot = (address - 0x8000) >> 13;
        const unsigned bank = slot == 3 ? static_cast<unsigned>(total - 1) : prgBanks_[slot] & 0x3F;
        return prg_[(bank % total) * 0x2000 + (address & 0x1FFF)];
    }
    if (address >= 0x6000 && (ramSelect_ & 0x80)) {
        if (ramSelect_ & 0x40) {
            if (!ram_.empty()) return ram_[((ramSelect_ & 0x3F) * 0x2000u + (address & 0x1FFFu)) % ram_.size()];
        } else {
            const std::size_t total = prg_.size() / 0x2000;
            return prg_[((ramSelect_ & 0x3F) % total) * 0x2000 + (address & 0x1FFF)];
        }
    }
    return openBus;
}
void Mapper069::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address >= 0x6000 && address < 0x8000) {
        if ((ramSelect_ & 0xC0) == 0xC0 && !ram_.empty())
            ram_[((ramSelect_ & 0x3F) * 0x2000u + (address & 0x1FFFu)) % ram_.size()] = value;
        return;
    }
    if (address < 0x8000) return;
    if (address < 0xA000) {
        command_ = value & 0x0F;
    } else if (address < 0xC000) {
        if (command_ < 8) chrBanks_[command_] = value;
        else if (command_ == 8) ramSelect_ = value;
        else if (command_ <= 11) prgBanks_[command_ - 9] = value;
        else if (command_ == 12) {
            if (!fourScreen_) {
                switch (value & 3) {
                case 0: mirroring_ = Mirroring::Vertical; break;
                case 1: mirroring_ = Mirroring::Horizontal; break;
                case 2: mirroring_ = Mirroring::SingleScreenLow; break;
                default: mirroring_ = Mirroring::SingleScreenHigh; break;
                }
            }
        } else if (command_ == 13) {
            irqControl_ = value;
            irqPending_ = false;
        } else if (command_ == 14) irqCounter_ = static_cast<std::uint16_t>((irqCounter_ & 0xFF00) | value);
        else irqCounter_ = static_cast<std::uint16_t>((irqCounter_ & 0x00FF) | (static_cast<unsigned>(value) << 8));
    } else if (address < 0xE000) {
        soundSelect_ = value;
    } else if ((soundSelect_ & 0xF0) == 0) {
        const std::uint8_t reg = soundSelect_ & 0x0F;
        soundRegs_[reg] = value;
        if (reg == 13) {
            envelopeStep_ = 0;
            envelopeHolding_ = false;
            envelopeAttack_ = (value & 4) != 0;
            envelopeTimer_ = 0;
        }
    }
}
std::uint8_t Mapper069::ppuRead(std::uint16_t address) {
    address &= 0x1FFF;
    const std::size_t bank = chrBanks_[address >> 10] % (chr_.size() / 0x400);
    return chr_[bank * 0x400 + (address & 0x3FF)];
}
void Mapper069::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) {
        address &= 0x1FFF;
        const std::size_t bank = chrBanks_[address >> 10] % (chr_.size() / 0x400);
        chr_[bank * 0x400 + (address & 0x3FF)] = value;
    }
}
void Mapper069::clockEnvelope() {
    if (envelopeHolding_) return;
    if (envelopeStep_ < 15) { ++envelopeStep_; return; }
    const unsigned shape = soundRegs_[13];
    if (!(shape & 8)) {
        envelopeHolding_ = true;
        envelopeAttack_ = false;
    } else if (shape & 1) {
        envelopeHolding_ = true;
        if (shape & 2) envelopeAttack_ = !envelopeAttack_;
    } else {
        envelopeStep_ = 0;
        if (shape & 2) envelopeAttack_ = !envelopeAttack_;
    }
}
void Mapper069::tickCpu() {
    if (irqControl_ & 0x80) {
        if (irqCounter_-- == 0 && (irqControl_ & 1)) irqPending_ = true;
    }
    for (unsigned channel = 0; channel < 3; ++channel) {
        const unsigned period = (soundRegs_[channel * 2] | ((soundRegs_[channel * 2 + 1] & 15u) << 8));
        const unsigned ticks = 16u * std::max(1u, period);
        if (toneTimers_[channel] == 0) {
            toneTimers_[channel] = static_cast<std::uint16_t>(ticks);
            toneOutput_[channel] = !toneOutput_[channel];
        }
        --toneTimers_[channel];
    }
    const unsigned noiseTicks = 16u * std::max(1u, static_cast<unsigned>(soundRegs_[6] & 31u));
    if (!noiseTimer_) {
        noiseTimer_ = static_cast<std::uint16_t>(noiseTicks);
        noiseOutput_ = (noiseShift_ & 1) != 0;
        noiseShift_ = static_cast<std::uint16_t>((noiseShift_ >> 1) | (((noiseShift_ ^ (noiseShift_ >> 3)) & 1u) << 15));
    }
    --noiseTimer_;
    const unsigned envPeriod = soundRegs_[11] | (static_cast<unsigned>(soundRegs_[12]) << 8);
    const unsigned envTicks = 16u * std::max(1u, envPeriod);
    if (!envelopeTimer_) {
        envelopeTimer_ = static_cast<std::uint16_t>(std::min(envTicks, 65535u));
        clockEnvelope();
    }
    --envelopeTimer_;
}
float Mapper069::expansionAudio() const {
    // Volume curve approximates the AY-3-8910 / YM2149 exponential DAC.
    static constexpr float levels[16] = {
        0.f, .004f, .006f, .009f, .014f, .020f, .029f, .043f,
        .062f, .092f, .136f, .200f, .295f, .435f, .640f, 1.f
    };
    float sum = 0;
    for (unsigned ch = 0; ch < 3; ++ch) {
        const bool toneOn = (soundRegs_[7] & (1u << ch)) || toneOutput_[ch];
        const bool noiseOn = (soundRegs_[7] & (1u << (ch + 3))) || noiseOutput_;
        if (toneOn && noiseOn) {
            const unsigned volume = (soundRegs_[ch + 8] & 0x10)
                ? (envelopeAttack_ ? envelopeStep_ : 15u - envelopeStep_)
                : (soundRegs_[ch + 8] & 0x0F);
            sum += levels[volume];
        }
    }
    return sum * .045f;
}
} // namespace frantastic::nes

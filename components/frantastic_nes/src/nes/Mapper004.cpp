#include "frantastic/nes/Mapper004.hpp"
#include <algorithm>

namespace frantastic::nes {

Mapper004::Mapper004(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty()
            ? std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize, 0x2000u), 0)
            : image.chr),
      prgRam_(image.prgRamSize ? image.prgRamSize : 0x2000u, 0),
      chrRam_(image.chr.empty()), fourScreen_(image.mirroring == Mirroring::FourScreen),
      earlyIrq_(image.format == RomFormat::Nes20 && image.submapper == 3),
      mirroring_(image.mirroring) {
    if (prg_.size() < 0x8000u || prg_.size() % 0x2000u != 0)
        throw RomFormatError("NES: MMC3 requires PRG ROM in 8 KiB banks (at least 32 KiB)");
    if (chr_.size() < 0x2000u || chr_.size() % 0x400u != 0)
        throw RomFormatError("NES: MMC3 requires CHR in 1 KiB banks (at least 8 KiB)");
    // Mapper 4 submapper 1 denotes MMC6, whose WRAM differs from MMC3.
    if (image.format == RomFormat::Nes20 && image.submapper == 1)
        throw RomFormatError("NES: MMC6 (mapper 4 submapper 1) is not implemented");
    reset();
}

void Mapper004::reset() {
    banks_.fill(0);
    bankSelect_ = 0;
    ramProtect_ = 0x80;
    irqLatch_ = irqCounter_ = 0;
    irqReload_ = irqEnabled_ = irqPending_ = false;
    lastA12_ = false;
    lastLowDot_ = 0;
}

std::size_t Mapper004::prgOffset(std::uint16_t address) const {
    const std::size_t count = prg_.size() / 0x2000u;
    const unsigned slot = (address - 0x8000u) / 0x2000u;
    const bool flip = (bankSelect_ & 0x40u) != 0;
    std::size_t bank = 0;
    if (slot == 3) bank = count - 1;
    else if (slot == 1) bank = banks_[7];
    else if ((slot == 0 && flip) || (slot == 2 && !flip)) bank = count - 2;
    else bank = banks_[6];
    return (bank % count) * 0x2000u + (address & 0x1FFFu);
}

std::size_t Mapper004::chrOffset(std::uint16_t address) const {
    const unsigned slot = (address & 0x1FFFu) / 0x400u;
    const bool flip = (bankSelect_ & 0x80u) != 0;
    unsigned logical = flip ? (slot ^ 4u) : slot;
    std::size_t bank;
    if (logical < 2) bank = (banks_[0] & 0xFEu) + logical;
    else if (logical < 4) bank = (banks_[1] & 0xFEu) + (logical - 2u);
    else bank = banks_[logical - 2u];
    return (bank % (chr_.size() / 0x400u)) * 0x400u + (address & 0x3FFu);
}

std::uint8_t Mapper004::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x8000u) return prg_[prgOffset(address)];
    if (address >= 0x6000u && !prgRam_.empty() && (ramProtect_ & 0x80u))
        return prgRam_[(address - 0x6000u) % prgRam_.size()];
    return openBus;
}

void Mapper004::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address >= 0x6000u && address < 0x8000u) {
        if (!prgRam_.empty() && (ramProtect_ & 0x80u) && !(ramProtect_ & 0x40u))
            prgRam_[(address - 0x6000u) % prgRam_.size()] = value;
        return;
    }
    if (address < 0x8000u) return;
    switch ((address >> 13) & 3u) {
    case 0:
        if (!(address & 1u)) bankSelect_ = value;
        else banks_[bankSelect_ & 7u] = value;
        break;
    case 1:
        if (!(address & 1u)) {
            if (!fourScreen_) mirroring_ = (value & 1u) ? Mirroring::Horizontal : Mirroring::Vertical;
        } else ramProtect_ = value;
        break;
    case 2:
        if (!(address & 1u)) irqLatch_ = value;
        else irqReload_ = true;
        break;
    default:
        if (!(address & 1u)) {
            irqEnabled_ = false;
            irqPending_ = false;
        } else irqEnabled_ = true;
        break;
    }
}

std::uint8_t Mapper004::ppuRead(std::uint16_t address) {
    return chr_[chrOffset(address)];
}

void Mapper004::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) chr_[chrOffset(address)] = value;
}

void Mapper004::clockIrq() {
    const std::uint8_t old = irqCounter_;
    if (irqCounter_ == 0 || irqReload_) irqCounter_ = irqLatch_;
    else --irqCounter_;
    if (irqEnabled_ && irqCounter_ == 0 && (!earlyIrq_ || (old != 0 && !irqReload_)))
        irqPending_ = true;
    irqReload_ = false;
}

void Mapper004::ppuAddressTick(std::uint16_t address, std::uint64_t dot) {
    const bool a12 = (address & 0x1000u) != 0;
    if (!a12) {
        // The MMC3's A12 low-pass filter ignores short low pulses.
        if (lastA12_) lastLowDot_ = dot;
    } else if (!lastA12_ && dot - lastLowDot_ >= 8u) {
        clockIrq();
    }
    lastA12_ = a12;
}

} // namespace frantastic::nes

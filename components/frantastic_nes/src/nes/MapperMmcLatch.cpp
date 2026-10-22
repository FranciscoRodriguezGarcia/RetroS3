#include "frantastic/nes/MapperMmcLatch.hpp"
#include <algorithm>

namespace frantastic::nes {

MapperMmcLatch::MapperMmcLatch(const RomImage& image)
    : prg_(image.prg), chr_(image.chr), mmc4_(image.mapper == 10),
      mirroring_(image.mirroring) {
    const std::size_t prgUnit = mmc4_ ? 0x4000u : 0x2000u;
    if (prg_.size() < 0x8000u || prg_.size() % prgUnit)
        throw RomFormatError("NES: MMC2/MMC4 requires at least 32 KiB PRG ROM in bank units");
    if (chr_.size() < 0x2000u || chr_.size() % 0x1000u)
        throw RomFormatError("NES: MMC2/MMC4 requires CHR ROM in 4 KiB banks");
    reset();
}

void MapperMmcLatch::reset() {
    prgBank_ = 0;
    chrBanks_.fill(0);
    latchFe_.fill(true); // Power-up latches are FE; registers initially 0.
}

std::uint8_t MapperMmcLatch::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x6000u && address < 0x8000u)
        return mmc4_ ? prgRam_[address - 0x6000u] : openBus;
    if (address < 0x8000u) return openBus;
    if (mmc4_) {
        const std::size_t count = prg_.size() / 0x4000u;
        const std::size_t bank = address < 0xC000u ? (prgBank_ % count) : count - 1u;
        return prg_[bank * 0x4000u + (address & 0x3FFFu)];
    }
    const std::size_t count = prg_.size() / 0x2000u;
    const std::size_t slot = (address - 0x8000u) / 0x2000u;
    const std::size_t bank = slot == 0 ? prgBank_ % count : count - 4u + slot;
    return prg_[bank * 0x2000u + (address & 0x1FFFu)];
}

void MapperMmcLatch::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (mmc4_ && address >= 0x6000u && address < 0x8000u) {
        prgRam_[address - 0x6000u] = value;
        return;
    }
    if (address < 0xA000u) return;
    switch (address & 0xF000u) {
    case 0xA000u: prgBank_ = value & 0x0Fu; break;
    case 0xB000u: chrBanks_[0] = value & 0x1Fu; break;
    case 0xC000u: chrBanks_[1] = value & 0x1Fu; break;
    case 0xD000u: chrBanks_[2] = value & 0x1Fu; break;
    case 0xE000u: chrBanks_[3] = value & 0x1Fu; break;
    case 0xF000u: mirroring_ = (value & 1u) ? Mirroring::Horizontal : Mirroring::Vertical; break;
    default: break;
    }
}

std::uint8_t MapperMmcLatch::ppuRead(std::uint16_t address) {
    address &= 0x1FFFu;
    const unsigned half = address >> 12;
    const unsigned index = half * 2u + (latchFe_[half] ? 1u : 0u);
    const std::size_t bankCount = chr_.size() / 0x1000u;
    const std::uint8_t data = chr_[(static_cast<std::size_t>(chrBanks_[index]) % bankCount) * 0x1000u +
                                   (address & 0x0FFFu)];

    // The latch is clocked by the high-byte tile fetch ($xFD8/$xFE8),
    // after returning the data from the bank selected before that access.
    const unsigned offset = address & 0x0FFFu;
    if (offset >= 0x0FD8u && offset <= 0x0FDFu) latchFe_[half] = false;
    else if (offset >= 0x0FE8u && offset <= 0x0FEFu) latchFe_[half] = true;
    return data;
}

} // namespace frantastic::nes

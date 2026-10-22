#include "frantastic/nes/Mapper001.hpp"
#include <algorithm>
#include <string>

namespace frantastic::nes {

namespace {
std::vector<std::uint8_t> chrOrRam(const RomImage& image) {
    if (!image.chr.empty()) return image.chr;
    return std::vector<std::uint8_t>(image.chrRamSize ? image.chrRamSize : 0x2000u, 0);
}
void requirePrg(const std::vector<std::uint8_t>& prg, const char* board) {
    if (prg.empty() || prg.size() % 0x4000u != 0)
        throw RomFormatError(std::string("NES: ") + board + " requires PRG ROM in 16 KiB units");
}
} // namespace

// ---- MMC1 -----------------------------------------------------------------------
Mapper001::Mapper001(const RomImage& image)
    : prg_(image.prg), chr_(chrOrRam(image)), chrRam_(image.chr.empty()) {
    requirePrg(prg_, "MMC1");
    if (chr_.size() < 0x2000u) chr_.resize(0x2000u, 0);
    reset();
}

void Mapper001::reset() {
    shift_ = 0x10;
    control_ = 0x0C;  // PRG mode 3: last bank fixed at $C000 at power-on.
    lastWriteCycle_ = UINT64_MAX;
}

Mirroring Mapper001::mirroring() const {
    switch (control_ & 3u) {
    case 0: return Mirroring::SingleScreenLow;
    case 1: return Mirroring::SingleScreenHigh;
    case 2: return Mirroring::Vertical;
    default: return Mirroring::Horizontal;
    }
}

std::size_t Mapper001::prgOffset(std::uint16_t address) const {
    const std::size_t banks = prg_.size() / 0x4000u;
    // SUROM/SXROM: CHR bank 0 bit 4 selects the 256 KiB half.
    const std::size_t outer = banks > 16 ? (chr0_ & 0x10u) : 0u;
    std::size_t bank;
    const unsigned mode = (control_ >> 2) & 3u;
    const bool high = address >= 0xC000u;
    if (mode < 2) bank = (prgBank_ & 0x0Eu) + (high ? 1u : 0u);
    else if (mode == 2) bank = high ? (prgBank_ & 0x0Fu) : 0u;
    else bank = high ? 0x0Fu : (prgBank_ & 0x0Fu);
    bank = (outer | bank) % banks;
    if (mode == 3 && high) bank = (outer | (std::min<std::size_t>(banks, 16) - 1)) % banks;
    return bank * 0x4000u + (address & 0x3FFFu);
}

std::size_t Mapper001::chrOffset(std::uint16_t address) const {
    std::size_t bank4;
    if (control_ & 0x10u) bank4 = (address & 0x1000u) ? chr1_ : chr0_;
    else bank4 = (chr0_ & 0x1Eu) + ((address & 0x1000u) ? 1u : 0u);
    return (bank4 * 0x1000u + (address & 0x0FFFu)) % chr_.size();
}

std::uint8_t Mapper001::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x8000u) return prg_[prgOffset(address)];
    if (address >= 0x6000u) return (prgBank_ & 0x10u) ? openBus : prgRam_[address & 0x1FFFu];
    return openBus;
}

void Mapper001::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address < 0x6000u) return;
    if (address < 0x8000u) {
        if (!(prgBank_ & 0x10u)) prgRam_[address & 0x1FFFu] = value;
        return;
    }
    if (value & 0x80u) {
        shift_ = 0x10;
        control_ |= 0x0Cu;
        return;
    }
    const bool full = shift_ & 1u;
    shift_ = static_cast<std::uint8_t>((shift_ >> 1) | ((value & 1u) << 4));
    if (!full) return;
    const std::uint8_t data = shift_;
    shift_ = 0x10;
    switch ((address >> 13) & 3u) {
    case 0: control_ = data; break;
    case 1: chr0_ = data; break;
    case 2: chr1_ = data; break;
    default: prgBank_ = data; break;
    }
}

void Mapper001::cpuWriteTimed(std::uint16_t address, std::uint8_t value,
                              std::uint64_t cpuCycle) {
    // $6000-$7FFF is PRG RAM, not the MMC1 serial port. It must accept
    // back-to-back CPU writes and must not perturb serial-port timing.
    if (address < 0x8000u) {
        cpuWrite(address, value);
        return;
    }
    // MMC1's D7 reset path is recognized even on the second cycle of an
    // RMW instruction; only consecutive D0 serial DATA writes are filtered.
    // The reset cycle is still a write cycle for the next serial DATA access.
    if (value & 0x80u) {
        lastWriteCycle_ = cpuCycle;
        cpuWrite(address, value);
        return;
    }
    if (lastWriteCycle_ != UINT64_MAX && cpuCycle == lastWriteCycle_ + 1)
        return;
    lastWriteCycle_ = cpuCycle;
    cpuWrite(address, value);
}

void Mapper001::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) {
        ++debugChrWrites_;
        chr_[chrOffset(address)] = value;
    }
}

std::size_t Mapper001::debugChrNonzero() const {
    return static_cast<std::size_t>(std::count_if(chr_.begin(), chr_.end(),
        [](std::uint8_t value) { return value != 0; }));
}

// ---- UxROM ----------------------------------------------------------------------
Mapper002::Mapper002(const RomImage& image)
    : prg_(image.prg), chr_(chrOrRam(image)), chrRam_(image.chr.empty()), mirroring_(image.mirroring) {
    requirePrg(prg_, "UxROM");
    if (chr_.size() < 0x2000u) chr_.resize(0x2000u, 0);
}

std::uint8_t Mapper002::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address < 0x8000u) return openBus;
    const std::size_t banks = prg_.size() / 0x4000u;
    const std::size_t bank = address >= 0xC000u ? banks - 1 : bank_ % banks;
    return prg_[bank * 0x4000u + (address & 0x3FFFu)];
}

void Mapper002::cpuWrite(std::uint16_t address, std::uint8_t value) {
    // Bus conflicts: the written value is ANDed with the ROM byte.
    if (address >= 0x8000u) bank_ = value & cpuPeek(address, 0xFF);
}

// ---- CNROM ----------------------------------------------------------------------
Mapper003::Mapper003(const RomImage& image)
    : prg_(image.prg), chr_(chrOrRam(image)), chrRam_(image.chr.empty()), mirroring_(image.mirroring) {
    if (prg_.size() != 0x4000u && prg_.size() != 0x8000u)
        throw RomFormatError("NES: CNROM requires 16 KiB or 32 KiB PRG ROM");
    if (chr_.size() < 0x2000u) chr_.resize(0x2000u, 0);
}

std::uint8_t Mapper003::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address < 0x8000u) return openBus;
    return prg_[(address - 0x8000u) & (prg_.size() - 1u)];
}

void Mapper003::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address >= 0x8000u) bank_ = value & cpuPeek(address, 0xFF);
}

std::uint8_t Mapper003::ppuRead(std::uint16_t address) {
    return chr_[(static_cast<std::size_t>(bank_) * 0x2000u + (address & 0x1FFFu)) % chr_.size()];
}

void Mapper003::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) chr_[(static_cast<std::size_t>(bank_) * 0x2000u + (address & 0x1FFFu)) % chr_.size()] = value;
}

} // namespace frantastic::nes

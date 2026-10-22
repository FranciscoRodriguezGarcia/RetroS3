#include "frantastic/nes/Mapper000.hpp"
#include <string>

namespace frantastic::nes {

Mapper000::Mapper000(const RomImage& image) : prg_(image.prg), chr_(image.chr), mirroring_(image.mirroring) {
    if (prg_.size() != 0x4000u && prg_.size() != 0x8000u)
        throw RomFormatError("NES: NROM requires 16 KiB or 32 KiB PRG ROM, got " + std::to_string(prg_.size()) + " bytes");
    if (chr_.empty()) {
        chrRam_ = true;
        chr_.assign(image.chrRamSize ? image.chrRamSize : 0x2000u, 0);
    }
    if (chr_.size() < 0x2000u) chr_.resize(0x2000u, 0);
}

std::uint8_t Mapper000::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x8000u) return prg_[(address - 0x8000u) & (prg_.size() - 1u)];  // NROM-128 mirrors $8000 at $C000.
    if (address >= 0x6000u) return prgRam_[address & 0x1FFFu];
    return openBus;
}

std::uint8_t Mapper000::cpuRead(std::uint16_t address, std::uint8_t openBus) { return cpuPeek(address, openBus); }

void Mapper000::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address >= 0x6000u && address < 0x8000u) prgRam_[address & 0x1FFFu] = value;
    // Writes to PRG ROM have no effect on NROM.
}

std::uint8_t Mapper000::ppuRead(std::uint16_t address) { return chr_[address & 0x1FFFu]; }

void Mapper000::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) chr_[address & 0x1FFFu] = value;
}

} // namespace frantastic::nes

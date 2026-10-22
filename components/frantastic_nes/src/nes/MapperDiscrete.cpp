#include "frantastic/nes/MapperDiscrete.hpp"
#include <algorithm>

namespace frantastic::nes {
namespace {
void requirePrg32(const RomImage& image, const char* board) {
    if (image.prg.size() < 0x8000u || image.prg.size() % 0x8000u)
        throw RomFormatError(std::string("NES: ") + board + " requires PRG ROM in 32 KiB banks");
}
std::vector<std::uint8_t> chrMemory(const RomImage& image) {
    return image.chr.empty()
        ? std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize, 0x2000u), 0)
        : image.chr;
}
}

Mapper007::Mapper007(const RomImage& image)
    : prg_(image.prg), chr_(std::max<std::size_t>(image.chrRamSize, 0x2000u), 0),
      busConflicts_(image.format == RomFormat::Nes20 && image.submapper == 2) {
    requirePrg32(image, "AxROM");
    if (!image.chr.empty()) throw RomFormatError("NES: AxROM requires CHR RAM");
    reset();
}

std::uint8_t Mapper007::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address < 0x8000u) return openBus;
    const std::size_t count = prg_.size() / 0x8000u;
    return prg_[(static_cast<std::size_t>(bank_) % count) * 0x8000u + (address & 0x7FFFu)];
}

void Mapper007::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address < 0x8000u) return;
    if (busConflicts_) value &= cpuPeek(address, 0xFFu);
    bank_ = value & 7u;
    singleHigh_ = (value & 0x10u) != 0;
}

Mapper066::Mapper066(const RomImage& image)
    : prg_(image.prg), chr_(chrMemory(image)), chrRam_(image.chr.empty()),
      mirroring_(image.mirroring) {
    requirePrg32(image, "GNROM");
    if (chr_.size() < 0x2000u || chr_.size() % 0x2000u)
        throw RomFormatError("NES: GNROM requires CHR in 8 KiB banks");
}

std::uint8_t Mapper066::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address < 0x8000u) return openBus;
    const std::size_t count = prg_.size() / 0x8000u;
    return prg_[(static_cast<std::size_t>(prgBank_) % count) * 0x8000u + (address & 0x7FFFu)];
}

void Mapper066::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address < 0x8000u) return;
    value &= cpuPeek(address, 0xFFu);
    prgBank_ = (value >> 4) & 3u;
    chrBank_ = value & 3u;
}

std::uint8_t Mapper066::ppuRead(std::uint16_t address) {
    const std::size_t count = chr_.size() / 0x2000u;
    return chr_[(static_cast<std::size_t>(chrBank_) % count) * 0x2000u + (address & 0x1FFFu)];
}

void Mapper066::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (!chrRam_) return;
    const std::size_t count = chr_.size() / 0x2000u;
    chr_[(static_cast<std::size_t>(chrBank_) % count) * 0x2000u + (address & 0x1FFFu)] = value;
}

Mapper034::Mapper034(const RomImage& image)
    : prg_(image.prg), chr_(chrMemory(image)),
      nina_((image.format == RomFormat::Nes20 && image.submapper == 1) ||
            (!(image.format == RomFormat::Nes20 && image.submapper == 2) && image.chr.size() > 0x2000u)),
      chrRam_(image.chr.empty()), mirroring_(image.mirroring) {
    requirePrg32(image, "mapper 34");
    if (image.format == RomFormat::Nes20 && image.submapper > 2)
        throw RomFormatError("NES: unsupported mapper 34 submapper");
    if (chr_.size() < 0x2000u || chr_.size() % 0x1000u)
        throw RomFormatError("NES: mapper 34 requires CHR in 4 KiB banks");
    reset();
}

std::uint8_t Mapper034::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address >= 0x8000u) {
        const std::size_t count = prg_.size() / 0x8000u;
        return prg_[(static_cast<std::size_t>(prgBank_) % count) * 0x8000u + (address & 0x7FFFu)];
    }
    if (nina_ && address >= 0x6000u) return prgRam_[address & 0x1FFFu];
    return openBus;
}

void Mapper034::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (nina_) {
        if (address < 0x6000u || address >= 0x8000u) return;
        prgRam_[address & 0x1FFFu] = value;
        if (address == 0x7FFDu) prgBank_ = value & 1u;
        else if (address == 0x7FFEu) chrBank0_ = value & 0x0Fu;
        else if (address == 0x7FFFu) chrBank1_ = value & 0x0Fu;
    } else if (address >= 0x8000u) {
        // BNROM drives CPU and ROM onto the same data bus during writes.
        prgBank_ = value & cpuPeek(address, 0xFFu);
    }
}

std::uint8_t Mapper034::ppuRead(std::uint16_t address) {
    const std::size_t offset = nina_
        ? (static_cast<std::size_t>((address & 0x1000u) ? chrBank1_ : chrBank0_) * 0x1000u +
           (address & 0x0FFFu)) % chr_.size()
        : address & 0x1FFFu;
    return chr_[offset];
}

void Mapper034::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (!chrRam_) return;
    const std::size_t offset = nina_
        ? (static_cast<std::size_t>((address & 0x1000u) ? chrBank1_ : chrBank0_) * 0x1000u +
           (address & 0x0FFFu)) % chr_.size()
        : address & 0x1FFFu;
    chr_[offset] = value;
}

} // namespace frantastic::nes

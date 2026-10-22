#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include "frantastic/nes/Mapper.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {

// NROM (iNES mapper 0): 16 KiB (NROM-128, mirrored at $C000) or 32 KiB
// (NROM-256) PRG ROM, 8 KiB CHR ROM or CHR RAM, fixed solder-pad mirroring.
// An 8 KiB PRG RAM window at $6000-$7FFF is provided (Family BASIC boards and
// test ROMs such as blargg's use it for results); real NROM games ignore it.
class Mapper000 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper000(const RomImage& image);
    unsigned id() const override { return 0; }
    const char* name() const override { return "NROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override;
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override {}
    bool chrIsRam() const { return chrRam_; }

private:
    std::vector<std::uint8_t> prg_;
    std::vector<std::uint8_t> chr_;
    std::array<std::uint8_t, 0x2000> prgRam_{};
    bool chrRam_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
};

} // namespace frantastic::nes

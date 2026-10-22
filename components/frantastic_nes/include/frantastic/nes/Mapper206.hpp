#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {

// Namco 108 / Tengen MIMIC-1 (DxROM, iNES mapper 206).
// MMC3-style PRG/CHR banking with fixed mirroring, no IRQ or WRAM.
class Mapper206 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper206(const RomImage& image);
    unsigned id() const override { return 206; }
    const char* name() const override { return "Namco 108 / Tengen MIMIC-1"; }
    std::uint8_t cpuRead(std::uint16_t a, std::uint8_t bus) override { return cpuPeek(a,bus); }
    std::uint8_t cpuPeek(std::uint16_t a, std::uint8_t bus) const override;
    void cpuWrite(std::uint16_t a, std::uint8_t v) override;
    std::uint8_t ppuRead(std::uint16_t a) override;
    void ppuWrite(std::uint16_t a, std::uint8_t v) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
private:
    std::vector<std::uint8_t> prg_,chr_;
    std::array<std::uint8_t,8> bank_{};
    std::uint8_t selected_=0;
    bool chrRam_=false;
    Mirroring mirroring_;
    std::size_t chrOffset(std::uint16_t a) const;
};
} // namespace frantastic::nes

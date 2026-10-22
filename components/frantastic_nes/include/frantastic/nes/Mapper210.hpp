#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {
// Namco 175 (submapper 1) / Namco 340 (submapper 2).
class Mapper210 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper210(const RomImage& image);
    unsigned id() const override { return 210; }
    const char* name() const override { return namco175_ ? "Namco 175" : "Namco 340"; }
    std::uint8_t cpuRead(std::uint16_t address,std::uint8_t bus) override { return cpuPeek(address,bus); }
    std::uint8_t cpuPeek(std::uint16_t address,std::uint8_t bus) const override;
    void cpuWrite(std::uint16_t address,std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address,std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
private:
    std::vector<std::uint8_t> prg_,chr_,ram_;
    bool chrRam_=false,namco175_=false,ramEnabled_=false;
    Mirroring mirroring_=Mirroring::Vertical,hardwareMirroring_=Mirroring::Vertical;
    std::array<std::uint8_t,8> chrBanks_{};
    std::array<std::uint8_t,3> prgBanks_{};
};
} // namespace frantastic::nes

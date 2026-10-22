#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {
// JY830623C / YY840238C and EJ-006-1 (NES 2.0 submapper 1).
class Mapper091 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper091(const RomImage& image);
    unsigned id() const override { return 91; }
    const char* name() const override { return "JY Company / Super Fighter III"; }
    std::uint8_t cpuRead(std::uint16_t a,std::uint8_t bus) override { return cpuPeek(a,bus); }
    std::uint8_t cpuPeek(std::uint16_t a,std::uint8_t bus) const override;
    void cpuWrite(std::uint16_t a,std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t a) override;
    void ppuWrite(std::uint16_t a,std::uint8_t value) override;
    void ppuAddressTick(std::uint16_t a,std::uint64_t dot) override;
    void tickCpu() override;
    bool irqAsserted() const override { return irqPending_; }
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
private:
    std::vector<std::uint8_t> prg_,chr_;
    bool chrRam_=false,sub1_=false,irqEnabled_=false,irqPending_=false,lastA12_=false;
    Mirroring mirroring_=Mirroring::Vertical,hardwareMirroring_=Mirroring::Vertical;
    std::array<std::uint8_t,2> prgBanks_{};
    std::array<std::uint8_t,4> chrBanks_{};
    std::uint8_t outerBank_=0,ppuCounter_=0,cpuPhase_=0;
    std::uint16_t cpuCounter_=0;
};
} // namespace frantastic::nes

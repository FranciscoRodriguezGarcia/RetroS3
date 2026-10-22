#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {
// Jaleco SS88006 (18) and Taito TC0190 (33).
class MapperJalecoTaito final : public Mapper {
    friend class NesStateCodec;
public:
    explicit MapperJalecoTaito(const RomImage& image);
    unsigned id() const override { return id_; }
    const char* name() const override { return id_==18 ? "Jaleco SS88006" : "Taito TC0190"; }
    std::uint8_t cpuRead(std::uint16_t a,std::uint8_t bus) override {return cpuPeek(a,bus);}
    std::uint8_t cpuPeek(std::uint16_t a,std::uint8_t bus) const override;
    void cpuWrite(std::uint16_t a,std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t a) override;
    void ppuWrite(std::uint16_t a,std::uint8_t value) override;
    Mirroring mirroring() const override {return mirroring_;}
    bool irqAsserted() const override {return irqPending_;}
    void tickCpu() override;
    void reset() override;
private:
    unsigned id_;
    std::vector<std::uint8_t> prg_,chr_,ram_;
    bool chrRam_=false,ramEnabled_=false,ramWritable_=false;
    Mirroring mirroring_,hardwareMirroring_;
    std::array<std::uint8_t,3> prgBanks_{};
    std::array<std::uint8_t,8> chrBanks_{};
    std::uint16_t irqLatch_=0,irqCounter_=0;
    std::uint8_t irqControl_=0;
    bool irqPending_=false;
    void writeNibble(std::uint8_t& bank,unsigned nibble,std::uint8_t value);
};
} // namespace frantastic::nes

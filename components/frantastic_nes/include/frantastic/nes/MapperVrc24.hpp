#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// VRC2a/b/c and VRC4a/b/c/d/e/f.  NES 2.0 submappers identify
// board address-pin wiring and whether the IRQ-capable VRC4 is fitted.
// Legacy iNES maps both non-overlapping wiring layouts.
class MapperVrc24 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit MapperVrc24(const RomImage& image);
    unsigned id() const override { return mapperId_; }
    const char* name() const override { return vrc2_ ? "Konami VRC2" : "Konami VRC4"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    bool irqAsserted() const override { return !vrc2_ && irqPending_; }
    void tickCpu() override;
    void reset() override;
private:
    unsigned mapperId_ = 0, submapper_ = 0;
    bool nes20_ = false, vrc2_ = false, chrRam_ = false, fourScreen_ = false;
    std::vector<std::uint8_t> prg_, chr_, ram_;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::array<std::uint16_t, 8> chrBanks_{};
    std::uint8_t prg0_ = 0, prg1_ = 0, prgMode_ = 0, ramEnabled_ = 0, latch_ = 0;
    std::uint8_t irqLatch_ = 0, irqCounter_ = 0, irqControl_ = 0;
    int irqPrescaler_ = 341;
    bool irqEnabled_ = false, irqPending_ = false;
    unsigned registerIndex(std::uint16_t address) const;
    void clockIrq();
};
} // namespace frantastic::nes

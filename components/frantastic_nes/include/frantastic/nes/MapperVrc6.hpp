#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Konami VRC6a (24) / VRC6b (26): reversed A0/A1, PRG/CHR banking,
// CPU/scanline IRQ and two pulses plus a sawtooth expansion sound channel.
class MapperVrc6 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit MapperVrc6(const RomImage& image);
    unsigned id() const override { return mapperId_; }
    const char* name() const override { return mapperId_ == 24 ? "Konami VRC6a" : "Konami VRC6b"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    bool irqAsserted() const override { return irqPending_; }
    void tickCpu() override;
    float expansionAudio() const override;
    void reset() override;
private:
    struct Pulse {
        std::array<std::uint8_t,3> regs{};
        std::uint16_t divider = 0;
        std::uint8_t phase = 15;
    };
    unsigned mapperId_ = 24;
    std::vector<std::uint8_t> prg_, chr_, ram_;
    bool chrRam_ = false, fourScreen_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::uint8_t prg16_ = 0, prg8_ = 0, banking_ = 0;
    std::array<std::uint8_t,8> chrBanks_{};
    std::array<Pulse,2> pulses_{};
    std::array<std::uint8_t,3> sawRegs_{};
    std::uint16_t sawDivider_ = 0;
    std::uint8_t sawStep_ = 0, sawAccumulator_ = 0, audioControl_ = 0;
    std::uint8_t irqLatch_ = 0, irqCounter_ = 0, irqControl_ = 0;
    int irqPrescaler_ = 341;
    bool irqEnabled_ = false, irqPending_ = false;
    unsigned normalizedRegister(std::uint16_t address) const;
    void clockIrq();
    unsigned patternBank(unsigned slot) const;
};
} // namespace frantastic::nes

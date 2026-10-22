#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Sunsoft FME-7 / 5A / 5B, iNES mapper 69.
// FME-7 provides PRG/CHR banking and a CPU-cycle IRQ counter;
// Sunsoft 5B adds the cartridge-side three-voice PSG.
class Mapper069 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper069(const RomImage& image);
    unsigned id() const override { return 69; }
    const char* name() const override { return "Sunsoft FME-7/5B"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    void tickCpu() override;
    bool irqAsserted() const override { return irqPending_; }
    float expansionAudio() const override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
private:
    std::vector<std::uint8_t> prg_, chr_, ram_;
    bool chrRam_ = false, fourScreen_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::array<std::uint8_t, 8> chrBanks_{};
    std::array<std::uint8_t, 3> prgBanks_{};
    std::uint8_t command_ = 0, ramSelect_ = 0;
    std::uint8_t irqControl_ = 0;
    std::uint16_t irqCounter_ = 0;
    bool irqPending_ = false;
    std::uint8_t soundSelect_ = 0;
    std::array<std::uint8_t, 16> soundRegs_{};
    std::array<std::uint16_t, 3> toneTimers_{};
    std::array<bool, 3> toneOutput_{};
    std::uint16_t noiseTimer_ = 0, noiseShift_ = 1;
    bool noiseOutput_ = true;
    std::uint16_t envelopeTimer_ = 0;
    std::uint8_t envelopeStep_ = 0;
    bool envelopeHolding_ = false, envelopeAttack_ = false;
    void clockEnvelope();
};
} // namespace frantastic::nes

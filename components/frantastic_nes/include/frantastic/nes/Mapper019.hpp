#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Namco 163 / 129, mapper 19. Banked CHR/nametables, a 15-bit CPU IRQ,
// internal 128-byte RAM and up to eight multiplexed wavetable voices.
class Mapper019 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper019(const RomImage& image);
    unsigned id() const override { return 19; }
    const char* name() const override { return "Namco 163"; }
    std::uint8_t cpuRead(std::uint16_t address,std::uint8_t openBus) override;
    std::uint8_t cpuPeek(std::uint16_t address,std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address,std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address,std::uint8_t value) override;
    std::uint8_t ppuNametableRead(std::uint16_t address,std::uint8_t* vram,bool backgroundFetch) override;
    void ppuNametableWrite(std::uint16_t address,std::uint8_t value,std::uint8_t* vram) override;
    Mirroring mirroring() const override { return mirroring_; }
    bool irqAsserted() const override { return irqPending_; }
    void tickCpu() override;
    float expansionAudio() const override;
    void reset() override;
private:
    std::vector<std::uint8_t> prg_,chr_,ram_;
    bool chrRam_ = false;
    Mirroring mirroring_ = Mirroring::Vertical;
    std::array<std::uint8_t,12> chrBanks_{};
    std::array<std::uint8_t,3> prgBanks_{};
    std::array<std::uint8_t,128> soundRam_{};
    std::uint8_t soundAddress_=0,ramControl_=0,prgControl_=0,chrControl_=0,auxControl_=0;
    std::uint16_t irqCounter_=0;
    bool irqEnabled_=false,irqPending_=false;
    unsigned soundDivider_=0,soundChannel_=7;
    float soundOutput_=0;
    std::uint8_t* ciram_=nullptr; // Non-owning PPU nametable memory.
    std::uint8_t readPattern(std::uint16_t address,unsigned slot) const;
    void writePattern(std::uint16_t address,unsigned slot,std::uint8_t value);
    void clockSound();
    void incrementSoundAddress();
};
} // namespace frantastic::nes

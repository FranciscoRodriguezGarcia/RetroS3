#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// MMC5/ExROM register and memory controller (PRG/CHR, ExRAM, IRQ and sound).
class Mapper005 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper005(const RomImage& image);
    unsigned id() const override { return 5; }
    const char* name() const override { return "MMC5"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override;
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuReadBackground(std::uint16_t address, bool sprites16) override;
    std::uint8_t ppuReadSprite(std::uint16_t address, bool sprites16) override;
    std::uint8_t ppuNametableRead(std::uint16_t address, std::uint8_t* vram,
                                  bool backgroundFetch) override;
    void ppuNametableWrite(std::uint16_t address, std::uint8_t value,
                            std::uint8_t* vram) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
    bool irqAsserted() const override { return (irqEnable_ && irqPending_) || (pcmIrqEnable_ && pcmIrqPending_); }
    void tickCpu() override;
    float expansionAudio() const override;
    void ppuRenderTick(int scanline, int dot, bool rendering) override;
    void ppuBackgroundTile(unsigned column, int scanline) override;
private:
    struct PrgWindow { std::uint8_t bank; std::size_t size; std::size_t offset; bool forceRom; };
    PrgWindow window(std::uint16_t address) const;
    std::uint8_t readPrg(std::uint16_t address, std::uint8_t openBus) const;
    void writePrg(std::uint16_t address, std::uint8_t value);
    std::size_t chrOffset(std::uint16_t address, bool background, bool sprites16) const;
    std::uint8_t nametableSource(std::uint16_t address) const;
    void pcmWrite(std::uint8_t value);
    struct Pulse {
        std::uint8_t duty=0, volume=0, length=0, step=0, envDivider=0, envDecay=0;
        std::uint16_t period=0, timer=0;
        bool enabled=false, halt=false, constant=false, envStart=false;
        std::uint8_t output() const;
        void quarterFrame();
        void halfFrame();
        void clockTimer();
    };

    std::vector<std::uint8_t> prg_, chr_, ram_;
    std::array<std::uint8_t, 1024> exRam_{};
    std::array<std::uint8_t, 5> prgRegs_{};
    std::array<std::uint16_t, 12> chrRegs_{};
    std::uint8_t prgMode_ = 3, chrMode_ = 0;
    std::uint8_t ramProtect1_ = 0, ramProtect2_ = 0, exRamMode_ = 0;
    std::uint8_t ntMap_ = 0, fillTile_ = 0, fillAttr_ = 0, chrHigh_ = 0;
    std::uint8_t multiplierA_ = 0, multiplierB_ = 0;
    bool chrRam_ = false, lastChrBg_ = false;
    std::uint8_t extendedTile_ = 0;
    // The CPU/PPU observe the same MMC5 IRQ and ExRAM state.
    std::uint8_t irqTarget_=0, irqScanline_=0;
    bool irqEnable_=false, irqPending_=false, inFrame_=false;
    int lastScanline_=-1, lastDot_=-1;
    std::uint8_t splitControl_=0, splitScroll_=0, splitBank_=0;
    bool splitActive_=false;
    unsigned splitColumn_=0;
    int splitLine_=0;
    std::array<Pulse,2> pulses_{};
    std::uint8_t pcmLevel_=0;
    bool pcmReadMode_=false, pcmIrqEnable_=false, pcmIrqPending_=false;
    std::uint32_t audioCycle_=0;
    Mirroring mirroring_ = Mirroring::Horizontal;
};
} // namespace frantastic::nes

#pragma once
#include "frantastic/nes/Mapper.hpp"
#include <array>
#include <cstdint>

namespace frantastic::nes {

// Ricoh 2C02 (NTSC) picture processing unit, stepped one dot at a time.
// 262 scanlines x 341 dots; visible lines 0-239, post-render 240, VBlank
// 241-260, pre-render 261. On odd frames with rendering enabled the
// pre-render line is one dot shorter.
class PPU {
    friend class NesStateCodec;
public:
    static constexpr int Width = 256;
    static constexpr int Height = 240;
    static constexpr int DotsPerScanline = 341;
    static constexpr int ScanlinesPerFrame = 262;

    PPU();
    void connect(PpuCartridgePort* cartridge) { cart_ = cartridge; }
    void powerOn();
    void reset();
    void tick();  // Advance one PPU dot.

    // CPU-visible registers $2000-$2007 (reg = address & 7).
    std::uint8_t cpuRead(unsigned reg);
    void cpuWrite(unsigned reg, std::uint8_t value);
    std::uint8_t peekRegister(unsigned reg) const;
    void oamDmaWrite(std::uint8_t value) { cpuWrite(4, value); }

    // /NMI output: asserted while VBlank flag and PPUCTRL bit 7 are both set.
    bool nmiOutput() const { return vblank_ && (ctrl_ & 0x80u); }

    // PPU address space access without CPU-register side effects.
    std::uint8_t readMemory(std::uint16_t address);
    void writeMemory(std::uint16_t address, std::uint8_t value);

    int scanline() const { return scanline_; }
    int dot() const { return dot_; }
    std::uint64_t frame() const { return frame_; }
    bool oddFrame() const { return oddFrame_; }
    bool renderingEnabled() const { return (mask_ & 0x18u) != 0; }

    std::uint8_t ctrl() const { return ctrl_; }
    std::uint8_t mask() const { return mask_; }
    std::uint8_t status() const;
    std::uint16_t vramAddress() const { return v_; }
    std::uint16_t tempAddress() const { return t_; }
    std::uint8_t fineX() const { return fineX_; }
    bool writeToggle() const { return w_; }
    std::uint8_t oamAddress() const { return oamAddr_; }
    // Diagnostic-only CPU->PPU write routing counters (not emulation state).
    std::uint64_t debugPpuAddrWrites() const { return debugPpuAddrWrites_; }
    std::uint64_t debugPatternSelections() const { return debugPatternSelections_; }
    std::uint64_t debugDataPatternWrites() const { return debugDataPatternWrites_; }
    std::uint64_t debugDataNametableWrites() const { return debugDataNametableWrites_; }
    std::uint64_t debugDataPaletteWrites() const { return debugDataPaletteWrites_; }
    std::uint16_t debugLastDataAddress() const { return debugLastDataAddress_; }
    const std::array<std::uint8_t, 256>& oam() const { return oam_; }

    // Output: 6-bit palette index plus emphasis bits (bits 6-8) per pixel,
    // and the same frame converted to 0xAARRGGBB.
    const std::array<std::uint16_t, Width * Height>& indexBuffer() const { return index_; }
    const std::array<std::uint32_t, Width * Height>& framebuffer() const { return rgba_; }
    static std::uint32_t colorFor(std::uint16_t paletteIndexWithEmphasis);

    // Set when the last visible scanline has been output.
    bool frameReady() const { return frameReady_; }
    void clearFrameReady() { frameReady_ = false; }

private:
    void renderPixel();
    void fetchBackground();
    void loadShifters();
    void incrementX();
    void incrementY();
    void evaluateSprites();
    void fetchSprite(unsigned slot);
    std::uint8_t paletteRead(std::uint16_t address) const;
    std::uint16_t spritePatternAddress(unsigned slot, std::uint8_t tile, unsigned row) const;

    PpuCartridgePort* cart_ = nullptr;
    std::array<std::uint8_t, 0x1000> vram_{};   // CIRAM + optional four-screen RAM.
    std::array<std::uint8_t, 32> palette_{};
    std::array<std::uint8_t, 256> oam_{};
    std::array<std::uint16_t, Width * Height> index_{};
    std::array<std::uint32_t, Width * Height> rgba_{};

    std::uint8_t ctrl_ = 0, mask_ = 0, oamAddr_ = 0;
    bool vblank_ = false, spriteZeroHit_ = false, spriteOverflow_ = false;
    std::uint16_t v_ = 0, t_ = 0;
    std::uint8_t fineX_ = 0;
    bool w_ = false;
    std::uint8_t readBuffer_ = 0, openBus_ = 0;
    std::uint64_t debugPpuAddrWrites_ = 0, debugPatternSelections_ = 0;
    std::uint64_t debugDataPatternWrites_ = 0, debugDataNametableWrites_ = 0, debugDataPaletteWrites_ = 0;
    std::uint16_t debugLastDataAddress_ = 0;
    std::array<std::uint64_t, 8> openBusRefreshFrame_{};
    std::uint8_t decayedOpenBus();
    void refreshOpenBus(std::uint8_t value, std::uint8_t mask);
    bool suppressVblank_ = false;
    bool warmup_ = true;  // Writes to $2000/$2001/$2005/$2006 ignored until first pre-render line.

    int scanline_ = 0, dot_ = 0;
    bool oddFrame_ = false;
    bool oddRendering_ = false;
    std::uint64_t frame_ = 0;
    bool frameReady_ = false;

    // Background pipeline latches and 16-bit shift registers.
    std::uint8_t ntLatch_ = 0, atLatch_ = 0, patLoLatch_ = 0, patHiLatch_ = 0;
    std::uint16_t bgShiftLo_ = 0, bgShiftHi_ = 0, atShiftLo_ = 0, atShiftHi_ = 0;

    // Sprites selected for the line being rendered (fetched during the
    // previous line's dots 257-320) and secondary OAM for the next line.
    struct SpriteUnit { std::uint8_t lo = 0, hi = 0, attr = 0, x = 0; bool zero = false; };
    std::array<SpriteUnit, 8> sprites_{};
    unsigned spriteCount_ = 0;
    std::array<std::uint8_t, 32> secondaryOam_{};
    unsigned secondaryCount_ = 0;
    bool secondaryHasZero_ = false;
};

} // namespace frantastic::nes

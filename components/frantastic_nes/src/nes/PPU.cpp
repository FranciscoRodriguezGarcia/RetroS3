#include "frantastic/nes/PPU.hpp"
#include <cmath>

namespace frantastic::nes {

namespace {
// 2C02 NTSC colours as tabulated on the NESdev "PPU palettes" page (one of
// several published measurements; the exact RGB depends on the TV decoder).
constexpr std::uint32_t kBasePalette[64] = {
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00,
    0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00,
    0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22,
    0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5,
    0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000,
};

// Colour emphasis (PPUMASK bits 5-7) darkens the channels that are *not*
// emphasised. Bit 5 emphasises red on NTSC, bit 6 green, bit 7 blue. The
// attenuation factor is an approximation of the measured ~18% drop.
struct ColorTable {
    std::array<std::uint32_t, 512> rgba{};
    ColorTable() {
        for (unsigned e = 0; e < 8; ++e) {
            for (unsigned c = 0; c < 64; ++c) {
                double r = (kBasePalette[c] >> 16) & 0xFF;
                double g = (kBasePalette[c] >> 8) & 0xFF;
                double b = kBasePalette[c] & 0xFF;
                const double k = 0.816;
                if (e != 0 && (c & 0x0F) < 0x0E) {
                    if (!(e & 1)) r *= k;
                    if (!(e & 2)) g *= k;
                    if (!(e & 4)) b *= k;
                }
                rgba[e * 64 + c] = 0xFF000000u | (static_cast<std::uint32_t>(std::lround(r)) << 16) |
                                   (static_cast<std::uint32_t>(std::lround(g)) << 8) |
                                   static_cast<std::uint32_t>(std::lround(b));
            }
        }
    }
};
const ColorTable& colorTable() {
    static const ColorTable table;
    return table;
}
std::uint8_t reverseBits(std::uint8_t b) {
    b = static_cast<std::uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = static_cast<std::uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
    b = static_cast<std::uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
    return b;
}
} // namespace

std::uint32_t PPU::colorFor(std::uint16_t index) { return colorTable().rgba[index & 0x1FFu]; }

PPU::PPU() { powerOn(); }

void PPU::powerOn() {
    ctrl_ = mask_ = oamAddr_ = 0;
    vblank_ = spriteZeroHit_ = spriteOverflow_ = false;
    v_ = t_ = 0;
    fineX_ = 0;
    w_ = false;
    readBuffer_ = openBus_ = 0;
    scanline_ = 0;
    dot_ = 0;
    oddFrame_ = false;
    frame_ = 0;
    // Compatibility experiment: permit PPU register writes immediately on
    // cold power-on. Physical 2C02 warmup is normally active here; retain
    // the documented warmup behavior in reset() until boot differences are
    // evaluated against games that previously required an in-game reset.
    warmup_ = false;
    suppressVblank_ = false;
    spriteCount_ = secondaryCount_ = 0;
    oam_.fill(0);
    palette_.fill(0);
    index_.fill(0);
    rgba_.fill(colorFor(0));
}

void PPU::reset() {
    // Reset clears PPUCTRL/PPUMASK, the write toggle and the read buffer;
    // VBlank/sprite flags, OAM, VRAM and palette are unaffected.
    ctrl_ = mask_ = 0;
    w_ = false;
    t_ = 0;
    fineX_ = 0;
    readBuffer_ = 0;
    oddFrame_ = false;
    warmup_ = true;
}

std::uint8_t PPU::status() const {
    return static_cast<std::uint8_t>((vblank_ ? 0x80u : 0u) | (spriteZeroHit_ ? 0x40u : 0u) |
                                     (spriteOverflow_ ? 0x20u : 0u));
}

// ---- PPU address space ------------------------------------------------------
std::uint8_t PPU::paletteRead(std::uint16_t address) const {
    unsigned i = address & 0x1Fu;
    // $3F10/$3F14/$3F18/$3F1C mirror the backdrop entries $3F00/04/08/0C.
    if ((i & 0x13u) == 0x10u) i &= 0x0Fu;
    return palette_[i];
}

std::uint8_t PPU::readMemory(std::uint16_t address) {
    address &= 0x3FFFu;
    if (address < 0x2000u) return cart_ ? cart_->ppuRead(address) : 0;
    if (address < 0x3F00u) {
        return cart_ ? cart_->ppuNametableRead(address, vram_.data(), false)
                     : vram_[nametableOffset(Mirroring::Horizontal,address) & 0x0FFFu];
    }
    return paletteRead(address);
}

void PPU::writeMemory(std::uint16_t address, std::uint8_t value) {
    address &= 0x3FFFu;
    if (address < 0x2000u) {
        if (cart_) cart_->ppuWrite(address, value);
        return;
    }
    if (address < 0x3F00u) {
        if (cart_) cart_->ppuNametableWrite(address, value, vram_.data());
        else vram_[nametableOffset(Mirroring::Horizontal,address) & 0x0FFFu] = value;
        return;
    }
    unsigned i = address & 0x1Fu;
    if ((i & 0x13u) == 0x10u) i &= 0x0Fu;
    palette_[i] = value & 0x3Fu;
}

// ---- CPU registers ----------------------------------------------------------
// The PPU data bus latch ("decay register"): each bit read back as 1 decays to
// 0 if not refreshed with a 1 for roughly 600 ms (~36 frames).
std::uint8_t PPU::decayedOpenBus() {
    constexpr std::uint64_t DecayFrames = 36;
    for (unsigned b = 0; b < 8; ++b) {
        if ((openBus_ >> b & 1u) && frame() - openBusRefreshFrame_[b] > DecayFrames)
            openBus_ = static_cast<std::uint8_t>(openBus_ & ~(1u << b));
    }
    return openBus_;
}

void PPU::refreshOpenBus(std::uint8_t value, std::uint8_t mask) {
    decayedOpenBus();
    openBus_ = static_cast<std::uint8_t>((openBus_ & ~mask) | (value & mask));
    for (unsigned b = 0; b < 8; ++b)
        if (mask >> b & 1u) openBusRefreshFrame_[b] = frame();
}

std::uint8_t PPU::peekRegister(unsigned reg) const {
    switch (reg & 7u) {
    case 2: return static_cast<std::uint8_t>(status() | (openBus_ & 0x1Fu));
    case 4: return oam_[oamAddr_];
    default: return openBus_;
    }
}

std::uint8_t PPU::cpuRead(unsigned reg) {
    switch (reg & 7u) {
    case 2: {
        std::uint8_t value = static_cast<std::uint8_t>(status() | (decayedOpenBus() & 0x1Fu));
        // A read on the dot just before the flag is set (dot 1 of line 241 is
        // the next dot to run) returns it clear and suppresses both the flag
        // and the NMI for this frame.
        if (scanline_ == 241 && dot_ == 1) suppressVblank_ = true;
        vblank_ = false;
        w_ = false;
        refreshOpenBus(value, 0xE0u);
        return value;
    }
    case 4: {
        std::uint8_t value = oam_[oamAddr_];
        if ((oamAddr_ & 3u) == 2u) value &= 0xE3u;  // Unimplemented attribute bits.
        refreshOpenBus(value, 0xFFu);
        return value;
    }
    case 7: {
        const std::uint16_t address = v_ & 0x3FFFu;
        std::uint8_t value;
        if (address >= 0x3F00u) {
            // Palette reads are immediate; the buffer receives the nametable
            // byte "underneath" the palette ($2F00-$2FFF mirror).
            value = static_cast<std::uint8_t>((paletteRead(address) & 0x3Fu) | (decayedOpenBus() & 0xC0u));
            if (mask_ & 0x01u) value &= 0x30u | 0xC0u;
            readBuffer_ = readMemory(static_cast<std::uint16_t>(address - 0x1000u));
        } else {
            value = readBuffer_;
            readBuffer_ = readMemory(address);
        }
        const bool rendering = renderingEnabled() && (scanline_ < 240 || scanline_ == 261);
        if (rendering) {
            // During rendering the increment logic is shared with the fetch
            // pipeline: both coarse X and Y increment.
            incrementX();
            incrementY();
        } else {
            v_ = static_cast<std::uint16_t>((v_ + ((ctrl_ & 0x04u) ? 32u : 1u)) & 0x7FFFu);
        }
        refreshOpenBus(value, address >= 0x3F00u ? 0x3Fu : 0xFFu);
        return value;
    }
    default:
        return decayedOpenBus();
    }
}

void PPU::cpuWrite(unsigned reg, std::uint8_t value) {
    refreshOpenBus(value, 0xFFu);
    switch (reg & 7u) {
    case 0:
        if (warmup_) return;
        ctrl_ = value;
        t_ = static_cast<std::uint16_t>((t_ & ~0x0C00u) | ((value & 0x03u) << 10));
        break;
    case 1:
        if (warmup_) return;
        mask_ = value;
        break;
    case 3: oamAddr_ = value; break;
    case 4:
        if (renderingEnabled() && (scanline_ < 240 || scanline_ == 261)) {
            // Writes during rendering are ignored but bump the high six bits.
            oamAddr_ = static_cast<std::uint8_t>(oamAddr_ + 4u);
        } else {
            oam_[oamAddr_++] = value;
        }
        break;
    case 5:
        if (warmup_) return;
        if (!w_) {
            fineX_ = value & 0x07u;
            t_ = static_cast<std::uint16_t>((t_ & ~0x001Fu) | (value >> 3));
        } else {
            t_ = static_cast<std::uint16_t>((t_ & ~0x73E0u) | ((value & 0x07u) << 12) | ((value & 0xF8u) << 2));
        }
        w_ = !w_;
        break;
    case 6:
        if (warmup_) return;
        ++debugPpuAddrWrites_;
        if (!w_) {
            t_ = static_cast<std::uint16_t>((t_ & 0x00FFu) | ((value & 0x3Fu) << 8));
        } else {
            t_ = static_cast<std::uint16_t>((t_ & 0x7F00u) | value);
            v_ = t_;
            if ((v_ & 0x3FFFu) < 0x2000u) ++debugPatternSelections_;
        }
        w_ = !w_;
        break;
    case 7: {
        const auto address = static_cast<std::uint16_t>(v_ & 0x3FFFu);
        debugLastDataAddress_ = address;
        if (address < 0x2000u) ++debugDataPatternWrites_;
        else if (address < 0x3F00u) ++debugDataNametableWrites_;
        else ++debugDataPaletteWrites_;
        writeMemory(v_, value);
        const bool rendering = renderingEnabled() && (scanline_ < 240 || scanline_ == 261);
        if (rendering) {
            incrementX();
            incrementY();
        } else {
            v_ = static_cast<std::uint16_t>((v_ + ((ctrl_ & 0x04u) ? 32u : 1u)) & 0x7FFFu);
        }
        break;
    }
    default: break;  // $2002 is read-only.
    }
}

// ---- Loopy scroll register arithmetic ---------------------------------------
// v/t layout: 0yyy NNYY YYYX XXXX (fine Y, nametable, coarse Y, coarse X).
void PPU::incrementX() {
    if ((v_ & 0x001Fu) == 31u) {
        v_ = static_cast<std::uint16_t>((v_ & ~0x001Fu) ^ 0x0400u);
    } else {
        ++v_;
    }
}

void PPU::incrementY() {
    if ((v_ & 0x7000u) != 0x7000u) {
        v_ = static_cast<std::uint16_t>(v_ + 0x1000u);
        return;
    }
    v_ &= static_cast<std::uint16_t>(~0x7000u);
    unsigned coarseY = (v_ & 0x03E0u) >> 5;
    if (coarseY == 29u) {
        coarseY = 0;
        v_ ^= 0x0800u;           // Switch vertical nametable.
    } else if (coarseY == 31u) {
        coarseY = 0;             // Attribute rows 30/31 wrap without switching.
    } else {
        ++coarseY;
    }
    v_ = static_cast<std::uint16_t>((v_ & ~0x03E0u) | (coarseY << 5));
}

// ---- Background pipeline ------------------------------------------------------
void PPU::loadShifters() {
    bgShiftLo_ = static_cast<std::uint16_t>((bgShiftLo_ & 0xFF00u) | patLoLatch_);
    bgShiftHi_ = static_cast<std::uint16_t>((bgShiftHi_ & 0xFF00u) | patHiLatch_);
    // The 2-bit attribute is expanded to a full byte so it shifts in lockstep.
    atShiftLo_ = static_cast<std::uint16_t>((atShiftLo_ & 0xFF00u) | ((atLatch_ & 1u) ? 0xFFu : 0x00u));
    atShiftHi_ = static_cast<std::uint16_t>((atShiftHi_ & 0xFF00u) | ((atLatch_ & 2u) ? 0xFFu : 0x00u));
}

void PPU::fetchBackground() {
    switch (dot_ & 7) {
    case 1:
        {
            const auto address = static_cast<std::uint16_t>(0x2000u | (v_ & 0x0FFFu));
            if (cart_) cart_->ppuBackgroundTile(dot_ <= 256 ? static_cast<unsigned>((dot_-1)/8) :
                                               static_cast<unsigned>(32+(dot_-321)/8), scanline_);
            ntLatch_ = cart_ ? cart_->ppuNametableRead(address, vram_.data(), true)
                             : readMemory(address);
        }
        break;
    case 3: {
        const auto address = static_cast<std::uint16_t>(0x23C0u | (v_ & 0x0C00u) | ((v_ >> 4) & 0x38u) |
                                                        ((v_ >> 2) & 0x07u));
        std::uint8_t attribute = cart_ ? cart_->ppuNametableRead(address, vram_.data(), true)
                                       : readMemory(address);
        // Each attribute byte covers 4x4 tiles; select the 2x2 quadrant.
        if (v_ & 0x0040u) attribute >>= 4;  // Bottom half (coarse Y bit 1).
        if (v_ & 0x0002u) attribute >>= 2;  // Right half (coarse X bit 1).
        atLatch_ = attribute & 0x03u;
        break;
    }
    case 5:
        {
            const auto address = static_cast<std::uint16_t>(((ctrl_ & 0x10u) ? 0x1000u : 0u) +
                                                             ntLatch_ * 16u + ((v_ >> 12) & 7u));
            patLoLatch_ = cart_ ? cart_->ppuReadBackground(address, (ctrl_ & 0x20u) != 0)
                               : readMemory(address);
        }
        break;
    case 7:
        {
            const auto address = static_cast<std::uint16_t>(((ctrl_ & 0x10u) ? 0x1000u : 0u) +
                                                             ntLatch_ * 16u + ((v_ >> 12) & 7u) + 8u);
            patHiLatch_ = cart_ ? cart_->ppuReadBackground(address, (ctrl_ & 0x20u) != 0)
                               : readMemory(address);
        }
        break;
    case 0:
        incrementX();
        break;
    default:
        break;
    }
}

// ---- Sprites ------------------------------------------------------------------
// Sprite evaluation for the *next* scanline, done in one step at dot 257 with
// the documented results: up to eight sprites in OAM order, and the overflow
// flag computed with the hardware's diagonal OAM scan bug once eight sprites
// have been found.
void PPU::evaluateSprites() {
    const unsigned height = (ctrl_ & 0x20u) ? 16u : 8u;
    secondaryOam_.fill(0xFF);
    secondaryCount_ = 0;
    secondaryHasZero_ = false;
    const int line = scanline_;
    unsigned n = 0;
    for (; n < 64; ++n) {
        const int row = line - oam_[n * 4];
        if (row < 0 || row >= static_cast<int>(height)) continue;
        if (secondaryCount_ == 8) break;
        for (unsigned b = 0; b < 4; ++b) secondaryOam_[secondaryCount_ * 4 + b] = oam_[n * 4 + b];
        if (n == 0) secondaryHasZero_ = true;
        ++secondaryCount_;
    }
    if (secondaryCount_ == 8 && n < 64) {
        unsigned m = 0;
        while (n < 64) {
            const int row = line - oam_[(n * 4 + m) & 0xFFu];
            if (row >= 0 && row < static_cast<int>(height)) {
                spriteOverflow_ = true;
                break;
            }
            ++n;
            m = (m + 1) & 3u;  // Hardware bug: m increments alongside n.
        }
    }
}

std::uint16_t PPU::spritePatternAddress(unsigned, std::uint8_t tile, unsigned row) const {
    if (ctrl_ & 0x20u) {
        // 8x16: bit 0 of the tile index selects the pattern table.
        const unsigned table = (tile & 1u) ? 0x1000u : 0u;
        unsigned index = tile & 0xFEu;
        if (row >= 8) { ++index; row -= 8; }
        return static_cast<std::uint16_t>(table + index * 16u + row);
    }
    return static_cast<std::uint16_t>(((ctrl_ & 0x08u) ? 0x1000u : 0u) + tile * 16u + row);
}

void PPU::fetchSprite(unsigned slot) {
    const unsigned height = (ctrl_ & 0x20u) ? 16u : 8u;
    SpriteUnit& unit = sprites_[slot];
    if (slot < secondaryCount_) {
        const std::uint8_t y = secondaryOam_[slot * 4];
        const std::uint8_t tile = secondaryOam_[slot * 4 + 1];
        unit.attr = secondaryOam_[slot * 4 + 2];
        unit.x = secondaryOam_[slot * 4 + 3];
        unit.zero = secondaryHasZero_ && slot == 0;
        unsigned row = static_cast<unsigned>(scanline_ - y) & (height - 1u);
        if (unit.attr & 0x80u) row = height - 1u - row;  // Vertical flip.
        const std::uint16_t address = spritePatternAddress(slot, tile, row);
        unit.lo = cart_ ? cart_->ppuReadSprite(address, (ctrl_ & 0x20u) != 0) : readMemory(address);
        unit.hi = cart_ ? cart_->ppuReadSprite(static_cast<std::uint16_t>(address + 8u), (ctrl_ & 0x20u) != 0)
                        : readMemory(static_cast<std::uint16_t>(address + 8u));
        if (unit.attr & 0x40u) {  // Horizontal flip.
            unit.lo = reverseBits(unit.lo);
            unit.hi = reverseBits(unit.hi);
        }
    } else {
        // Empty slots still fetch tile $FF so mappers see the same A12 pattern.
        const std::uint16_t address = spritePatternAddress(slot, 0xFF, 0);
        if (cart_) {
            cart_->ppuReadSprite(address, (ctrl_ & 0x20u) != 0);
            cart_->ppuReadSprite(static_cast<std::uint16_t>(address + 8u), (ctrl_ & 0x20u) != 0);
        } else {
            readMemory(address);
            readMemory(static_cast<std::uint16_t>(address + 8u));
        }
        unit = SpriteUnit{};
    }
}

// ---- Pixel output -------------------------------------------------------------
void PPU::renderPixel() {
    const int x = dot_ - 1;
    unsigned bgPixel = 0, bgPalette = 0;
    if ((mask_ & 0x08u) && (x >= 8 || (mask_ & 0x02u))) {
        const unsigned bit = 15u - fineX_;
        bgPixel = ((bgShiftLo_ >> bit) & 1u) | (((bgShiftHi_ >> bit) & 1u) << 1);
        bgPalette = ((atShiftLo_ >> bit) & 1u) | (((atShiftHi_ >> bit) & 1u) << 1);
    }
    unsigned spPixel = 0, spPalette = 0;
    bool spBehind = false, spZero = false;
    if ((mask_ & 0x10u) && (x >= 8 || (mask_ & 0x04u))) {
        for (unsigned i = 0; i < spriteCount_; ++i) {
            const int column = x - sprites_[i].x;
            if (column < 0 || column > 7) continue;
            const unsigned shift = 7u - static_cast<unsigned>(column);
            const unsigned pixel = ((sprites_[i].lo >> shift) & 1u) | (((sprites_[i].hi >> shift) & 1u) << 1);
            if (pixel == 0) continue;
            // First opaque sprite in OAM order wins, regardless of priority.
            spPixel = pixel;
            spPalette = (sprites_[i].attr & 3u) + 4u;
            spBehind = (sprites_[i].attr & 0x20u) != 0;
            spZero = sprites_[i].zero;
            break;
        }
    }
    if (spZero && bgPixel && spPixel && x != 255 && (mask_ & 0x18u) == 0x18u) spriteZeroHit_ = true;

    std::uint16_t address;
    if (!bgPixel && !spPixel) {
        address = 0x3F00;
        // With rendering disabled and v pointing into palette RAM, the PPU
        // outputs that palette entry instead of the backdrop.
        if (!renderingEnabled() && (v_ & 0x3F00u) == 0x3F00u) address = v_ & 0x3F1Fu;
    } else if (!spPixel || (bgPixel && spBehind)) {
        address = static_cast<std::uint16_t>(0x3F00u + bgPalette * 4u + bgPixel);
    } else {
        address = static_cast<std::uint16_t>(0x3F00u + spPalette * 4u + spPixel);
    }
    std::uint8_t color = paletteRead(address) & 0x3Fu;
    if (mask_ & 0x01u) color &= 0x30u;  // Greyscale.
    const auto entry = static_cast<std::uint16_t>(color | ((mask_ >> 5) << 6));
    const int index = scanline_ * Width + x;
    index_[index] = entry;
    rgba_[index] = colorFor(entry);
}

// ---- Dot scheduler --------------------------------------------------------------
void PPU::tick() {
    // The MMC3 sees PPU A12 on every dot, including the dummy sprite slots.
    // Sprite reads currently execute together in fetchSprite(), so report
    // their actual address-bus phases independently of the data fetches.
    if (cart_) {
        std::uint16_t busAddress = 0x2000u;
        const bool activeLine = scanline_ < 240 || scanline_ == 261;
        if (activeLine && renderingEnabled()) {
            if ((dot_ >= 1 && dot_ <= 256) || (dot_ >= 321 && dot_ <= 336)) {
                const unsigned phase = dot_ & 7u;
                if (phase >= 5u) {
                    busAddress = static_cast<std::uint16_t>(
                        ((ctrl_ & 0x10u) ? 0x1000u : 0u) +
                        ntLatch_ * 16u + ((v_ >> 12) & 7u) + (phase >= 7u ? 8u : 0u));
                }
            } else if (dot_ >= 257 && dot_ <= 320) {
                const unsigned slot = static_cast<unsigned>(dot_ - 257) / 8u;
                const unsigned phase = static_cast<unsigned>(dot_ - 257) & 7u;
                if (phase >= 4u) {
                    const std::uint8_t tile = slot < secondaryCount_ ?
                        secondaryOam_[slot * 4 + 1] : 0xFFu;
                    const unsigned height = (ctrl_ & 0x20u) ? 16u : 8u;
                    unsigned row = slot < secondaryCount_ ?
                        static_cast<unsigned>(scanline_ - secondaryOam_[slot * 4]) & (height - 1u) : 0u;
                    if (slot < secondaryCount_ && (secondaryOam_[slot * 4 + 2] & 0x80u))
                        row = height - 1u - row;
                    busAddress = static_cast<std::uint16_t>(
                        spritePatternAddress(slot, tile, row) + (phase >= 6u ? 8u : 0u));
                }
            }
        }
        const std::uint64_t absoluteDot =
            frame_ * static_cast<std::uint64_t>(ScanlinesPerFrame * DotsPerScanline) +
            static_cast<std::uint64_t>(scanline_ * DotsPerScanline + dot_);
        cart_->ppuAddressTick(busAddress, absoluteDot);
        cart_->ppuRenderTick(scanline_, dot_, renderingEnabled());
    }
    const bool visibleLine = scanline_ < 240;
    const bool preRender = scanline_ == 261;
    const bool rendering = renderingEnabled();

    if (visibleLine || preRender) {
        if (rendering) {
            const bool fetchDot = (dot_ >= 1 && dot_ <= 256) || (dot_ >= 321 && dot_ <= 336);
            if ((dot_ >= 2 && dot_ <= 257) || (dot_ >= 322 && dot_ <= 337)) {
                bgShiftLo_ <<= 1; bgShiftHi_ <<= 1; atShiftLo_ <<= 1; atShiftHi_ <<= 1;
                if ((dot_ & 7) == 1 && (dot_ >= 9)) loadShifters();
            }
            if (visibleLine && dot_ >= 1 && dot_ <= 256) renderPixel();
            if (fetchDot) fetchBackground();
            if (dot_ == 256) incrementY();
            if (dot_ == 257) {
                v_ = static_cast<std::uint16_t>((v_ & ~0x041Fu) | (t_ & 0x041Fu));
                if (visibleLine) evaluateSprites();
                else secondaryCount_ = 0;  // No sprites are drawn on line 0.
            }
            if (dot_ >= 257 && dot_ <= 320) {
                oamAddr_ = 0;
                if (((dot_ - 257) & 7) == 4) fetchSprite(static_cast<unsigned>(dot_ - 257) / 8u);
                if (dot_ == 320) spriteCount_ = secondaryCount_;
            }
            if (dot_ == 337 || dot_ == 339) readMemory(static_cast<std::uint16_t>(0x2000u | (v_ & 0x0FFFu)));
            if (preRender && dot_ >= 280 && dot_ <= 304)
                v_ = static_cast<std::uint16_t>((v_ & ~0x7BE0u) | (t_ & 0x7BE0u));
        } else if (visibleLine && dot_ >= 1 && dot_ <= 256) {
            renderPixel();
        }
    }

    if (scanline_ == 239 && dot_ == 256) frameReady_ = true;

    // The skip decision samples the rendering-enable state at dot 338.
    if (preRender && dot_ == 338) oddRendering_ = renderingEnabled();
    if (scanline_ == 241 && dot_ == 1) {
        if (!suppressVblank_) vblank_ = true;
        suppressVblank_ = false;
        ++frame_;
    }
    if (preRender && dot_ == 1) {
        vblank_ = spriteZeroHit_ = spriteOverflow_ = false;
        warmup_ = false;
    }

    // Advance the beam. The odd-frame skip drops dot 340 of the pre-render
    // line when background or sprite rendering is enabled.
    if (preRender && dot_ == 339 && oddFrame_ && oddRendering_) {
        dot_ = 340;
    }
    if (++dot_ > 340) {
        dot_ = 0;
        if (++scanline_ > 261) {
            scanline_ = 0;
            oddFrame_ = !oddFrame_;
            if (!rendering) spriteCount_ = 0;
        }
    }
}

} // namespace frantastic::nes

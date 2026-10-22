#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace frantastic::nes {

enum class Mirroring { Horizontal, Vertical, SingleScreenLow, SingleScreenHigh, FourScreen };

const char* mirroringName(Mirroring mirroring);

// Physical nametable addressing. The 2C02 only owns 2 KiB of nametable RAM
// (CIRAM); the cartridge decides which 1 KiB page answers each of the four
// logical nametables by driving CIRAM A10. Four-screen boards add 2 KiB of
// cartridge VRAM, modelled here as offsets $0800-$0FFF of a 4 KiB VRAM array.
std::uint16_t nametableOffset(Mirroring mirroring, std::uint16_t address);

// Cartridge-side view of the PPU bus. The PPU talks to the cartridge only
// through this interface, so mappers can observe every pattern fetch (needed
// later for A12-clocked IRQ counters such as MMC3).
class PpuCartridgePort {
public:
    virtual ~PpuCartridgePort() = default;
    // Pattern-table space $0000-$1FFF.
    virtual std::uint8_t ppuRead(std::uint16_t address) = 0;
    virtual void ppuWrite(std::uint16_t address, std::uint8_t value) = 0;
    // Rendering supplies context for mappers with independent sprite/background
    // pattern banks. Regular CPU $2007 accesses retain ppuRead semantics.
    virtual std::uint8_t ppuReadBackground(std::uint16_t address, bool sprites16) {
        (void)sprites16;
        return ppuRead(address);
    }
    virtual std::uint8_t ppuReadSprite(std::uint16_t address, bool sprites16) {
        (void)sprites16;
        return ppuRead(address);
    }
    // Cartridge-controlled CIRAM, ExRAM and fill-mode nametable sources.
    // Implementations other than MMC5 use the existing mirroring mapping.
    virtual std::uint8_t ppuNametableRead(std::uint16_t address, std::uint8_t* vram,
                                          bool backgroundFetch) {
        (void)backgroundFetch;
        return vram[nametableAddress(address) & 0x0FFFu];
    }
    virtual void ppuNametableWrite(std::uint16_t address, std::uint8_t value,
                                    std::uint8_t* vram) {
        vram[nametableAddress(address) & 0x0FFFu] = value;
    }
    // The cartridge observes the external PPU address bus once per dot.
    // Used by A12-driven MMC3 IRQ timing; distinct from data accesses.
    virtual void ppuAddressTick(std::uint16_t, std::uint64_t) {}
    // Rendering phase and on-screen tile position (not used for CPU $2007).
    virtual void ppuRenderTick(int, int, bool) {}
    virtual void ppuBackgroundTile(unsigned, int) {}
    // Maps $2000-$3EFF to an offset into the 4 KiB PPU VRAM array.
    virtual std::uint16_t nametableAddress(std::uint16_t address) const = 0;
};

// Base class for every cartridge board. CPU accesses cover $4020-$FFFF.
class Mapper : public PpuCartridgePort {
public:
    ~Mapper() override = default;
    virtual unsigned id() const = 0;
    virtual const char* name() const = 0;
    // `openBus` is the last value seen on the CPU data bus; unmapped cartridge
    // addresses return it, as the real data bus floats.
    virtual std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) = 0;
    // Side-effect-free read used by debuggers and trace output.
    virtual std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const = 0;
    virtual void cpuWrite(std::uint16_t address, std::uint8_t value) = 0;
    // CPU bus supplies the exact write cycle for boards with cycle-sensitive ports.
    virtual void cpuWriteTimed(std::uint16_t address, std::uint8_t value,
                               std::uint64_t cpuCycle) {
        (void)cpuCycle;
        cpuWrite(address, value);
    }
    virtual void reset() {}
    virtual bool irqAsserted() const { return false; }
    virtual void tickCpu() {}
    virtual float expansionAudio() const { return 0.0f; }
    virtual Mirroring mirroring() const = 0;
    std::uint16_t nametableAddress(std::uint16_t address) const override {
        return nametableOffset(mirroring(), address);
    }
};

class UnsupportedMapperError : public std::runtime_error {
public:
    UnsupportedMapperError(unsigned mapper, const std::string& message)
        : std::runtime_error(message), mapper_(mapper) {}
    unsigned mapper() const { return mapper_; }
private:
    unsigned mapper_;
};

} // namespace frantastic::nes

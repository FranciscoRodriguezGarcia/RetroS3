#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Nintendo MMC2 / PxROM (mapper 9) and MMC4 / FxROM (mapper 10).
// Both boards switch 4 KiB CHR ROM banks through PPU tile-FD/FE latches.
// The triggering PPU read uses the previous bank; the latch changes afterward.
class MapperMmcLatch final : public Mapper {
    friend class NesStateCodec;
public:
    explicit MapperMmcLatch(const RomImage& image);
    unsigned id() const override { return mmc4_ ? 10u : 9u; }
    const char* name() const override { return mmc4_ ? "MMC4" : "MMC2"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t, std::uint8_t) override {}
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override;
private:
    std::vector<std::uint8_t> prg_, chr_;
    std::array<std::uint8_t, 0x2000> prgRam_{};
    std::array<std::uint8_t, 4> chrBanks_{}; // FD0, FE0, FD1, FE1
    std::array<bool, 2> latchFe_{}; // false => FD; true => FE
    bool mmc4_ = false;
    std::uint8_t prgBank_ = 0;
    Mirroring mirroring_ = Mirroring::Horizontal;
};

} // namespace frantastic::nes

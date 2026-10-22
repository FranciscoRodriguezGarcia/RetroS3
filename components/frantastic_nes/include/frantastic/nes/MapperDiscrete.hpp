#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Nintendo AxROM: switchable 32 KiB PRG, 8 KiB CHR RAM and software-selectable
// single-screen CIRAM. NES 2.0 submapper 2 explicitly enables bus conflicts.
class Mapper007 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper007(const RomImage& image);
    unsigned id() const override { return 7; }
    const char* name() const override { return "AxROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override { return chr_[address & 0x1FFFu]; }
    void ppuWrite(std::uint16_t address, std::uint8_t value) override { chr_[address & 0x1FFFu] = value; }
    Mirroring mirroring() const override { return singleHigh_ ? Mirroring::SingleScreenHigh : Mirroring::SingleScreenLow; }
    void reset() override { bank_ = 0; singleHigh_ = false; }
private:
    std::vector<std::uint8_t> prg_, chr_;
    std::uint8_t bank_ = 0;
    bool singleHigh_ = false, busConflicts_ = false;
};

// GNROM / MHROM: 32 KiB PRG and 8 KiB CHR ROM bank selects in one register,
// fixed header mirroring, and AND-type bus conflicts.
class Mapper066 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper066(const RomImage& image);
    unsigned id() const override { return 66; }
    const char* name() const override { return "GNROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override { prgBank_ = chrBank_ = 0; }
private:
    std::vector<std::uint8_t> prg_, chr_;
    bool chrRam_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::uint8_t prgBank_ = 0, chrBank_ = 0;
};

// iNES mapper 34 is shared by unrelated boards:
// BNROM: 32 KiB PRG switching at $8000-$FFFF (bus conflicts), fixed CHR.
// NINA-001/002: PRG $7FFD, two 4 KiB CHR registers $7FFE/$7FFF,
// each overlaid on 8 KiB PRG RAM. NES 2.0 submappers disambiguate.
class Mapper034 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper034(const RomImage& image);
    unsigned id() const override { return 34; }
    const char* name() const override { return nina_ ? "NINA-001" : "BNROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override { prgBank_ = chrBank0_ = chrBank1_ = 0; }
    bool nina() const { return nina_; }
private:
    std::vector<std::uint8_t> prg_, chr_;
    std::array<std::uint8_t, 0x2000> prgRam_{};
    bool nina_ = false, chrRam_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::uint8_t prgBank_ = 0, chrBank0_ = 0, chrBank1_ = 0;
};

} // namespace frantastic::nes

#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {

// MMC1 (SxROM, iNES mapper 1): serial 5-bit load register, switchable
// 16/32 KiB PRG banking, 4/8 KiB CHR banking, software mirroring and 8 KiB
// PRG RAM. 512 KiB SUROM boards use CHR bank bit 4 as the PRG outer bank.
// Consecutive-cycle serial writes are ignored, as on the MMC1 ASIC.
class Mapper001 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper001(const RomImage& image);
    unsigned id() const override { return 1; }
    const char* name() const override { return "MMC1"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    void cpuWriteTimed(std::uint16_t address, std::uint8_t value,
                       std::uint64_t cpuCycle) override;
    std::uint8_t ppuRead(std::uint16_t address) override { ++debugChrReads_; return chr_[chrOffset(address)]; }
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override;
    // Read-only diagnostics; no mapper side effects.
    std::uint8_t debugControl() const { return control_; }
    std::uint8_t debugPrgBank() const { return prgBank_; }
    std::uint8_t debugChr0() const { return chr0_; }
    std::uint8_t debugChr1() const { return chr1_; }
    std::uint8_t debugShift() const { return shift_; }
    std::uint64_t debugChrReads() const { return debugChrReads_; }
    std::uint64_t debugChrWrites() const { return debugChrWrites_; }
    std::size_t debugChrNonzero() const;
    void reset() override;

private:
    std::size_t prgOffset(std::uint16_t address) const;
    std::size_t chrOffset(std::uint16_t address) const;

    std::vector<std::uint8_t> prg_, chr_;
    std::array<std::uint8_t, 0x2000> prgRam_{};
    bool chrRam_ = false;
    std::uint8_t shift_ = 0x10, control_ = 0x0C, chr0_ = 0, chr1_ = 0, prgBank_ = 0;
    std::uint64_t lastWriteCycle_ = UINT64_MAX;
    // Diagnostic counters only; not part of emulated hardware/save-state.
    std::uint64_t debugChrReads_ = 0, debugChrWrites_ = 0;
};

// UxROM (iNES mapper 2): 16 KiB switchable bank at $8000, last bank fixed at
// $C000, CHR RAM, fixed mirroring.
class Mapper002 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper002(const RomImage& image);
    unsigned id() const override { return 2; }
    const char* name() const override { return "UxROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override { return chr_[address & 0x1FFFu]; }
    void ppuWrite(std::uint16_t address, std::uint8_t value) override { if (chrRam_) chr_[address & 0x1FFFu] = value; }
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override { bank_ = 0; }

private:
    std::vector<std::uint8_t> prg_, chr_;
    bool chrRam_ = false;
    Mirroring mirroring_;
    std::uint8_t bank_ = 0;
};

// CNROM (iNES mapper 3): fixed 16/32 KiB PRG, 8 KiB switchable CHR ROM.
class Mapper003 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper003(const RomImage& image);
    unsigned id() const override { return 3; }
    const char* name() const override { return "CNROM"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override { return cpuPeek(address, openBus); }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    void reset() override { bank_ = 0; }

private:
    std::vector<std::uint8_t> prg_, chr_;
    bool chrRam_ = false;
    Mirroring mirroring_;
    std::uint8_t bank_ = 0;
};

} // namespace frantastic::nes

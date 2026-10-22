#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <cstdint>
#include <vector>

namespace frantastic::nes {

// Nintendo MMC3 (TxROM, mapper 4). IRQ behavior defaults to the
// MMC3B/C variant; NES 2.0 submapper 3 selects the earlier MMC3A/NEC IRQ.
class Mapper004 final : public Mapper {
    friend class NesStateCodec;
public:
    explicit Mapper004(const RomImage& image);
    unsigned id() const override { return 4; }
    const char* name() const override { return "MMC3"; }
    std::uint8_t cpuRead(std::uint16_t address, std::uint8_t openBus) override {
        return cpuPeek(address, openBus);
    }
    std::uint8_t cpuPeek(std::uint16_t address, std::uint8_t openBus) const override;
    void cpuWrite(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t ppuRead(std::uint16_t address) override;
    void ppuWrite(std::uint16_t address, std::uint8_t value) override;
    Mirroring mirroring() const override { return mirroring_; }
    bool irqAsserted() const override { return irqPending_; }
    void ppuAddressTick(std::uint16_t address, std::uint64_t dot) override;
    void reset() override;
    std::uint8_t debugIrqCounter() const { return irqCounter_; }
    std::uint8_t debugBank(unsigned index) const { return banks_[index & 7u]; }

private:
    std::size_t prgOffset(std::uint16_t address) const;
    std::size_t chrOffset(std::uint16_t address) const;
    void clockIrq();
    std::vector<std::uint8_t> prg_, chr_, prgRam_;
    bool chrRam_ = false, fourScreen_ = false, earlyIrq_ = false;
    Mirroring mirroring_ = Mirroring::Horizontal;
    std::array<std::uint8_t, 8> banks_{};
    std::uint8_t bankSelect_ = 0, ramProtect_ = 0x80;
    std::uint8_t irqLatch_ = 0, irqCounter_ = 0;
    bool irqReload_ = false, irqEnabled_ = false, irqPending_ = false;
    bool lastA12_ = false;
    std::uint64_t lastLowDot_ = 0;
};

} // namespace frantastic::nes

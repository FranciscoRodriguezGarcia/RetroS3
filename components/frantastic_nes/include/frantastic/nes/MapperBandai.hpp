#pragma once
#include "frantastic/nes/Cartridge.hpp"
#include <array>
#include <vector>

namespace frantastic::nes {

// Bandai FCG-1/2 and LZ93D50. Mapper 16, 153 and 159.
// Register decoding and IRQ behaviour depend on the actual board revision.
class MapperBandai final : public Mapper {
    friend class NesStateCodec;
public:
    explicit MapperBandai(const RomImage& image);
    unsigned id() const override { return id_; }
    const char* name() const override { return id_==153?"Bandai LZ93D50 (WRAM)":id_==159?"Bandai LZ93D50 (X24C01)":"Bandai FCG/LZ93D50"; }
    std::uint8_t cpuRead(std::uint16_t a,std::uint8_t bus) override { return cpuPeek(a,bus); }
    std::uint8_t cpuPeek(std::uint16_t a,std::uint8_t bus) const override;
    void cpuWrite(std::uint16_t a,std::uint8_t v) override;
    std::uint8_t ppuRead(std::uint16_t a) override;
    void ppuWrite(std::uint16_t a,std::uint8_t v) override;
    Mirroring mirroring() const override { return mirroring_; }
    void tickCpu() override;
    bool irqAsserted() const override { return irqPending_; }
    void reset() override;
private:
    unsigned id_,submapper_;
    std::vector<std::uint8_t> prg_,chr_,wram_;
    bool chrRam_=false,hasWram_=false,fcg_=false,fourScreen_=false;
    Mirroring mirroring_;
    std::array<std::uint8_t,8> chrBank_{};
    std::uint8_t prgBank_=0,outerBank_=0;
    std::uint16_t irqLatch_=0,irqCounter_=0;
    bool irqEnabled_=false,irqPending_=false,ramEnabled_=false;
    // Open-drain serial bus interface to onboard EEPROM.
    std::array<std::uint8_t,256> eeprom_{};
    bool hasEeprom_=false,x24c01_=false,scl_=false,sda_=true,readDirection_=false;
    bool driveLow_=false,ack_=false,reading_=false,expectAck_=false;
    unsigned bits_=0,stage_=0;
    std::uint8_t received_=0,shift_=0,address_=0;
    void eepromWriteLines(std::uint8_t value);
    void eepromByte(std::uint8_t value);
    bool inRegisters(std::uint16_t address) const;
};
} // namespace frantastic::nes

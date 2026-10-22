#include "frantastic/nes/MapperVrc24.hpp"
#include <algorithm>

namespace frantastic::nes {

MapperVrc24::MapperVrc24(const RomImage& image)
    : mapperId_(image.mapper), submapper_(image.submapper),
      nes20_(image.format==RomFormat::Nes20),
      vrc2_(image.mapper==22 || (image.format==RomFormat::Nes20 && image.submapper==3)),
      chrRam_(image.chr.empty()), fourScreen_(image.mirroring==Mirroring::FourScreen),
      prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
      ram_(image.prgRamSize ? image.prgRamSize : 0x2000u,0), mirroring_(image.mirroring) {
    if (prg_.size()<0x8000u || (prg_.size()%0x2000u) || (chr_.size()%0x400u))
        throw RomFormatError("NES: VRC2/VRC4 needs 8 KiB PRG and 1 KiB CHR alignment");
    reset();
}
void MapperVrc24::reset() {
    chrBanks_.fill(0); prg0_=prg1_=prgMode_=latch_=0;
    ramEnabled_=0; irqLatch_=irqCounter_=irqControl_=0; irqPrescaler_=341;
    irqEnabled_=irqPending_=false;
}
unsigned MapperVrc24::registerIndex(std::uint16_t address) const {
    const auto decode=[address](unsigned a0,unsigned a1) {
        return ((address>>a0)&1u)|(((address>>a1)&1u)<<1);
    };
    if (mapperId_==22) return decode(1,0);
    // Each legacy mapper combines two non-overlapping pin layouts.
    if (mapperId_==21) {
        if (nes20_ && submapper_==1) return decode(1,2);
        if (nes20_ && submapper_==2) return decode(6,7);
        return (address&0x06u) ? decode(1,2) : decode(6,7);
    }
    if (mapperId_==23) {
        if (nes20_ && submapper_==2) return decode(2,3);
        if (nes20_ && (submapper_==1 || submapper_==3)) return decode(0,1);
        return (address&0x0Cu) ? decode(2,3) : decode(0,1);
    }
    if (mapperId_==25) {
        if (nes20_ && submapper_==2) return decode(3,2);
        if (nes20_ && (submapper_==1 || submapper_==3)) return decode(1,0);
        return (address&0x0Cu) ? decode(3,2) : decode(1,0);
    }
    return 0;
}
std::uint8_t MapperVrc24::cpuPeek(std::uint16_t address,std::uint8_t openBus) const {
    if (address>=0x8000u) {
        const std::size_t count=prg_.size()/0x2000u;
        const unsigned slot=(address-0x8000u)>>13;
        std::size_t bank;
        if (slot==3) bank=count-1;
        else if (slot==1) bank=prg1_;
        else if (vrc2_) bank=slot==0?prg0_:count-2;
        else if ((slot==0 && prgMode_) || (slot==2 && !prgMode_)) bank=count-2;
        else bank=prg0_;
        return prg_[(bank%count)*0x2000u+(address&0x1FFFu)];
    }
    if (address>=0x6000u) {
        if (vrc2_) {
            // Some VRC2 boards have external WRAM; otherwise a one-bit
            // write/readback latch is connected to the EEPROM-data pad.
            if (mapperId_==25 && !ram_.empty())
                return ram_[(address-0x6000u)%ram_.size()];
            if (address<0x7000u) return static_cast<std::uint8_t>((openBus&0xFEu)|latch_);
            return openBus;
        }
        if ((ramEnabled_ || (mapperId_==25 && !nes20_)) && !ram_.empty())
            return ram_[(address-0x6000u)%ram_.size()];
        // Legacy mapper 23/25 headers may actually contain VRC2 boards.
        // Retain the EEPROM-data latch fallback when RAM is not selected.
        if (!nes20_ && address<0x7000u)
            return static_cast<std::uint8_t>((openBus&0xFEu)|latch_);
    }
    return openBus;
}
void MapperVrc24::cpuWrite(std::uint16_t address,std::uint8_t value) {
    if (address>=0x6000u && address<0x8000u) {
        if (vrc2_) {
            if (mapperId_==25 && !ram_.empty()) ram_[(address-0x6000u)%ram_.size()]=value;
            if (address<0x7000u) latch_=value&1u;
        } else {
            if ((ramEnabled_ || (mapperId_==25 && !nes20_)) && !ram_.empty())
                ram_[(address-0x6000u)%ram_.size()]=value;
            if (address<0x7000u) latch_=value&1u;
        }
        return;
    }
    if (address<0x8000u) return;
    const unsigned area=address&0xF000u;
    const unsigned reg=registerIndex(address);
    if (area==0x8000u) prg0_=value&0x1Fu;
    else if (area==0xA000u) prg1_=value&0x1Fu;
    else if (area==0x9000u) {
        if (!vrc2_ && reg==2) {
            ramEnabled_=value&1u;
            prgMode_=(value>>1)&1u;
        } else if (vrc2_ || reg==0) {
            if (!fourScreen_)
                mirroring_ = (value&1u)?Mirroring::Horizontal:Mirroring::Vertical;
            if (!vrc2_ && !fourScreen_ && (value&2u))
                mirroring_=(value&1u)?Mirroring::SingleScreenHigh:Mirroring::SingleScreenLow;
        }
    } else if (area>=0xB000u && area<=0xE000u) {
        const unsigned slot=((area-0xB000u)>>12)*2u+(reg>>1);
        const unsigned mask=vrc2_?0x0Fu:0x1Fu;
        if (reg&1u) chrBanks_[slot]=(chrBanks_[slot]&0x0Fu)|((value&mask)<<4);
        else chrBanks_[slot]=(chrBanks_[slot]&~0x0Fu)|(value&0x0Fu);
    } else if (area==0xF000u && !vrc2_) {
        switch(reg) {
        case 0: irqLatch_=(irqLatch_&0xF0u)|(value&15u); break;
        case 1: irqLatch_=(irqLatch_&0x0Fu)|((value&15u)<<4); break;
        case 2:
            irqControl_=value&7u; irqPending_=false; irqEnabled_=(value&2u)!=0;
            irqPrescaler_=341;
            if (irqEnabled_) irqCounter_=irqLatch_;
            break;
        case 3:
            irqPending_=false; irqEnabled_=(irqControl_&1u)!=0;
            break;
        }
    }
}
std::uint8_t MapperVrc24::ppuRead(std::uint16_t address) {
    const unsigned slot=(address&0x1FFFu)>>10;
    const std::size_t bank=(mapperId_==22?chrBanks_[slot]>>1:chrBanks_[slot])%(chr_.size()/0x400u);
    return chr_[bank*0x400u+(address&0x3FFu)];
}
void MapperVrc24::ppuWrite(std::uint16_t address,std::uint8_t value) {
    if (!chrRam_) return;
    const unsigned slot=(address&0x1FFFu)>>10;
    const std::size_t bank=(mapperId_==22?chrBanks_[slot]>>1:chrBanks_[slot])%(chr_.size()/0x400u);
    chr_[bank*0x400u+(address&0x3FFu)]=value;
}
void MapperVrc24::clockIrq() {
    if (irqCounter_==255u) { irqCounter_=irqLatch_; irqPending_=true; }
    else ++irqCounter_;
}
void MapperVrc24::tickCpu() {
    if (vrc2_ || !irqEnabled_) return;
    if (irqControl_&4u) clockIrq();
    else {
        irqPrescaler_-=3;
        if (irqPrescaler_<=0) { irqPrescaler_+=341; clockIrq(); }
    }
}
} // namespace frantastic::nes

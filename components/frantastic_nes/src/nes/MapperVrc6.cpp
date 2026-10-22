#include "frantastic/nes/MapperVrc6.hpp"
#include <algorithm>

namespace frantastic::nes {
MapperVrc6::MapperVrc6(const RomImage& image)
    : mapperId_(image.mapper), prg_(image.prg),
      chr_(image.chr.empty() ? std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0) : image.chr),
      ram_(image.prgRamSize ? image.prgRamSize : 0x2000u,0),
      chrRam_(image.chr.empty()), fourScreen_(image.mirroring==Mirroring::FourScreen),
      mirroring_(image.mirroring) {
    if (prg_.size()<0x8000u || (prg_.size()%0x2000u) || chr_.size()%0x400u)
        throw RomFormatError("NES: VRC6 needs 8 KiB PRG and 1 KiB CHR bank alignment");
    reset();
}
void MapperVrc6::reset() {
    prg16_=prg8_=banking_=0; chrBanks_.fill(0);
    pulses_={}; sawRegs_.fill(0); sawDivider_=0; sawStep_=sawAccumulator_=audioControl_=0;
    irqLatch_=irqCounter_=irqControl_=0;
    irqPrescaler_=341; irqEnabled_=irqPending_=false;
}
unsigned MapperVrc6::normalizedRegister(std::uint16_t address) const {
    unsigned a=address & 0xF003;
    if (mapperId_==26) a=(a & ~3u) | ((a & 1u)<<1) | ((a & 2u)>>1);
    return a;
}
std::uint8_t MapperVrc6::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address>=0x8000) {
        const std::size_t count=prg_.size()/0x2000;
        std::size_t bank;
        if (address<0xC000) bank=static_cast<std::size_t>(prg16_ & 15u)*2 + ((address>>13)&1u);
        else if (address<0xE000) bank=prg8_ & 31u;
        else bank=count-1;
        return prg_[(bank%count)*0x2000+(address&0x1FFFu)];
    }
    if (address>=0x6000 && (banking_&0x80u))
        return ram_[(address-0x6000u)%ram_.size()];
    return openBus;
}
unsigned MapperVrc6::patternBank(unsigned slot) const {
    switch (banking_&3u) {
    case 0: return chrBanks_[slot];
    case 1: return chrBanks_[slot/2u];
    default: return slot<4 ? chrBanks_[slot] : chrBanks_[4+(slot-4)/2u];
    }
}
std::uint8_t MapperVrc6::ppuRead(std::uint16_t address) {
    const unsigned slot=(address&0x1FFFu)>>10;
    unsigned bank=patternBank(slot);
    // With B003 bit 5 set, the 2 KiB modes pass through PPU A10.
    if ((banking_&0x20u) && ((banking_&3u)!=0) &&
        ((banking_&3u)==1 || slot>=4)) bank=(bank&~1u)|(slot&1u);
    const std::size_t index=(bank%(chr_.size()/0x400u))*0x400u+(address&0x3FFu);
    return chr_[index];
}
void MapperVrc6::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (!chrRam_) return;
    const unsigned slot=(address&0x1FFFu)>>10;
    unsigned bank=patternBank(slot);
    if ((banking_&0x20u) && ((banking_&3u)!=0) &&
        ((banking_&3u)==1 || slot>=4)) bank=(bank&~1u)|(slot&1u);
    chr_[(bank%(chr_.size()/0x400u))*0x400u+(address&0x3FFu)]=value;
}
void MapperVrc6::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address>=0x6000 && address<0x8000) {
        if (banking_&0x80u) ram_[(address-0x6000u)%ram_.size()]=value;
        return;
    }
    if (address<0x8000) return;
    const unsigned reg=normalizedRegister(address);
    if ((reg&0xF000u)==0x8000u) { prg16_=value&15u; return; }
    if ((reg&0xF000u)==0xC000u) { prg8_=value&31u; return; }
    if ((reg&0xF000u)==0xD000u || (reg&0xF000u)==0xE000u) {
        chrBanks_[((reg>>12)-0xDu)*4u+(reg&3u)]=value; return;
    }
    if (reg==0xB003u) {
        banking_=value;
        if (!fourScreen_) {
            switch ((value>>2)&3u) {
            case 0: mirroring_=Mirroring::Vertical; break;
            case 1: mirroring_=Mirroring::Horizontal; break;
            case 2: mirroring_=Mirroring::SingleScreenLow; break;
            case 3: mirroring_=Mirroring::SingleScreenHigh; break;
            }
        }
        return;
    }
    if (reg>=0x9000u && reg<=0x9003u) {
        if ((reg&3u)==3u) audioControl_=value;
        else pulses_[0].regs[reg&3u]=value;
        return;
    }
    if (reg>=0xA000u && reg<=0xA002u) { pulses_[1].regs[reg&3u]=value; return; }
    if (reg>=0xB000u && reg<=0xB002u) { sawRegs_[reg&3u]=value; return; }
    if (reg>=0xF000u && reg<=0xF002u) {
        switch (reg&3u) {
        case 0: irqLatch_=value; break;
        case 1:
            irqControl_=value&7u;
            irqPending_=false;
            irqEnabled_=(value&2u)!=0;
            if (irqEnabled_) { irqCounter_=irqLatch_; irqPrescaler_=341; }
            break;
        case 2:
            irqPending_=false;
            irqEnabled_=(irqControl_&1u)!=0;
            break;
        }
    }
}
void MapperVrc6::clockIrq() {
    if (irqCounter_==255) {
        irqCounter_=irqLatch_;
        irqPending_=true;
    } else ++irqCounter_;
}
void MapperVrc6::tickCpu() {
    if (irqEnabled_) {
        if (irqControl_&4u) clockIrq();
        else {
            irqPrescaler_-=3;
            if (irqPrescaler_<=0) {
                irqPrescaler_+=341;
                clockIrq();
            }
        }
    }
    if (audioControl_&1u) return;
    const unsigned scale=(audioControl_&4u)?8u:((audioControl_&2u)?4u:0u);
    for (auto& pulse:pulses_) {
        if (!(pulse.regs[2]&0x80u)) continue;
        if (pulse.divider==0) {
            const unsigned period=(pulse.regs[1]|((pulse.regs[2]&15u)<<8));
            pulse.divider=static_cast<std::uint16_t>(period>>scale);
            pulse.phase=(pulse.phase-1)&15u;
        } else --pulse.divider;
    }
    if (sawRegs_[2]&0x80u) {
        if (sawDivider_==0) {
            const unsigned period=sawRegs_[1]|((sawRegs_[2]&15u)<<8);
            sawDivider_=static_cast<std::uint16_t>(period>>scale);
            if ((sawStep_&1u) && sawStep_<13u)
                sawAccumulator_=static_cast<std::uint8_t>(sawAccumulator_+(sawRegs_[0]&63u));
            sawStep_++;
            if (sawStep_==14) { sawStep_=0; sawAccumulator_=0; }
        } else --sawDivider_;
    } else { sawStep_=0; sawAccumulator_=0; }
}
float MapperVrc6::expansionAudio() const {
    float amplitude=0;
    for (const auto& p:pulses_) {
        if (!(p.regs[2]&0x80u)) continue;
        const bool on=(p.regs[0]&0x80u) || p.phase<=((p.regs[0]>>4)&7u);
        if (on) amplitude+=static_cast<float>(p.regs[0]&15u);
    }
    if (sawRegs_[2]&0x80u) amplitude+=static_cast<float>(sawAccumulator_>>3);
    return amplitude*(0.12f/61.f);
}
} // namespace frantastic::nes

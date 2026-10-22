#include "frantastic/nes/Mapper091.hpp"
#include <algorithm>

namespace frantastic::nes {
Mapper091::Mapper091(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
      chrRam_(image.chr.empty()),sub1_(image.submapper==1),
      mirroring_(image.mirroring),hardwareMirroring_(image.mirroring) {
    if(prg_.size()<0x4000u || prg_.size()%0x2000u || chr_.size()%0x800u)
        throw RomFormatError("NES: mapper 91 requires 8 KiB PRG and 2 KiB CHR alignment");
    reset();
}
void Mapper091::reset() {
    prgBanks_.fill(0);chrBanks_.fill(0);outerBank_=0;
    ppuCounter_=cpuPhase_=0;cpuCounter_=0;irqEnabled_=irqPending_=lastA12_=false;
    mirroring_=hardwareMirroring_;
}
std::uint8_t Mapper091::cpuPeek(std::uint16_t a,std::uint8_t bus) const {
    if(a<0x8000u) return bus;
    const unsigned slot=(a-0x8000u)>>13;
    const std::size_t count=prg_.size()/0x2000u;
    const std::size_t bank=slot>=2u ? count-(4u-slot)
        : ((static_cast<std::size_t>(outerBank_>>1u & 3u)<<4u)|(prgBanks_[slot]&15u))%count;
    return prg_[bank*0x2000u+(a&0x1FFFu)];
}
void Mapper091::cpuWrite(std::uint16_t a,std::uint8_t value) {
    if(a>=0x6000u && a<0x8000u) {
        const unsigned reg=a & (sub1_?7u:3u);
        if(a<0x7000u) {
            if(reg<4u) chrBanks_[reg]=value;
            else if(sub1_) {
                if(reg==4u) mirroring_=Mirroring::Horizontal;
                else if(reg==5u) mirroring_=Mirroring::Vertical;
                else if(reg==6u) cpuCounter_=(cpuCounter_&0xFF00u)|value;
                else if(reg==7u) cpuCounter_=(cpuCounter_&0x00FFu)|(static_cast<std::uint16_t>(value)<<8u);
            }
        } else {
            if(reg<2u) prgBanks_[reg]=value;
            else if(reg==6u || (!sub1_ && reg==2u)) {irqEnabled_=false;irqPending_=false;}
            else if(reg==7u || (!sub1_ && reg==3u)) {
                irqEnabled_=true;irqPending_=false;
                ppuCounter_=0;cpuPhase_=0;
            }
        }
        return;
    }
    if(!sub1_ && a>=0x8000u && a<0xA000u) outerBank_=value&7u;
}
std::uint8_t Mapper091::ppuRead(std::uint16_t a) {
    a&=0x1FFFu;
    const std::size_t count=chr_.size()/0x800u;
    const std::size_t bank=((static_cast<std::size_t>(outerBank_&1u)<<8u)|chrBanks_[a>>11])%count;
    return chr_[bank*0x800u+(a&0x7FFu)];
}
void Mapper091::ppuWrite(std::uint16_t a,std::uint8_t value) {
    if(!chrRam_) return;
    a&=0x1FFFu;
    const std::size_t bank=((static_cast<std::size_t>(outerBank_&1u)<<8u)|chrBanks_[a>>11])%(chr_.size()/0x800u);
    chr_[bank*0x800u+(a&0x7FFu)]=value;
}
void Mapper091::ppuAddressTick(std::uint16_t a,std::uint64_t) {
    if(sub1_) return;
    const bool high=(a&0x1000u)!=0;
    // Submapper 0 clocks unfiltered PPU A12 edges, including $2007 activity.
    if(high && !lastA12_ && irqEnabled_) {
        if(++ppuCounter_>=64u) {irqPending_=true;irqEnabled_=false;}
    }
    lastA12_=high;
}
void Mapper091::tickCpu() {
    if(!sub1_ || !irqEnabled_) return;
    if(++cpuPhase_==4u) {
        cpuPhase_=0;
        if(cpuCounter_<=5u) {cpuCounter_=0;irqPending_=true;irqEnabled_=false;}
        else cpuCounter_=static_cast<std::uint16_t>(cpuCounter_-5u);
    }
}
} // namespace frantastic::nes

#include "frantastic/nes/Mapper210.hpp"
#include <algorithm>

namespace frantastic::nes {
Mapper210::Mapper210(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(0x2000u,image.chrRamSize),0):image.chr),
      ram_(image.prgRamSize ? image.prgRamSize : 0x2000u,0),
      chrRam_(image.chr.empty()),
      namco175_(image.submapper==1 || (image.submapper==0 && image.battery)),
      mirroring_(image.mirroring), hardwareMirroring_(image.mirroring) {
    if(prg_.size()<0x2000u || prg_.size()%0x2000u || chr_.size()%0x400u)
        throw RomFormatError("NES: Namco 175/340 requires 8 KiB PRG and 1 KiB CHR alignment");
    reset();
}
void Mapper210::reset() {
    chrBanks_.fill(0); prgBanks_.fill(0);
    ramEnabled_=false;
    mirroring_=hardwareMirroring_;
}
std::uint8_t Mapper210::cpuPeek(std::uint16_t a,std::uint8_t bus) const {
    if(a>=0x8000u) {
        const unsigned slot=(a-0x8000u)>>13;
        const std::size_t count=prg_.size()/0x2000u;
        const std::size_t bank=slot==3 ? count-1 : (prgBanks_[slot]&0x3Fu)%count;
        return prg_[bank*0x2000u+(a&0x1FFFu)];
    }
    if(a>=0x6000u && namco175_ && !ram_.empty())
        return ram_[(a-0x6000u)%ram_.size()];
    return bus;
}
void Mapper210::cpuWrite(std::uint16_t a,std::uint8_t value) {
    if(a>=0x6000u && a<0x8000u) {
        if(namco175_ && ramEnabled_ && !ram_.empty())
            ram_[(a-0x6000u)%ram_.size()]=value;
        return;
    }
    if(a<0x8000u) return;
    const unsigned reg=(a-0x8000u)>>11;
    if(reg<8u) {chrBanks_[reg]=value;return;}
    if(reg==8u) {if(namco175_) ramEnabled_=(value&1u)!=0;return;}
    if(reg==12u) {
        prgBanks_[0]=value&0x3Fu;
        if(!namco175_) {
            switch(value>>6) {
            case 0: mirroring_=Mirroring::SingleScreenLow; break;
            case 1: mirroring_=Mirroring::Vertical; break;
            case 2: mirroring_=Mirroring::SingleScreenHigh; break;
            default: mirroring_=Mirroring::Horizontal; break;
            }
        }
    } else if(reg==13u) prgBanks_[1]=value&0x3Fu;
    else if(reg==14u) prgBanks_[2]=value&0x3Fu;
}
std::uint8_t Mapper210::ppuRead(std::uint16_t a) {
    a&=0x1FFFu;
    const std::size_t bank=chrBanks_[a>>10]%(chr_.size()/0x400u);
    return chr_[bank*0x400u+(a&0x3FFu)];
}
void Mapper210::ppuWrite(std::uint16_t a,std::uint8_t value) {
    if(!chrRam_) return;
    a&=0x1FFFu;
    const std::size_t bank=chrBanks_[a>>10]%(chr_.size()/0x400u);
    chr_[bank*0x400u+(a&0x3FFu)]=value;
}
} // namespace frantastic::nes

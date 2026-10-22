#include "frantastic/nes/MapperJalecoTaito.hpp"
#include <algorithm>

namespace frantastic::nes {
MapperJalecoTaito::MapperJalecoTaito(const RomImage& image)
    : id_(image.mapper),prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
      ram_(image.prgRamSize?image.prgRamSize:0x2000u,0),
      chrRam_(image.chr.empty()),mirroring_(image.mirroring),hardwareMirroring_(image.mirroring) {
    if((id_!=18 && id_!=33) || prg_.size()<0x8000u || prg_.size()%0x2000u || chr_.size()%0x400u)
        throw RomFormatError("NES: Jaleco/Taito mapper requires aligned 8 KiB PRG and 1 KiB CHR");
    reset();
}
void MapperJalecoTaito::reset() {
    prgBanks_.fill(0);chrBanks_.fill(0);
    irqLatch_=irqCounter_=0;irqControl_=0;irqPending_=false;
    ramEnabled_=ramWritable_=false;mirroring_=hardwareMirroring_;
}
void MapperJalecoTaito::writeNibble(std::uint8_t& bank,unsigned nibble,std::uint8_t v) {
    if(nibble) bank=static_cast<std::uint8_t>((bank&15u)|((v&15u)<<4u));
    else bank=static_cast<std::uint8_t>((bank&0xF0u)|(v&15u));
}
std::uint8_t MapperJalecoTaito::cpuPeek(std::uint16_t a,std::uint8_t bus) const {
    if(a>=0x8000u) {
        const unsigned slot=(a-0x8000u)>>13;
        const std::size_t count=prg_.size()/0x2000u;
        const std::size_t bank=(id_==33 && slot>=2u) ? count-(4u-slot) :
            (slot==3u ? count-1u : static_cast<std::size_t>(prgBanks_[slot]&0x3Fu)%count);
        return prg_[bank*0x2000u+(a&0x1FFFu)];
    }
    if(id_==18 && a>=0x6000u && ramEnabled_ && !ram_.empty())
        return ram_[(a-0x6000u)%ram_.size()];
    return bus;
}
void MapperJalecoTaito::cpuWrite(std::uint16_t a,std::uint8_t v) {
    if(a<0x8000u) {
        if(id_==18 && a>=0x6000u && ramEnabled_ && ramWritable_ && !ram_.empty())
            ram_[(a-0x6000u)%ram_.size()]=v;
        return;
    }
    if(id_==33) {
        const unsigned reg=a&0xA003u;
        if(reg==0x8000u) {
            prgBanks_[0]=v&0x3Fu;
            mirroring_=(v&0x40u)?Mirroring::Horizontal:Mirroring::Vertical;
        } else if(reg==0x8001u) prgBanks_[1]=v&0x3Fu;
        else if(reg==0x8002u || reg==0x8003u) {
            const unsigned slot=(reg==0x8002u)?0u:2u;
            chrBanks_[slot]=static_cast<std::uint8_t>(v*2u);
            chrBanks_[slot+1]=static_cast<std::uint8_t>(v*2u+1u);
        } else if(reg>=0xA000u && reg<=0xA003u)
            chrBanks_[4u+(reg&3u)]=v;
        return;
    }
    const unsigned reg=a&0xF003u;
    const unsigned page=(reg>>12)&15u;
    const unsigned lane=reg&3u;
    if(page==8u || page==9u) {
        if(page==9u && lane==2u) {
            ramEnabled_=(v&1u)!=0;ramWritable_=(v&2u)!=0;
        } else if((page==8u && lane<4u) || (page==9u && lane<2u)) {
            const unsigned bank=page==8u ? lane/2u : 2u;
            writeNibble(prgBanks_[bank],lane&1u,v);
            prgBanks_[bank]&=0x3Fu;
        }
    } else if(page>=10u && page<=13u) {
        writeNibble(chrBanks_[(page-10u)*2u+lane/2u],lane&1u,v);
    } else if(page==14u) {
        const unsigned shift=lane*4u;
        irqLatch_=static_cast<std::uint16_t>((irqLatch_&~(0xFu<<shift))|((v&15u)<<shift));
    } else if(page==15u) {
        switch(lane) {
        case 0:irqCounter_=irqLatch_;irqPending_=false;break;
        case 1:irqControl_=v&15u;irqPending_=false;break;
        case 2:
            switch(v&3u) {
            case 0:mirroring_=Mirroring::Horizontal;break;
            case 1:mirroring_=Mirroring::Vertical;break;
            case 2:mirroring_=Mirroring::SingleScreenLow;break;
            default:mirroring_=Mirroring::SingleScreenHigh;break;
            }
            break;
        default: break; // Optional external ADPCM, not contained in ROM.
        }
    }
}
std::uint8_t MapperJalecoTaito::ppuRead(std::uint16_t a) {
    a&=0x1FFFu;
    return chr_[(static_cast<std::size_t>(chrBanks_[a>>10])%(chr_.size()/0x400u))*0x400u+(a&0x3FFu)];
}
void MapperJalecoTaito::ppuWrite(std::uint16_t a,std::uint8_t v) {
    if(!chrRam_) return;
    a&=0x1FFFu;
    chr_[(static_cast<std::size_t>(chrBanks_[a>>10])%(chr_.size()/0x400u))*0x400u+(a&0x3FFu)]=v;
}
void MapperJalecoTaito::tickCpu() {
    if(id_!=18 || !(irqControl_&1u)) return;
    const unsigned mask=(irqControl_&8u)?0xFu:(irqControl_&4u)?0xFFu:(irqControl_&2u)?0xFFFu:0xFFFFu;
    const unsigned low=irqCounter_&mask;
    if(low==0u) irqPending_=true;
    irqCounter_=static_cast<std::uint16_t>((irqCounter_&~mask)|((low-1u)&mask));
}
} // namespace frantastic::nes

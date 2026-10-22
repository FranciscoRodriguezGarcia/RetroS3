#include "frantastic/nes/Mapper019.hpp"
#include <algorithm>

namespace frantastic::nes {
Mapper019::Mapper019(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
      ram_(image.prgRamSize?image.prgRamSize:0x2000u,0),
      chrRam_(image.chr.empty()),mirroring_(image.mirroring) {
    if (prg_.size()<0x8000u || (prg_.size()%0x2000u) || chr_.size()%0x400u)
        throw RomFormatError("NES: Namco 163 requires aligned 8 KiB PRG and 1 KiB CHR banks");
    reset();
}
void Mapper019::reset() {
    chrBanks_.fill(0);
    // Hardware powers on undefined; deterministic defaults aid reproducibility.
    chrBanks_[8]=0xE0; chrBanks_[9]=0xE1; chrBanks_[10]=0xE0; chrBanks_[11]=0xE1;
    prgBanks_.fill(0);
    soundRam_.fill(0);
    soundAddress_=ramControl_=prgControl_=chrControl_=auxControl_=0;
    irqCounter_=0;irqEnabled_=irqPending_=false;
    soundDivider_=0;soundChannel_=7;soundOutput_=0;ciram_=nullptr;
}
void Mapper019::incrementSoundAddress() {
    if ((soundAddress_&0x80u) && (soundAddress_&0x7Fu)!=0x7Fu)
        soundAddress_=static_cast<std::uint8_t>(soundAddress_+1u);
}
std::uint8_t Mapper019::cpuRead(std::uint16_t address,std::uint8_t openBus) {
    if (address>=0x4800u && address<0x5000u) {
        const auto value=soundRam_[soundAddress_&0x7Fu];
        incrementSoundAddress();
        return value;
    }
    return cpuPeek(address,openBus);
}
std::uint8_t Mapper019::cpuPeek(std::uint16_t address,std::uint8_t openBus) const {
    if (address>=0x8000u) {
        const unsigned slot=(address-0x8000u)>>13;
        const std::size_t count=prg_.size()/0x2000u;
        const std::size_t bank=slot==3?count-1:prgBanks_[slot]&0x3Fu;
        return prg_[(bank%count)*0x2000u+(address&0x1FFFu)];
    }
    if (address>=0x6000u)
        return ram_[(address-0x6000u)%ram_.size()];
    if (address>=0x5800u) return static_cast<std::uint8_t>((irqCounter_>>8)|(irqEnabled_?0x80u:0));
    if (address>=0x5000u) return static_cast<std::uint8_t>(irqCounter_&0xFFu);
    if (address>=0x4800u) return soundRam_[soundAddress_&0x7Fu];
    return openBus;
}
void Mapper019::cpuWrite(std::uint16_t address,std::uint8_t value) {
    if (address>=0x4800u && address<0x5000u) {
        soundRam_[soundAddress_&0x7Fu]=value;incrementSoundAddress();return;
    }
    if (address>=0x5000u && address<0x5800u) {
        irqCounter_=static_cast<std::uint16_t>((irqCounter_&0x7F00u)|value);
        irqPending_=false;return;
    }
    if (address>=0x5800u && address<0x6000u) {
        irqCounter_=static_cast<std::uint16_t>((irqCounter_&0x00FFu)|((value&0x7Fu)<<8));
        irqEnabled_=(value&0x80u)!=0;irqPending_=false;return;
    }
    if (address>=0x6000u && address<0x8000u) {
        const unsigned window=(address-0x6000u)>>11;
        if ((ramControl_&0xF0u)==0x40u && !(ramControl_&(1u<<window)))
            ram_[(address-0x6000u)%ram_.size()]=value;
        return;
    }
    if (address<0x8000u) return;
    const unsigned reg=(address-0x8000u)>>11;
    if (reg<12u) { chrBanks_[reg]=value;return; }
    if (reg==12u) {prgBanks_[0]=value&0x3Fu;prgControl_=value;return;}
    if (reg==13u) {prgBanks_[1]=value&0x3Fu;chrControl_=value;return;}
    if (reg==14u) {prgBanks_[2]=value&0x3Fu;auxControl_=value;return;}
    ramControl_=value;soundAddress_=value;
}
std::uint8_t Mapper019::readPattern(std::uint16_t address,unsigned slot) const {
    const std::uint8_t bank=chrBanks_[slot];
    const bool useCiram=bank>=0xE0u && (slot>=8u || !(chrControl_&(slot<4u?0x40u:0x80u)));
    if (useCiram) {
        if (!ciram_) return 0;
        return ciram_[((bank&1u)<<10)|(address&0x3FFu)];
    }
    return chr_[(static_cast<std::size_t>(bank)%(chr_.size()/0x400u))*0x400u+(address&0x3FFu)];
}
void Mapper019::writePattern(std::uint16_t address,unsigned slot,std::uint8_t value) {
    const std::uint8_t bank=chrBanks_[slot];
    const bool useCiram=bank>=0xE0u && (slot>=8u || !(chrControl_&(slot<4u?0x40u:0x80u)));
    if (useCiram) {
        if (ciram_) ciram_[((bank&1u)<<10)|(address&0x3FFu)]=value;
    } else if (chrRam_)
        chr_[(static_cast<std::size_t>(bank)%(chr_.size()/0x400u))*0x400u+(address&0x3FFu)]=value;
}
std::uint8_t Mapper019::ppuRead(std::uint16_t address) {
    address&=0x1FFFu;return readPattern(address,address>>10);
}
void Mapper019::ppuWrite(std::uint16_t address,std::uint8_t value) {
    address&=0x1FFFu;writePattern(address,address>>10,value);
}
std::uint8_t Mapper019::ppuNametableRead(std::uint16_t address,std::uint8_t* vram,bool) {
    ciram_=vram;
    const unsigned slot=8u+((address>>10)&3u);
    return readPattern(address,slot);
}
void Mapper019::ppuNametableWrite(std::uint16_t address,std::uint8_t value,std::uint8_t* vram) {
    ciram_=vram;
    const unsigned slot=8u+((address>>10)&3u);
    writePattern(address,slot,value);
}
void Mapper019::clockSound() {
    if (prgControl_&0x40u) {soundOutput_=0;return;}
    const unsigned count=((soundRam_[0x7Fu]>>4)&7u)+1u;
    const unsigned lowest=8u-count;
    if (soundChannel_<lowest || soundChannel_>7u) soundChannel_=7u;
    const unsigned base=0x40u+soundChannel_*8u;
    const unsigned frequency=soundRam_[base]|(soundRam_[base+2u]<<8)|((soundRam_[base+4u]&3u)<<16);
    const unsigned length=256u-(soundRam_[base+4u]&0xFCu);
    unsigned phase=soundRam_[base+1u]|(soundRam_[base+3u]<<8)|(soundRam_[base+5u]<<16);
    phase=(phase+frequency)%(length<<16);
    soundRam_[base+1u]=static_cast<std::uint8_t>(phase);
    soundRam_[base+3u]=static_cast<std::uint8_t>(phase>>8);
    soundRam_[base+5u]=static_cast<std::uint8_t>(phase>>16);
    const unsigned sampleIndex=(((phase>>16)+soundRam_[base+6u])&0xFFu);
    const unsigned packed=soundRam_[sampleIndex>>1u];
    const unsigned nibble=(packed>>((sampleIndex&1u)*4u))&0xFu;
    soundOutput_=(static_cast<int>(nibble)-8)*static_cast<int>(soundRam_[base+7u]&15u);
    if (soundChannel_==lowest) soundChannel_=7u;else --soundChannel_;
}
void Mapper019::tickCpu() {
    if (irqEnabled_ && irqCounter_<0x7FFFu) {
        ++irqCounter_;
        if (irqCounter_==0x7FFFu) irqPending_=true;
    }
    if (++soundDivider_==15u) {soundDivider_=0;clockSound();}
}
float Mapper019::expansionAudio() const {
    return soundOutput_*(0.12f/120.f);
}
} // namespace frantastic::nes

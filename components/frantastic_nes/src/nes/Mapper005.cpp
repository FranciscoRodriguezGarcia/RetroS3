#include "frantastic/nes/Mapper005.hpp"
#include <algorithm>

namespace frantastic::nes {

Mapper005::Mapper005(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty() ? std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0)
                             : image.chr),
      ram_(std::max<std::size_t>(image.prgRamSize,0x2000u),0),
      chrRam_(image.chr.empty()), mirroring_(image.mirroring) {
    if (prg_.size() < 0x8000u || prg_.size()%0x2000u)
        throw RomFormatError("NES: MMC5 needs PRG ROM in 8 KiB units");
    if (chr_.size() < 0x2000u || chr_.size()%0x400u)
        throw RomFormatError("NES: MMC5 needs CHR in 1 KiB units");
    reset();
}

void Mapper005::reset() {
    prgMode_=3; chrMode_=0;
    prgRegs_.fill(0); prgRegs_[4]=0xFF;
    chrRegs_.fill(0);
    ramProtect1_=ramProtect2_=0;
    exRamMode_=ntMap_=fillTile_=fillAttr_=chrHigh_=0;
    multiplierA_=multiplierB_=0;
    lastChrBg_=false;
    extendedTile_=0;
    irqTarget_=irqScanline_=0;
    irqEnable_=irqPending_=inFrame_=false;
    lastScanline_=lastDot_=-1;
    splitControl_=splitScroll_=splitBank_=0;
    splitActive_=false; splitColumn_=0; splitLine_=0;
    pulses_={}; pcmLevel_=0; pcmReadMode_=pcmIrqEnable_=pcmIrqPending_=false;
    audioCycle_=0;
}

Mapper005::PrgWindow Mapper005::window(std::uint16_t address) const {
    if (address < 0x8000u) return {prgRegs_[0],0x2000u,address-0x6000u,false};
    const unsigned slot=(address-0x8000u)/0x2000u;
    if (prgMode_==0) return {prgRegs_[4],0x8000u,address-0x8000u,true};
    if (prgMode_==1) {
        if (slot<2) return {prgRegs_[2],0x4000u,address-0x8000u,false};
        return {prgRegs_[4],0x4000u,address-0xC000u,true};
    }
    if (prgMode_==2) {
        if (slot<2) return {prgRegs_[2],0x4000u,address-0x8000u,false};
        if (slot==2) return {prgRegs_[3],0x2000u,address-0xC000u,false};
        return {prgRegs_[4],0x2000u,address-0xE000u,true};
    }
    const unsigned reg=slot+1u;
    return {prgRegs_[reg],0x2000u,address & 0x1FFFu,slot==3};
}

std::uint8_t Mapper005::readPrg(std::uint16_t address, std::uint8_t openBus) const {
    const auto w=window(address);
    if (address>=0x8000u && (w.forceRom || (w.bank & 0x80u))) {
        const auto count=prg_.size()/w.size;
        const auto bank=(static_cast<std::size_t>(w.bank & 0x7Fu)/(w.size/0x2000u))%count;
        return prg_[bank*w.size+w.offset];
    }
    // $5113 at $6000 is always RAM. ROM-disabled windows may expose RAM.
    if (ram_.empty()) return openBus;
    const std::size_t bank=address<0x8000u ? (w.bank & 0x07u)
                         : (w.bank & 0x07u)/(w.size/0x2000u);
    return ram_[(bank*w.size+w.offset)%ram_.size()];
}

void Mapper005::writePrg(std::uint16_t address, std::uint8_t value) {
    if (address>=0x8000u && (window(address).forceRom || (window(address).bank & 0x80u))) return;
    if (ramProtect1_!=2 || ramProtect2_!=1 || ram_.empty()) return;
    const auto w=window(address);
    const auto bank=address<0x8000u ? (w.bank & 7u) : (w.bank & 7u)/(w.size/0x2000u);
    ram_[(static_cast<std::size_t>(bank)*w.size+w.offset)%ram_.size()]=value;
}

std::uint8_t Mapper005::cpuPeek(std::uint16_t address, std::uint8_t openBus) const {
    if (address>=0x6000u) return readPrg(address,openBus);
    if (address==0x5010u) return static_cast<std::uint8_t>((pcmIrqPending_?0x80u:0u)|1u);
    if (address==0x5015u) return static_cast<std::uint8_t>((pulses_[0].length?1u:0u)|(pulses_[1].length?2u:0u));
    if (address==0x5204u) return static_cast<std::uint8_t>((irqPending_?0x80u:0u)|(inFrame_?0x40u:0u));
    if (address>=0x5C00u) return exRamMode_>=2 ? exRam_[address-0x5C00u] : openBus;
    if (address==0x5205u || address==0x5206u) {
        const unsigned product=static_cast<unsigned>(multiplierA_)*multiplierB_;
        return address==0x5205u ? static_cast<std::uint8_t>(product)
                                 : static_cast<std::uint8_t>(product>>8);
    }
    return openBus;
}
std::uint8_t Mapper005::cpuRead(std::uint16_t address, std::uint8_t openBus) {
    if (address==0x5204u) { const auto v=cpuPeek(address,openBus); irqPending_=false; return v; }
    if (address==0x5010u) { const auto v=cpuPeek(address,openBus); pcmIrqPending_=false; return v; }
    const auto v=cpuPeek(address,openBus);
    if (pcmReadMode_ && address>=0x8000u && address<0xC000u) pcmWrite(v);
    return v;
}
void Mapper005::cpuWrite(std::uint16_t address, std::uint8_t value) {
    if (address>=0x6000u) { writePrg(address,value); return; }
    if ((address>=0x5000u && address<=0x5007u)) {
        const unsigned channel=(address-0x5000u)/4u;
        const unsigned reg=address&3u;
        Pulse& p=pulses_[channel];
        if (reg==0u) { p.duty=value>>6u; p.halt=(value&0x20u)!=0; p.constant=(value&0x10u)!=0; p.volume=value&15u; }
        if (reg==2u) p.period=static_cast<std::uint16_t>((p.period&0x700u)|value);
        if (reg==3u) {
            static constexpr std::uint8_t lengths[32]={10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
                                                        12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30};
            p.period=static_cast<std::uint16_t>((p.period&0xFFu)|((value&7u)<<8u));
            if (p.enabled) p.length=lengths[value>>3u];
            p.step=0; p.envStart=true;
        }
        return;
    }
    if (address==0x5010u) { pcmReadMode_=(value&1u)!=0; pcmIrqEnable_=(value&0x80u)!=0; if (!pcmIrqEnable_) pcmIrqPending_=false; return; }
    if (address==0x5011u) { if (!pcmReadMode_) pcmWrite(value); return; }
    if (address==0x5015u) {
        for (unsigned i=0;i<2;++i) { pulses_[i].enabled=(value&(1u<<i))!=0; if (!pulses_[i].enabled) pulses_[i].length=0; }
        return;
    }
    if (address>=0x5C00u) {
        if (exRamMode_!=3) exRam_[address-0x5C00u]=value;
        return;
    }
    if (address>=0x5113u && address<=0x5117u) {
        prgRegs_[address-0x5113u]=value; return;
    }
    if (address>=0x5120u && address<=0x512Bu) {
        chrRegs_[address-0x5120u]=static_cast<std::uint16_t>((chrHigh_<<8)|value);
        lastChrBg_=address>=0x5128u; return;
    }
    switch(address) {
    case 0x5100u: prgMode_=value&3u; break;
    case 0x5101u: chrMode_=value&3u; break;
    case 0x5102u: ramProtect1_=value&3u; break;
    case 0x5103u: ramProtect2_=value&3u; break;
    case 0x5104u: exRamMode_=value&3u; break;
    case 0x5105u: ntMap_=value; break;
    case 0x5106u: fillTile_=value; break;
    case 0x5107u: fillAttr_=value&3u; break;
    case 0x5130u: chrHigh_=value&3u; break;
    case 0x5200u: splitControl_=value; break;
    case 0x5201u: splitScroll_=value; break;
    case 0x5202u: splitBank_=value; break;
    case 0x5203u: irqTarget_=value; break;
    case 0x5204u: irqEnable_=(value&0x80u)!=0; break;
    case 0x5205u: multiplierA_=value; break;
    case 0x5206u: multiplierB_=value; break;
    default: break;
    }
}

std::size_t Mapper005::chrOffset(std::uint16_t address, bool background, bool sprites16) const {
    const unsigned slot=(address&0x1FFFu)/0x400u;
    // During 8x16 sprite rendering MMC5 uses two independent CHR banks:
    // eight sprite registers and four repeating background registers.
    // In 8x8 mode the most recently written register set supplies both.
    const bool useBg=sprites16 ? background : lastChrBg_;
    if (background && splitActive_) {
        const std::size_t bank=splitBank_;
        return (bank*0x1000u+(address&0x0FFFu))%chr_.size();
    }
    if (background && exRamMode_==1) {
        const std::size_t bank=(static_cast<std::size_t>(chrHigh_)<<6)|(extendedTile_&0x3Fu);
        return (bank*0x1000u+(address&0x0FFFu))%chr_.size();
    }
    const unsigned group=1u<<chrMode_;
    const unsigned windowSlots=8u/group;
    const unsigned end=slot/windowSlots*windowSlots+(windowSlots-1u);
    const unsigned reg=useBg ? (8u+(end%4u)) : end;
    const std::size_t bank=chrRegs_[reg];
    const std::size_t count=chr_.size()/0x400u;
    // MMC5 banks are indexed in units of the selected CHR window size.
    // Unlike MMC1, the low register bits remain significant in 2/4/8 KiB modes.
    return ((bank*windowSlots+slot%windowSlots)%count)*0x400u+(address&0x3FFu);
}

std::uint8_t Mapper005::ppuRead(std::uint16_t address) {
    return chr_[chrOffset(address,false,false)];
}
std::uint8_t Mapper005::ppuReadBackground(std::uint16_t address, bool sprites16) {
    return chr_[chrOffset(address,true,sprites16)];
}
std::uint8_t Mapper005::ppuReadSprite(std::uint16_t address, bool sprites16) {
    return chr_[chrOffset(address,false,sprites16)];
}
void Mapper005::ppuWrite(std::uint16_t address, std::uint8_t value) {
    if (chrRam_) chr_[chrOffset(address,false,false)]=value;
}

std::uint8_t Mapper005::nametableSource(std::uint16_t address) const {
    const unsigned table=((address&0x0FFFu)>>10u);
    return (ntMap_>>(table*2u))&3u;
}

std::uint8_t Mapper005::ppuNametableRead(std::uint16_t address, std::uint8_t* vram,
                                         bool backgroundFetch) {
    const unsigned offset=address&0x3FFu;
    const bool attribute=offset>=0x3C0u;
    if (backgroundFetch && splitActive_) {
        const unsigned y=static_cast<unsigned>((splitLine_+splitScroll_)%240);
        const unsigned x=splitColumn_&31u;
        if (!attribute) return exRam_[((y/8u)*32u+x)&0x3FFu];
        const auto at=exRam_[0x3C0u+((y/32u)*8u+(x/4u))];
        const unsigned shift=((y/16u)&1u)*4u+((x/2u)&1u)*2u;
        return static_cast<std::uint8_t>(((at>>shift)&3u)*0x55u);
    }
    // In extended-attribute mode each background tile has its own palette and
    // CHR bank selection. The tile fetch latches the corresponding ExRAM byte.
    if (backgroundFetch && exRamMode_==1) {
        if (!attribute) extendedTile_=exRam_[offset];
        else return static_cast<std::uint8_t>((extendedTile_>>6u)*0x55u);
    }
    switch(nametableSource(address)) {
    case 0: return vram[offset];
    case 1: return vram[0x400u+offset];
    case 2: return exRamMode_<=1 ? exRam_[offset] : 0;
    default:
        if (!attribute) return fillTile_;
        return static_cast<std::uint8_t>(fillAttr_*0x55u);
    }
}

void Mapper005::ppuNametableWrite(std::uint16_t address, std::uint8_t value,
                                   std::uint8_t* vram) {
    const unsigned offset=address&0x3FFu;
    switch(nametableSource(address)) {
    case 0: vram[offset]=value; break;
    case 1: vram[0x400u+offset]=value; break;
    case 2: if (exRamMode_<=1) exRam_[offset]=value; break;
    default: break; // Fill-mode memory is read-only.
    }
}

// --- MMC5 clock domains ------------------------------------------------------
// The PPU calls this at the actual beam position. The IRQ compare is sampled
// at visible dot 4, matching the MMC5's nametable-fetch scanline boundary.
void Mapper005::ppuRenderTick(int line, int dot, bool rendering) {
    if (line==240 && dot==0) { inFrame_=false; irqPending_=false; irqScanline_=0; }
    if (!rendering || line<0 || line>=240) return;
    if (dot==4 && (lastScanline_!=line || lastDot_!=dot)) {
        if (!inFrame_) { irqScanline_=0; inFrame_=true; irqPending_=false; }
        else ++irqScanline_;
        if (irqTarget_ && irqScanline_==irqTarget_) irqPending_=true;
    }
    lastScanline_=line; lastDot_=dot;
}

void Mapper005::ppuBackgroundTile(unsigned column, int line) {
    splitColumn_=column;
    splitLine_=line;
    const unsigned threshold=splitControl_&0x1Fu;
    splitActive_=line>=0 && line<240 && column<32 && (splitControl_&0x80u) &&
                 ((splitControl_&0x40u) ? column>=threshold : column<threshold);
}

std::uint8_t Mapper005::Pulse::output() const {
    static constexpr std::uint8_t dutyTable[4][8]={
        {0,1,0,0,0,0,0,0},{0,1,1,0,0,0,0,0},
        {0,1,1,1,1,0,0,0},{1,0,0,1,1,1,1,1}};
    if (!enabled || !length || !dutyTable[duty&3u][step&7u]) return 0;
    return constant ? volume : envDecay;
}
void Mapper005::Pulse::clockTimer() {
    if (!timer) { timer=period; step=static_cast<std::uint8_t>((step+1u)&7u); }
    else --timer;
}
void Mapper005::Pulse::quarterFrame() {
    if (envStart) { envStart=false; envDecay=15; envDivider=volume; }
    else if (!envDivider) {
        envDivider=volume;
        if (envDecay) --envDecay;
        else if (halt) envDecay=15;
    } else --envDivider;
}
void Mapper005::Pulse::halfFrame() { if (!halt && length) --length; }

void Mapper005::pcmWrite(std::uint8_t value) {
    if (value) pcmLevel_=value;
    else pcmIrqPending_=true;
}

void Mapper005::tickCpu() {
    // MMC5 pulse timer uses the APU's half-rate pulse clock.
    if ((audioCycle_&1u)==0u) for (auto& pulse:pulses_) pulse.clockTimer();
    ++audioCycle_;
    // NTSC 2A03 frame sequencer, aligned to the CPU master clock.
    if (audioCycle_==7457u || audioCycle_==14913u ||
        audioCycle_==22371u || audioCycle_==29829u)
        for (auto& pulse:pulses_) pulse.quarterFrame();
    if (audioCycle_==14913u || audioCycle_==29829u)
        for (auto& pulse:pulses_) pulse.halfFrame();
    if (audioCycle_>=29830u) audioCycle_=0;
}

float Mapper005::expansionAudio() const {
    const float pulse=static_cast<float>(pulses_[0].output()+pulses_[1].output());
    // MMC5 outputs enter the NES analog mixer with inverted polarity.
    const float sq=pulse>0.0f ? static_cast<float>(95.88/(8128.0/pulse+100.0)) : 0.0f;
    return -(sq + static_cast<float>(pcmLevel_)/255.0f*0.25f);
}

} // namespace frantastic::nes

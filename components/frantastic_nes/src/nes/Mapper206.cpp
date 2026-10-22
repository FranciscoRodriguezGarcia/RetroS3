#include "frantastic/nes/Mapper206.hpp"
#include <algorithm>

namespace frantastic::nes {
Mapper206::Mapper206(const RomImage& image)
    : prg_(image.prg),
      chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
      chrRam_(image.chr.empty()),mirroring_(image.mirroring) {
    if (prg_.size()<0x8000u || prg_.size()%0x2000u || chr_.size()<0x2000u || chr_.size()%0x400u)
        throw RomFormatError("NES: Namco 108 requires aligned PRG and CHR banks");
    reset();
}
void Mapper206::reset() { bank_.fill(0); selected_=0; }
std::uint8_t Mapper206::cpuPeek(std::uint16_t a,std::uint8_t bus) const {
    if (a<0x8000u) return bus;
    const std::size_t count=prg_.size()/0x2000u;
    const unsigned slot=(a-0x8000u)>>13;
    const std::size_t bank=slot==3 ? count-1 : slot==2 ? count-2 : (bank_[slot+6u]&0x0Fu)%count;
    return prg_[bank*0x2000u+(a&0x1FFFu)];
}
void Mapper206::cpuWrite(std::uint16_t a,std::uint8_t v) {
    if (a<0x8000u || a>=0xA000u) return;
    if ((a&1u)==0) selected_=v&7u;
    else bank_[selected_]=v&0x3Fu;
}
std::size_t Mapper206::chrOffset(std::uint16_t a) const {
    const unsigned slot=(a&0x1FFFu)>>10;
    const unsigned bank=slot<2u ? (bank_[0]&0x3Eu)+slot
                       :slot<4u ? (bank_[1]&0x3Eu)+(slot-2u)
                       :bank_[slot-2u]&0x3Fu;
    return (static_cast<std::size_t>(bank)%(chr_.size()/0x400u))*0x400u+(a&0x3FFu);
}
std::uint8_t Mapper206::ppuRead(std::uint16_t a) { return chr_[chrOffset(a)]; }
void Mapper206::ppuWrite(std::uint16_t a,std::uint8_t v) { if(chrRam_) chr_[chrOffset(a)]=v; }
} // namespace frantastic::nes

#include "frantastic/nes/MapperBandai.hpp"
#include <algorithm>

namespace frantastic::nes {
MapperBandai::MapperBandai(const RomImage& image)
 : id_(image.mapper),submapper_(image.submapper),prg_(image.prg),
   chr_(image.chr.empty()?std::vector<std::uint8_t>(std::max<std::size_t>(image.chrRamSize,0x2000u),0):image.chr),
   wram_(0x2000u,0),chrRam_(image.chr.empty()),hasWram_(id_==153),
   fcg_(id_==16 && image.format==RomFormat::Nes20 && image.submapper==4),
   fourScreen_(image.mirroring==Mirroring::FourScreen),mirroring_(image.mirroring),
   hasEeprom_(id_==159 || (id_==16 && (image.submapper==1 || (image.submapper!=4 && image.battery)))),
   x24c01_(id_==159 || (id_==16 && image.submapper==1)) {
    if(prg_.size()<0x8000u || prg_.size()%0x4000u || chr_.size()%0x400u)
        throw RomFormatError("NES: Bandai board needs 16 KiB PRG and 1 KiB CHR alignment");
    eeprom_.fill(0xFFu);
    reset();
}
void MapperBandai::reset() {
    chrBank_.fill(0);prgBank_=outerBank_=0;irqLatch_=irqCounter_=0;
    irqEnabled_=irqPending_=ramEnabled_=false;
    scl_=false;sda_=true;readDirection_=false;driveLow_=ack_=reading_=expectAck_=false;
    bits_=stage_=0;received_=shift_=address_=0;
}
bool MapperBandai::inRegisters(std::uint16_t a) const {
    if(id_!=16) return a>=0x8000u;
    if(submapper_==4 && fcg_) return a>=0x6000u && a<0x8000u;
    if(submapper_==5 || submapper_==1) return a>=0x8000u;
    return a>=0x6000u; // Legacy iNES dump: accept both windows.
}
std::uint8_t MapperBandai::cpuPeek(std::uint16_t a,std::uint8_t bus) const {
    if(a>=0x8000u) {
        const std::size_t count=prg_.size()/0x4000u;
        if(id_==153) {
            const std::size_t base=(static_cast<std::size_t>(outerBank_)*16u)%count;
            const std::size_t bank=(base+((a<0xC000u)?prgBank_&0x0Fu:15u))%count;
            return prg_[bank*0x4000u+(a&0x3FFFu)];
        }
        const std::size_t bank=(a<0xC000u ? prgBank_%count : count-1u);
        return prg_[bank*0x4000u+(a&0x3FFFu)];
    }
    if(a>=0x6000u && hasWram_ && ramEnabled_) return wram_[a&0x1FFFu];
    if(a>=0x6000u && hasEeprom_) {
        // EEPROM SDA is sampled on CPU data bit 4.
        return static_cast<std::uint8_t>((bus&~0x10u)|((driveLow_?0u:1u)<<4));
    }
    return bus;
}
void MapperBandai::eepromByte(std::uint8_t v) {
    if(x24c01_) {
        if(stage_==0) {address_=static_cast<std::uint8_t>((v>>1)&0x7Fu);reading_=(v&1u)!=0;stage_=reading_?3u:2u;}
        else if(stage_==2) {eeprom_[address_++&0x7Fu]=v;}
    } else {
        if(stage_==0) {if((v&0xF0u)!=0xA0u) {stage_=4;return;} reading_=(v&1u)!=0;stage_=reading_?3u:1u;}
        else if(stage_==1) {address_=v;stage_=2;}
        else if(stage_==2) {eeprom_[address_++]=v;}
    }
    if(reading_) shift_=eeprom_[address_&(x24c01_?0x7Fu:0xFFu)];
}
void MapperBandai::eepromWriteLines(std::uint8_t value) {
    const bool clock=(value&0x20u)!=0;
    const bool hostSda=(value&0x40u)!=0;
    const bool direction=(value&0x80u)!=0;
    // START/STOP are master SDA transitions while SCL is high.
    if(scl_ && clock && !readDirection_ && !direction) {
        if(sda_ && !hostSda) {
            stage_=0;bits_=0;received_=0;ack_=reading_=expectAck_=driveLow_=false;
        } else if(!sda_ && hostSda) {
            stage_=0;bits_=0;ack_=reading_=expectAck_=driveLow_=false;
        }
    }
    if(scl_ && !clock) {
        if(ack_) driveLow_=true;
        else if(reading_ && !expectAck_) driveLow_=(shift_&(0x80u>>bits_))==0;
        else driveLow_=false;
    }
    if(!scl_ && clock) {
        if(ack_) {
            ack_=false;driveLow_=false;
        } else if(expectAck_) {
            expectAck_=false;driveLow_=false;
            if(!hostSda) {
                ++address_;
                shift_=eeprom_[address_&(x24c01_?0x7Fu:0xFFu)];
                bits_=0;
            } else {reading_=false;stage_=4;}
        } else if(reading_) {
            if(++bits_==8u) {bits_=0;expectAck_=true;}
        } else if(stage_!=4u) {
            received_=static_cast<std::uint8_t>((received_<<1)|(hostSda?1u:0u));
            if(++bits_==8u) {
                eepromByte(received_);received_=0;bits_=0;ack_=true;
            }
        }
    }
    scl_=clock;sda_=hostSda;readDirection_=direction;
}
void MapperBandai::cpuWrite(std::uint16_t a,std::uint8_t v) {
    if(hasWram_ && a>=0x6000u && a<0x8000u) {
        if(ramEnabled_) wram_[a&0x1FFFu]=v;
        return;
    }
    if(!inRegisters(a)) return;
    switch(a&0x0Fu) {
    case 0:case 1:case 2:case 3:
        if(id_==153) outerBank_=v&1u;
        else chrBank_[a&7u]=v;
        break;
    case 4:case 5:case 6:case 7:
        if(id_!=153) chrBank_[a&7u]=v;
        break;
    case 8:prgBank_=v&0x0Fu;break;
    case 9:
        if(!fourScreen_) switch(v&3u) {
            case 0:mirroring_=Mirroring::Vertical;break;
            case 1:mirroring_=Mirroring::Horizontal;break;
            case 2:mirroring_=Mirroring::SingleScreenLow;break;
            default:mirroring_=Mirroring::SingleScreenHigh;break;
        }
        break;
    case 10:
        irqPending_=false;irqEnabled_=(v&1u)!=0;
        if(irqEnabled_ && !fcg_) irqCounter_=irqLatch_;
        break;
    case 11:irqLatch_=static_cast<std::uint16_t>((irqLatch_&0xFF00u)|v);
        if(fcg_) { irqCounter_=static_cast<std::uint16_t>((irqCounter_&0xFF00u)|v); }
        break;
    case 12:irqLatch_=static_cast<std::uint16_t>((irqLatch_&0x00FFu)|(v<<8));
        if(fcg_) { irqCounter_=static_cast<std::uint16_t>((irqCounter_&0x00FFu)|(v<<8)); }
        break;
    case 13:
        if(hasWram_) ramEnabled_=(v&0x20u)!=0;
        else if(hasEeprom_) eepromWriteLines(v);
        break;
    default:break;
    }
}
std::uint8_t MapperBandai::ppuRead(std::uint16_t a) {
    const std::size_t count=chr_.size()/0x400u;
    const std::size_t bank=id_==153 ? (a&0x1FFFu)>>10 : chrBank_[(a>>10)&7u];
    return chr_[(bank%count)*0x400u+(a&0x3FFu)];
}
void MapperBandai::ppuWrite(std::uint16_t a,std::uint8_t v) {
    if(!chrRam_) return;
    const std::size_t count=chr_.size()/0x400u;
    const std::size_t bank=id_==153 ? (a&0x1FFFu)>>10 : chrBank_[(a>>10)&7u];
    chr_[(bank%count)*0x400u+(a&0x3FFu)]=v;
}
void MapperBandai::tickCpu() {
    if(!irqEnabled_) return;
    if(irqCounter_==0) {irqPending_=true;irqEnabled_=false;}
    else --irqCounter_;
}
} // namespace frantastic::nes

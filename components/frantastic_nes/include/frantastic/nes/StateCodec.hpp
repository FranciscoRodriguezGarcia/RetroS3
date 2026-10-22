#pragma once
#include "frantastic/nes/NES.hpp"
#include "frantastic/StateArchive.hpp"
#include "frantastic/nes/Mapper000.hpp"
#include "frantastic/nes/Mapper001.hpp"
#include "frantastic/nes/Mapper004.hpp"
#include "frantastic/nes/Mapper005.hpp"
#include "frantastic/nes/MapperDiscrete.hpp"
#include "frantastic/nes/MapperMmcLatch.hpp"
#include "frantastic/nes/Mapper069.hpp"
#include "frantastic/nes/MapperVrc6.hpp"
#include "frantastic/nes/MapperVrc24.hpp"
#include "frantastic/nes/Mapper019.hpp"
#include "frantastic/nes/Mapper091.hpp"
#include "frantastic/nes/Mapper210.hpp"
#include "frantastic/nes/MapperJalecoTaito.hpp"
#include "frantastic/nes/Mapper206.hpp"
#include "frantastic/nes/MapperBandai.hpp"
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace frantastic::nes {


// Serialized hardware only: pointers, ROM data, host callbacks and SDL resources stay live.
class NesStateCodec {
public:
    static std::uint64_t fingerprint(const std::vector<std::uint8_t>& rom) {
        return state_detail::hash(rom);
    }
    static std::vector<std::uint8_t> save(NES& nes, std::uint64_t romFingerprint) {
        std::vector<std::uint8_t> payload;
        state_detail::Archive ar(payload);
        transfer(ar, nes, 5);
        std::vector<std::uint8_t> file;
        state_detail::Archive header(file);
        std::uint32_t magic = 0x31534E52u, version = 5;
        std::uint32_t mapper = nes.cartridge_->mapper().id();
        std::uint32_t size = static_cast<std::uint32_t>(payload.size());
        std::uint64_t checksum = state_detail::hash(payload);
        header.run(magic, version, romFingerprint, mapper, size, checksum);
        file.insert(file.end(), payload.begin(), payload.end());
        return file;
    }
    static void load(NES& nes, const std::vector<std::uint8_t>& file,
                     std::uint64_t romFingerprint) {
        if (file.size() < 32) throw std::runtime_error("NES state: header missing");
        auto copy = file;
        state_detail::Archive header(copy, true);
        std::uint32_t magic = 0, version = 0, mapper = 0, size = 0;
        std::uint64_t rom = 0, checksum = 0;
        header.run(magic, version, rom, mapper, size, checksum);
        if (magic != 0x31534E52u || (version != 2 && version != 3 && version != 4 && version != 5))
            throw std::runtime_error("NES state: unsupported format");
        if (rom != romFingerprint) throw std::runtime_error("NES state: different ROM");
        if (mapper != nes.cartridge_->mapper().id())
            throw std::runtime_error("NES state: different mapper");
        if (size > 4 * 1024 * 1024 || size != file.size() - 32)
            throw std::runtime_error("NES state: invalid size");
        std::vector<std::uint8_t> payload(file.begin() + 32, file.end());
        if (checksum != state_detail::hash(payload))
            throw std::runtime_error("NES state: checksum failed");
        const auto backup = save(nes, romFingerprint);
        try {
            state_detail::Archive ar(payload, true);
            transfer(ar, nes, version);
            if (ar.position() != payload.size())
                throw std::runtime_error("NES state: trailing data");
        } catch (...) {
            std::vector<std::uint8_t> original(backup.begin() + 32, backup.end());
            state_detail::Archive rollback(original, true);
            transfer(rollback, nes, 5);
            throw;
        }
        nes.apu_.discardSamples();
    }
private:
    static void transfer(state_detail::Archive& ar, NES& n, std::uint32_t version) {
        auto& c = n.cpu_;
        ar.run(c.pc, c.a, c.x, c.y, c.s, c.p, c.cycles_, c.instructions_,
               c.pollNow_.nmi, c.pollNow_.irq, c.pollPrevious_.nmi,
               c.pollPrevious_.irq, c.nmiRequested_, c.irqRequested_);
        auto& b = n.bus_;
        ar.run(b.ram_, b.openBus_, b.cycles_, b.dmaCycles_, b.nmiLine_,
               b.nmiLatch_, b.oamDmaPending_, b.oamDmaPage_, b.inDma_);
        auto& p = n.ppu_;
        ar.run(p.vram_, p.palette_, p.oam_, p.index_, p.rgba_,
               p.ctrl_, p.mask_, p.oamAddr_, p.vblank_, p.spriteZeroHit_,
               p.spriteOverflow_, p.v_, p.t_, p.fineX_, p.w_, p.readBuffer_,
               p.openBus_, p.openBusRefreshFrame_, p.suppressVblank_, p.warmup_,
               p.scanline_, p.dot_, p.oddFrame_, p.oddRendering_, p.frame_,
               p.frameReady_, p.ntLatch_, p.atLatch_, p.patLoLatch_,
               p.patHiLatch_, p.bgShiftLo_, p.bgShiftHi_, p.atShiftLo_, p.atShiftHi_);
        for (auto& sprite : p.sprites_)
            ar.run(sprite.lo, sprite.hi, sprite.attr, sprite.x, sprite.zero);
        ar.run(p.spriteCount_, p.secondaryOam_, p.secondaryCount_, p.secondaryHasZero_);
        auto& a = n.apu_;
        for (auto* pulse : {&a.pulse1_, &a.pulse2_}) {
            ar.run(pulse->enabled, pulse->ones, pulse->duty, pulse->step, pulse->length,
                   pulse->period, pulse->timer, pulse->env.start, pulse->env.loop,
                   pulse->env.constant, pulse->env.period, pulse->env.divider,
                   pulse->env.decay, pulse->sweepEnabled, pulse->sweepNegate,
                   pulse->sweepReload, pulse->sweepPeriod, pulse->sweepShift,
                   pulse->sweepDivider);
        }
        auto& t = a.triangle_;
        ar.run(t.enabled, t.control, t.reloadFlag, t.length, t.linear,
               t.linearReload, t.step, t.period, t.timer);
        auto& noise = a.noise_;
        ar.run(noise.enabled, noise.mode, noise.length, noise.period,
               noise.timer, noise.shift, noise.env.start, noise.env.loop,
               noise.env.constant, noise.env.period, noise.env.divider, noise.env.decay);
        auto& d = a.dmc_;
        ar.run(d.enabled, d.irqEnable, d.loop, d.irq, d.rate, d.timer,
               d.level, d.sampleAddress, d.sampleLength, d.address, d.remaining,
               d.buffer, d.bufferFull, d.dmaRequest, d.shift, d.bitsLeft, d.silence);
        ar.run(a.fiveStep_, a.irqInhibit_, a.frameIrq_, a.frameCycle_,
               a.frameResetDelay_, a.pendingFrameValue_, a.apuCycle_,
               a.cpuCycles_, a.sampleRate_, a.cyclesPerSample_, a.sampleClock_,
               a.accumulator_, a.accumulated_, a.hp90Prev_, a.hp90Out_,
               a.hp440Prev_, a.hp440Out_, a.lpOut_);
        for (auto* pad : {&n.port1_, &n.port2_})
            ar.run(pad->buttons_, pad->shift_, pad->strobe_);
        ar.value(n.frames_);
        auto& mapper = n.cartridge_->mapper();
        switch (mapper.id()) {
            case 0: {
                auto& m = static_cast<Mapper000&>(mapper);
                ar.run(m.prgRam_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 1: {
                auto& m = static_cast<Mapper001&>(mapper);
                ar.run(m.prgRam_, m.shift_, m.control_, m.chr0_, m.chr1_, m.prgBank_, m.lastWriteCycle_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 2: {
                auto& m = static_cast<Mapper002&>(mapper);
                ar.value(m.bank_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 3: {
                auto& m = static_cast<Mapper003&>(mapper);
                ar.value(m.bank_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 4: {
                auto& m = static_cast<Mapper004&>(mapper);
                ar.run(m.prgRam_, m.mirroring_, m.banks_, m.bankSelect_,
                       m.ramProtect_, m.irqLatch_, m.irqCounter_, m.irqReload_,
                       m.irqEnabled_, m.irqPending_, m.lastA12_, m.lastLowDot_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 5: {
                auto& m = static_cast<Mapper005&>(mapper);
                ar.run(m.ram_, m.exRam_, m.prgRegs_, m.chrRegs_,
                       m.prgMode_, m.chrMode_, m.ramProtect1_, m.ramProtect2_,
                       m.exRamMode_, m.ntMap_, m.fillTile_, m.fillAttr_, m.chrHigh_,
                       m.multiplierA_, m.multiplierB_, m.lastChrBg_);
                if (version >= 4) ar.value(m.extendedTile_);
                else m.extendedTile_ = 0;
                if (version >= 5) {
                    ar.run(m.irqTarget_, m.irqScanline_, m.irqEnable_, m.irqPending_, m.inFrame_,
                           m.lastScanline_, m.lastDot_, m.splitControl_, m.splitScroll_,
                           m.splitBank_, m.splitActive_, m.splitColumn_, m.splitLine_,
                           m.pcmLevel_, m.pcmReadMode_, m.pcmIrqEnable_, m.pcmIrqPending_,
                           m.audioCycle_);
                    for (auto& p : m.pulses_)
                        ar.run(p.duty, p.volume, p.length, p.step, p.envDivider, p.envDecay,
                               p.period, p.timer, p.enabled, p.halt, p.constant, p.envStart);
                } else {
                    m.irqTarget_=m.irqScanline_=0;
                    m.irqEnable_=m.irqPending_=m.inFrame_=false;
                    m.lastScanline_=m.lastDot_=-1;
                    m.splitControl_=m.splitScroll_=m.splitBank_=0;
                    m.splitActive_=false; m.splitColumn_=0; m.splitLine_=0;
                    m.pcmLevel_=0; m.pcmReadMode_=m.pcmIrqEnable_=m.pcmIrqPending_=false;
                    m.audioCycle_=0; m.pulses_={};
                }
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 7: {
                auto& m = static_cast<Mapper007&>(mapper);
                ar.run(m.bank_, m.singleHigh_, m.chr_);
                break;
            }
            case 9:
            case 10: {
                auto& m = static_cast<MapperMmcLatch&>(mapper);
                ar.run(m.prgBank_, m.chrBanks_, m.latchFe_, m.mirroring_);
                if (m.mmc4_) ar.value(m.prgRam_);
                break;
            }
            case 34: {
                auto& m = static_cast<Mapper034&>(mapper);
                ar.run(m.prgBank_, m.chrBank0_, m.chrBank1_);
                if (m.nina_) ar.value(m.prgRam_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 66: {
                auto& m = static_cast<Mapper066&>(mapper);
                ar.run(m.prgBank_, m.chrBank_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 19: {
                auto& m = static_cast<Mapper019&>(mapper);
                ar.run(m.ram_, m.chrBanks_, m.prgBanks_, m.soundRam_, m.soundAddress_,
                       m.ramControl_, m.prgControl_, m.chrControl_, m.auxControl_,
                       m.irqCounter_, m.irqEnabled_, m.irqPending_, m.soundDivider_,
                       m.soundChannel_, m.soundOutput_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 21:
            case 22:
            case 23:
            case 25: {
                auto& m = static_cast<MapperVrc24&>(mapper);
                ar.run(m.ram_, m.mirroring_, m.chrBanks_, m.prg0_, m.prg1_,
                       m.prgMode_, m.ramEnabled_, m.latch_, m.irqLatch_,
                       m.irqCounter_, m.irqControl_, m.irqPrescaler_,
                       m.irqEnabled_, m.irqPending_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 24:
            case 26: {
                auto& m = static_cast<MapperVrc6&>(mapper);
                ar.run(m.ram_, m.mirroring_, m.prg16_, m.prg8_, m.banking_, m.chrBanks_,
                       m.sawRegs_, m.sawDivider_, m.sawStep_, m.sawAccumulator_,
                       m.audioControl_, m.irqLatch_, m.irqCounter_, m.irqControl_,
                       m.irqPrescaler_, m.irqEnabled_, m.irqPending_);
                for (auto& pulse : m.pulses_) ar.run(pulse.regs, pulse.divider, pulse.phase);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 69: {
                auto& m = static_cast<Mapper069&>(mapper);
                ar.run(m.ram_, m.mirroring_, m.chrBanks_, m.prgBanks_, m.command_,
                       m.ramSelect_, m.irqControl_, m.irqCounter_, m.irqPending_,
                       m.soundSelect_, m.soundRegs_, m.toneTimers_, m.toneOutput_,
                       m.noiseTimer_, m.noiseShift_, m.noiseOutput_, m.envelopeTimer_,
                       m.envelopeStep_, m.envelopeHolding_, m.envelopeAttack_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 16:
            case 153:
            case 159: {
                auto& m = static_cast<MapperBandai&>(mapper);
                ar.run(m.mirroring_,m.chrBank_,m.prgBank_,m.outerBank_,m.irqLatch_,m.irqCounter_,
                       m.irqEnabled_,m.irqPending_,m.ramEnabled_,m.eeprom_,m.scl_,m.sda_,
                       m.readDirection_,m.driveLow_,m.ack_,m.reading_,m.expectAck_,
                       m.bits_,m.stage_,m.received_,m.shift_,m.address_);
                if(m.hasWram_) ar.value(m.wram_);
                if(m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 18:
            case 33: {
                auto& m=static_cast<MapperJalecoTaito&>(mapper);
                ar.run(m.ram_,m.mirroring_,m.prgBanks_,m.chrBanks_,
                       m.ramEnabled_,m.ramWritable_,m.irqLatch_,m.irqCounter_,
                       m.irqControl_,m.irqPending_);
                if(m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 91: {
                auto& m = static_cast<Mapper091&>(mapper);
                ar.run(m.prgBanks_,m.chrBanks_,m.mirroring_,m.outerBank_,m.ppuCounter_,
                       m.cpuPhase_,m.cpuCounter_,m.irqEnabled_,m.irqPending_,m.lastA12_);
                if(m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 210: {
                auto& m = static_cast<Mapper210&>(mapper);
                ar.run(m.ram_,m.prgBanks_,m.chrBanks_,m.mirroring_,m.ramEnabled_);
                if(m.chrRam_) ar.value(m.chr_);
                break;
            }
            case 206: {
                auto& m = static_cast<Mapper206&>(mapper);
                ar.run(m.bank_, m.selected_);
                if (m.chrRam_) ar.value(m.chr_);
                break;
            }
            default: throw std::runtime_error("NES state: mapper unsupported");
        }
        // v3 appends the CPU's JAM/KIL latch. v2 snapshots precede it.
        if (version >= 3) ar.value(c.halted_);
        else c.halted_ = false;
    }
};
} // namespace frantastic::nes

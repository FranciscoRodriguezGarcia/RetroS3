#include "frantastic/nes/Bus.hpp"
#include "frantastic/nes/APU.hpp"
#include "frantastic/nes/Controller.hpp"
#include "frantastic/nes/Mapper.hpp"
#include "frantastic/nes/PPU.hpp"
#include <cstdio>

namespace frantastic::nes {

Bus::Bus(PPU& ppu, APU& apu, Controller& port1, Controller& port2)
    : ppu_(ppu), apu_(apu), port1_(port1), port2_(port2) {}

void Bus::powerOn() {
    ram_.fill(0);
    openBus_ = 0;
    cycles_ = dmaCycles_ = 0;
    nmiLine_ = nmiLatch_ = false;
    nmiEdges_ = nmiAcknowledged_ = 0;
    debugSignal1aWrites_ = debugClear1aWrites_ = 0;
    debugEnemyWrites_ = debugEnemyChangedWrites_ = debugEnemyPositionChanges_ = 0;
    oamDmaPending_ = inDma_ = false;
}

void Bus::reset() {
    nmiLine_ = nmiLatch_ = false;
    oamDmaPending_ = inDma_ = false;
}

bool Bus::irqAsserted() const { return apu_.irqAsserted() || (mapper_ && mapper_->irqAsserted()); }

// One CPU cycle is three PPU dots. Two dots elapse before the bus access and
// one after it; this phase alignment is what blargg's ppu_vbl_nmi and
// cpu_interrupts timing tests measure. At the end of the cycle the PPU's /NMI
// line is sampled; the CPU latches NMI on its falling edge, modelled here as
// a rising edge of nmiOutput().
void Bus::tick() {
    ++cycles_;
    ppu_.tick();
    ppu_.tick();
    if (mapper_) mapper_->tickCpu();
    apu_.tick();
}

void Bus::endCycle() {
    ppu_.tick();
    const bool line = ppu_.nmiOutput();
    if (line && !nmiLine_) { nmiLatch_ = true; ++nmiEdges_; }
    nmiLine_ = line;
}

void Bus::idleCycle() {
    tick();
    endCycle();
}

// Get/put cycles follow the APU clock halves, not CPU cycle parity.
// DMA units take over on a CPU read cycle (the 2A03 can only halt the 6502
// on reads). OAM DMA: one halt cycle, an optional alignment cycle so reads
// fall on "get" cycles, then 256 read/write pairs = 513 or 514 cycles. DMC
// sample fetch: halt, dummy, optional alignment, read = 3-4 cycles.
void Bus::runPendingDma() {
    inDma_ = true;
    if (apu_.dmcDmaRequested()) {
        const std::uint64_t start = cycles_;
        idleCycle();                                // halt
        idleCycle();                                // dummy
        if (apu_.nextCycleIsPut()) idleCycle();     // alignment
        tick();
        apu_.dmcDmaComplete(readNoDma(apu_.dmcDmaAddress()));
        endCycle();
        dmaCycles_ += cycles_ - start;
    }
    if (oamDmaPending_) {
        oamDmaPending_ = false;
        const std::uint64_t start = cycles_;
        idleCycle();                                // halt
        if (apu_.nextCycleIsPut()) idleCycle();     // alignment
        const auto base = static_cast<std::uint16_t>(oamDmaPage_ << 8);
        for (unsigned i = 0; i < 256; ++i) {
            tick();
            const std::uint8_t value = readNoDma(static_cast<std::uint16_t>(base + i));
            endCycle();
            tick();
            ppu_.oamDmaWrite(value);
            endCycle();
        }
        dmaCycles_ += cycles_ - start;
    }
    inDma_ = false;
}

std::uint8_t Bus::read(std::uint16_t address) {
    if (!inDma_ && (oamDmaPending_ || apu_.dmcDmaRequested())) runPendingDma();
    tick();
    const std::uint8_t v = readNoDma(address);
    if (diagnosticHook_) diagnosticHook_(false, address, v, cycles_);
    endCycle();
    return v;
}

std::uint8_t Bus::readNoDma(std::uint16_t address) {
    std::uint8_t value = openBus_;
    if (address < 0x2000u) {
        value = ram_[address & 0x07FFu];
    } else if (address < 0x4000u) {
        value = ppu_.cpuRead(address & 7u);
    } else if (address == 0x4015u) {
        // Bit 5 is not driven; the internal status read does not update the
        // external data bus.
        return static_cast<std::uint8_t>(apu_.readStatus() | (openBus_ & 0x20u));
    } else if (address == 0x4016u) {
        value = static_cast<std::uint8_t>((openBus_ & 0xE0u) | port1_.read());
    } else if (address == 0x4017u) {
        value = static_cast<std::uint8_t>((openBus_ & 0xE0u) | port2_.read());
    } else if (address < 0x4020u) {
        if (address >= 0x4018u) logUnsupported("read", address, 0);
        value = openBus_;  // Write-only APU registers and disabled test mode.
    } else if (mapper_) {
        value = mapper_->cpuRead(address, openBus_);
    }
    openBus_ = value;
    return value;
}

void Bus::write(std::uint16_t address, std::uint8_t value) {
    tick();
    struct End { Bus* b; ~End() { b->endCycle(); } } end{this};
    openBus_ = value;
    if (address < 0x2000u && (address & 0x07FFu) == 0x001Au) {
        if (value == 0) ++debugClear1aWrites_;
        else ++debugSignal1aWrites_;
    }
    if (diagnosticHook_) diagnosticHook_(true, address, value, cycles_);
    if (address < 0x2000u) {
        const unsigned ramAddress = address & 0x07FFu;
        if (ramAddress >= 0x0400u && ramAddress < 0x0500u) {
            ++debugEnemyWrites_;
            if (ram_[ramAddress] != value) {
                ++debugEnemyChangedWrites_;
                if ((ramAddress & 0x0Fu) <= 1u) ++debugEnemyPositionChanges_;
            }
        }
        ram_[ramAddress] = value;
    } else if (address < 0x4000u) {
        ppu_.cpuWrite(address & 7u, value);
    } else if (address == 0x4014u) {
        oamDmaPending_ = true;
        oamDmaPage_ = value;
    } else if (address == 0x4016u) {
        port1_.writeStrobe(value & 1u);
        port2_.writeStrobe(value & 1u);
    } else if (address <= 0x4017u) {
        apu_.writeRegister(address, value);
    } else if (address < 0x4020u) {
        logUnsupported("write", address, value);
    } else if (mapper_) {
        mapper_->cpuWriteTimed(address, value, cycles_);
    }
}

std::uint8_t Bus::peek(std::uint16_t address) const {
    if (address < 0x2000u) return ram_[address & 0x07FFu];
    if (address < 0x4000u) return ppu_.peekRegister(address & 7u);
    if (address == 0x4015u) return apu_.peekStatus();
    if (address == 0x4016u) return static_cast<std::uint8_t>((openBus_ & 0xE0u) | port1_.peek());
    if (address == 0x4017u) return static_cast<std::uint8_t>((openBus_ & 0xE0u) | port2_.peek());
    if (address < 0x4020u) return openBus_;
    return mapper_ ? mapper_->cpuPeek(address, openBus_) : openBus_;
}

void Bus::logUnsupported(const char* kind, std::uint16_t address, std::uint8_t value) {
    if (!logUnsupported_ || !log_) return;
    const std::uint32_t key = (kind[0] == 'w' ? 0x10000u : 0u) | address;
    if (!logged_.insert(key).second) return;
    char line[96];
    std::snprintf(line, sizeof line, "NES: unsupported %s at $%04X (value $%02X)", kind, address, value);
    log_(LogLevel::Warning, line);
}

} // namespace frantastic::nes

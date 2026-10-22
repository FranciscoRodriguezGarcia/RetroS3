#pragma once
#include "frantastic/nes/CPU6502.hpp"
#include "frantastic/nes/Log.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <set>

namespace frantastic::nes {

class PPU;
class APU;
class Controller;
class Mapper;

// NES CPU address space and master clock. Each CPU read/write is one CPU
// cycle: the bus first advances the PPU by three dots and the APU by one
// cycle, then performs the access. DMA units halt the CPU on its next read
// cycle, exactly where the 2A03 inserts them.
//
//   $0000-$07FF  2 KiB RAM          $0800-$1FFF RAM mirrors
//   $2000-$2007  PPU registers      $2008-$3FFF PPU mirrors (every 8 bytes)
//   $4000-$4017  APU / OAM DMA / controller ports
//   $4018-$401F  CPU test mode (disabled; open bus)
//   $4020-$FFFF  Cartridge
class Bus final : public CpuBus {
    friend class NesStateCodec;
public:
    Bus(PPU& ppu, APU& apu, Controller& port1, Controller& port2);
    void attachMapper(Mapper* mapper) { mapper_ = mapper; }

    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t peek(std::uint16_t address) const override;
    bool nmiPending() const override { return nmiLatch_; }
    void acknowledgeNmi() override { nmiLatch_ = false; ++nmiAcknowledged_; }
    bool irqAsserted() const override;

    void powerOn();
    void reset();
    std::uint64_t cycles() const { return cycles_; }
    std::uint64_t dmaStallCycles() const { return dmaCycles_; }
    std::uint8_t lastOamDmaPage() const { return oamDmaPage_; }
    // Diagnostic counters for the Metroid NMI/main-loop handshake at zero-page $1A.
    std::uint64_t debugSignal1aWrites() const { return debugSignal1aWrites_; }
    std::uint64_t debugClear1aWrites() const { return debugClear1aWrites_; }
    std::uint64_t debugEnemyWrites() const { return debugEnemyWrites_; }
    std::uint64_t debugEnemyChangedWrites() const { return debugEnemyChangedWrites_; }
    std::uint64_t debugEnemyPositionChanges() const { return debugEnemyPositionChanges_; }
    std::uint64_t nmiEdges() const { return nmiEdges_; }
    std::uint64_t nmiAcknowledged() const { return nmiAcknowledged_; }
    std::uint8_t openBus() const { return openBus_; }
    std::array<std::uint8_t, 0x800>& ram() { return ram_; }

    using DiagnosticHook = std::function<void(bool, std::uint16_t, std::uint8_t, std::uint64_t)>;
    void setDiagnosticHook(DiagnosticHook hook) { diagnosticHook_ = std::move(hook); }
    void setLogSink(LogSink sink) { log_ = std::move(sink); }
    void setLogUnsupported(bool enabled) { logUnsupported_ = enabled; }

private:
    void tick();
    void endCycle();
    void idleCycle();
    void runPendingDma();
    std::uint8_t readNoDma(std::uint16_t address);
    void logUnsupported(const char* kind, std::uint16_t address, std::uint8_t value);

    PPU& ppu_;
    APU& apu_;
    Controller& port1_;
    Controller& port2_;
    Mapper* mapper_ = nullptr;
    std::array<std::uint8_t, 0x800> ram_{};
    std::uint8_t openBus_ = 0;
    std::uint64_t cycles_ = 0;
    std::uint64_t dmaCycles_ = 0;
    bool nmiLine_ = false, nmiLatch_ = false;
    std::uint64_t nmiEdges_ = 0, nmiAcknowledged_ = 0;
    std::uint64_t debugSignal1aWrites_ = 0, debugClear1aWrites_ = 0;
    std::uint64_t debugEnemyWrites_ = 0, debugEnemyChangedWrites_ = 0, debugEnemyPositionChanges_ = 0;
    bool oamDmaPending_ = false;
    std::uint8_t oamDmaPage_ = 0;
    bool inDma_ = false;
    LogSink log_;
    DiagnosticHook diagnosticHook_;
    bool logUnsupported_ = false;
    std::set<std::uint32_t> logged_;
};

} // namespace frantastic::nes

#pragma once
#include "frantastic/nes/APU.hpp"
#include "frantastic/nes/Bus.hpp"
#include "frantastic/nes/CPU6502.hpp"
#include "frantastic/nes/Cartridge.hpp"
#include "frantastic/nes/Controller.hpp"
#include "frantastic/nes/Log.hpp"
#include "frantastic/nes/PPU.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace frantastic::nes {

struct DebugState {
    std::uint16_t pc = 0;
    std::uint8_t a = 0, x = 0, y = 0, s = 0, p = 0;
    std::uint64_t instructions = 0, cpuCycles = 0, frame = 0;
    int scanline = 0, dot = 0;
};

// The console: wires cartridge, CPU, PPU, APU and controllers together and
// exposes a host-independent stepping API. No SDL or OS dependencies.
class NES {
    friend class NesStateCodec;
public:
    explicit NES(std::unique_ptr<Cartridge> cartridge, double audioSampleRate = 48000.0);
    static std::unique_ptr<NES> fromFile(const std::string& path, double audioSampleRate = 48000.0);

    void powerOn();
    void reset();
    unsigned stepInstruction();  // Returns CPU cycles including DMA stalls.
    // Runs until the PPU has produced the next complete picture (end of
    // scanline 239). The number of CPU instructions per frame is whatever
    // the emulated timing yields, never a fixed budget.
    void runFrame();

    void setButtons(unsigned port, std::uint8_t buttons);
    const std::array<std::uint32_t, PPU::Width * PPU::Height>& framebuffer() const { return ppu_.framebuffer(); }
    std::size_t readAudio(float* out, std::size_t max) { return apu_.readSamples(out, max); }

    CPU6502& cpu() { return cpu_; }
    PPU& ppu() { return ppu_; }
    APU& apu() { return apu_; }
    Bus& bus() { return bus_; }
    Cartridge& cartridge() { return *cartridge_; }
    Controller& controller(unsigned port) { return port ? port2_ : port1_; }
    DebugState debugState() const;
    std::uint64_t frameCount() const { return frames_; }

    // CPU trace: one line per instruction, including PPU scanline/dot.
    void setTraceSink(std::function<void(const std::string&)> sink);
    void setLogSink(LogSink sink) { bus_.setLogSink(std::move(sink)); }
    void setLogUnsupported(bool enabled) { bus_.setLogUnsupported(enabled); }

private:
    std::unique_ptr<Cartridge> cartridge_;
    PPU ppu_;
    APU apu_;
    Controller port1_, port2_;
    Bus bus_;
    CPU6502 cpu_;
    std::uint64_t frames_ = 0;
};

} // namespace frantastic::nes

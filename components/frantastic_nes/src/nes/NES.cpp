#include "frantastic/nes/NES.hpp"
#include <cstdio>
#include <stdexcept>

namespace frantastic::nes {

NES::NES(std::unique_ptr<Cartridge> cartridge, double audioSampleRate)
    : cartridge_(std::move(cartridge)), apu_(audioSampleRate), bus_(ppu_, apu_, port1_, port2_), cpu_(bus_) {
    ppu_.connect(&cartridge_->mapper());
    bus_.attachMapper(&cartridge_->mapper());
    apu_.setExpansionAudio([this]() { return cartridge_->mapper().expansionAudio(); });
    powerOn();
}

std::unique_ptr<NES> NES::fromFile(const std::string& path, double audioSampleRate) {
    return std::make_unique<NES>(Cartridge::fromFile(path), audioSampleRate);
}

void NES::powerOn() {
    ppu_.powerOn();
    apu_.powerOn();
    bus_.powerOn();
    port1_.reset();
    port2_.reset();
    cartridge_->reset();
    cpu_.powerOn();
    frames_ = 0;
}

void NES::reset() {
    ppu_.reset();
    apu_.reset();
    bus_.reset();
    cartridge_->reset();
    cpu_.reset();
}

unsigned NES::stepInstruction() {
    const std::uint64_t start = bus_.cycles();
    cpu_.step();
    return static_cast<unsigned>(bus_.cycles() - start);
}

void NES::runFrame() {
    ppu_.clearFrameReady();
    // A healthy NTSC PPU produces a frame within roughly 30k CPU cycles.
    // Do not hang the Android render thread indefinitely if a hardware
    // regression prevents the frame-ready transition.
    constexpr unsigned kMaximumInstructionsPerFrame = 200000;
    unsigned instructions = 0;
    while (!ppu_.frameReady()) {
        if (++instructions > kMaximumInstructionsPerFrame)
            throw std::runtime_error("NES: PPU frame-ready timeout");
        cpu_.step();
    }
    ++frames_;
}

void NES::setButtons(unsigned port, std::uint8_t buttons) { controller(port).setButtons(buttons); }

DebugState NES::debugState() const {
    DebugState d;
    d.pc = cpu_.pc;
    d.a = cpu_.a;
    d.x = cpu_.x;
    d.y = cpu_.y;
    d.s = cpu_.s;
    d.p = cpu_.p;
    d.instructions = cpu_.instructionCount();
    d.cpuCycles = bus_.cycles();
    d.frame = ppu_.frame();
    d.scanline = ppu_.scanline();
    d.dot = ppu_.dot();
    return d;
}

void NES::setTraceSink(std::function<void(const std::string&)> sink) {
    if (!sink) {
        cpu_.setTraceHook(nullptr);
        return;
    }
    cpu_.setTraceHook([this, sink](const CPU6502& cpu) {
        char suffix[64];
        std::snprintf(suffix, sizeof suffix, " PPU:%3d,%3d CYC:%llu", ppu_.scanline(), ppu_.dot(),
                      static_cast<unsigned long long>(bus_.cycles()));
        sink(cpu.traceLine() + suffix);
    });
}

} // namespace frantastic::nes

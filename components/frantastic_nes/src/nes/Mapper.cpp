#include "frantastic/nes/Mapper.hpp"

namespace frantastic::nes {

const char* mirroringName(Mirroring mirroring) {
    switch (mirroring) {
    case Mirroring::Horizontal: return "horizontal";
    case Mirroring::Vertical: return "vertical";
    case Mirroring::SingleScreenLow: return "single-screen (low)";
    case Mirroring::SingleScreenHigh: return "single-screen (high)";
    case Mirroring::FourScreen: return "four-screen";
    }
    return "unknown";
}

// Maps a PPU address in $2000-$3EFF to an offset into the console's 2 KiB
// CIRAM (or the 4 KiB on four-screen boards).
std::uint16_t nametableOffset(Mirroring mirroring, std::uint16_t address) {
    const std::uint16_t a = static_cast<std::uint16_t>((address - 0x2000u) & 0x0FFFu);
    const unsigned table = a >> 10;   // 0..3 logical nametable
    const unsigned offset = a & 0x3FFu;
    switch (mirroring) {
    case Mirroring::Horizontal: return static_cast<std::uint16_t>(((table >> 1) << 10) | offset);
    case Mirroring::Vertical: return static_cast<std::uint16_t>(((table & 1u) << 10) | offset);
    case Mirroring::SingleScreenLow: return static_cast<std::uint16_t>(offset);
    case Mirroring::SingleScreenHigh: return static_cast<std::uint16_t>(0x400u | offset);
    case Mirroring::FourScreen: return a;
    }
    return a;
}

} // namespace frantastic::nes

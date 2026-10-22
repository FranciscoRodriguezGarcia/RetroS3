#pragma once
#include <cstdint>

namespace frantastic::nes {

// Bit positions follow the 4021 shift-register order read through $4016/7.
namespace Button {
constexpr std::uint8_t A = 0x01;
constexpr std::uint8_t B = 0x02;
constexpr std::uint8_t Select = 0x04;
constexpr std::uint8_t Start = 0x08;
constexpr std::uint8_t Up = 0x10;
constexpr std::uint8_t Down = 0x20;
constexpr std::uint8_t Left = 0x40;
constexpr std::uint8_t Right = 0x80;
} // namespace Button

// Standard NES controller: a parallel-in/serial-out shift register.
// While strobe is high the register continuously reloads, so reads return
// the A button. After strobe falls, each read returns the next bit; once
// all eight bits are shifted out an official controller returns 1.
class Controller {
    friend class NesStateCodec;
public:
    void setButtons(std::uint8_t buttons);
    std::uint8_t buttons() const { return buttons_; }
    void writeStrobe(bool high);
    std::uint8_t read();            // Returns bit 0 only (D0 line).
    std::uint8_t peek() const;
    void reset();

private:
    std::uint8_t buttons_ = 0;
    std::uint8_t shift_ = 0;
    bool strobe_ = false;
};

} // namespace frantastic::nes

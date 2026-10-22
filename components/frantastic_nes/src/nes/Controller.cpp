#include "frantastic/nes/Controller.hpp"

namespace frantastic::nes {

// Standard controller: a 4021 parallel-in shift register. While strobe is
// high the register continuously reloads, so reads return button A. After
// eight reads the serial input is tied high and further reads return 1.
void Controller::setButtons(std::uint8_t buttons) {
    buttons_ = buttons;
    if (strobe_) shift_ = buttons_;
}

void Controller::writeStrobe(bool high) {
    strobe_ = high;
    if (strobe_) shift_ = buttons_;
}

std::uint8_t Controller::read() {
    if (strobe_) return buttons_ & 1u;
    const std::uint8_t bit = shift_ & 1u;
    shift_ = static_cast<std::uint8_t>((shift_ >> 1) | 0x80u);
    return bit;
}

std::uint8_t Controller::peek() const { return strobe_ ? (buttons_ & 1u) : (shift_ & 1u); }

void Controller::reset() {
    shift_ = 0;
    strobe_ = false;
}

} // namespace frantastic::nes

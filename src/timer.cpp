#include "dmg/timer.hpp"
#include <array>
namespace dmg {
bool Timer::signal() const {
    constexpr std::array<unsigned, 4> bits{9, 3, 5, 7};
    return (tac_ & 4U) != 0 && ((divider_ >> bits[tac_ & 3U]) & 1U) != 0;
}
void Timer::increment() {
    if (overflow_delay_ != 0 || reload_window_ != 0)
        return;
    ++tima_;
    if (tima_ == 0)
        overflow_delay_ = 4;
}
void Timer::tick() {
    if (reload_window_ != 0)
        --reload_window_;
    if (overflow_delay_ != 0 && --overflow_delay_ == 0) {
        tima_ = tma_;
        iflag_ |= 4;
        reload_window_ = 4;
    }
    const bool previous = signal();
    ++divider_;
    if (previous && !signal())
        increment();
}
std::uint8_t Timer::read(std::uint16_t address) const {
    switch (address) {
    case 0xFF04:
        return static_cast<std::uint8_t>(divider_ >> 8U);
    case 0xFF05:
        return tima_;
    case 0xFF06:
        return tma_;
    case 0xFF07:
        return static_cast<std::uint8_t>(tac_ | 0xF8U);
    default:
        return 0xFF;
    }
}
void Timer::reset_divider() {
    const bool previous = signal();
    divider_ = 0;
    if (previous && !signal())
        increment();
}
void Timer::write(std::uint16_t address, std::uint8_t value) {
    switch (address) {
    case 0xFF04:
        reset_divider();
        break;
    case 0xFF05:
        if (reload_window_ == 0) {
            tima_ = value;
            overflow_delay_ = 0;
        }
        break;
    case 0xFF06:
        tma_ = value;
        if (reload_window_ != 0)
            tima_ = value;
        break;
    case 0xFF07: {
        const bool previous = signal();
        tac_ = static_cast<std::uint8_t>(value & 7U);
        if (previous && !signal())
            increment();
        break;
    }
    default:
        break;
    }
}
} // namespace dmg

#pragma once
#include <cstdint>
namespace dmg {
class Timer {
  public:
    explicit Timer(std::uint8_t &interrupt_flags) : iflag_(interrupt_flags) {}
    void tick();
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    void reset_divider();
    [[nodiscard]] std::uint16_t divider() const { return divider_; }

  private:
    friend struct SnapshotAccess;
    std::uint8_t &iflag_;
    std::uint16_t divider_ = 0xABCC;
    std::uint8_t tima_ = 0, tma_ = 0, tac_ = 0;
    unsigned overflow_delay_ = 0, reload_window_ = 0;
    [[nodiscard]] bool signal() const;
    void increment();
};
} // namespace dmg

#pragma once
#include "dmg/mbc.hpp"
#include "dmg/apu.hpp"
#include "dmg/ppu.hpp"
#include "dmg/serial_link.hpp"
#include "dmg/timer.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace dmg {
#ifdef ENABLE_AUTOPSY
class Autopsy;
#endif
class Bus {
  public:
    std::uint8_t iflag = 0xE1, ie = 0;
    Ppu ppu;
    Timer timer;
    Apu apu;
    Cartridge cartridge;
    std::string serial_output;
    bool capture_serial_output{true};
    // The caller owns this endpoint and keeps it alive while attached.
    SerialEndpoint *serial_endpoint{};
#ifdef ENABLE_AUTOPSY
    Autopsy *autopsy{};
#endif
    explicit Bus(std::vector<std::uint8_t> rom,
                 std::optional<std::size_t> ram_override = std::nullopt);
    Bus(const Bus &) = delete;
    Bus &operator=(const Bus &) = delete;
    Bus(Bus &&) = delete;
    Bus &operator=(Bus &&) = delete;
    [[nodiscard]] std::uint8_t read(std::uint16_t address, bool idu = false,
                                    std::uint8_t *interrupt_sample = nullptr);
    void write(std::uint16_t address, std::uint8_t value);
    void idle() { tick(4); }
    void idu_idle(std::uint16_t address);
    void tick(unsigned t_cycles);
    [[nodiscard]] std::uint8_t peek(std::uint16_t address) const;
    void poke(std::uint16_t address, std::uint8_t value);
    [[nodiscard]] std::uint64_t cycles() const { return cycles_; }
    [[nodiscard]] std::uint8_t pending_interrupts() const {
        return static_cast<std::uint8_t>(iflag & ie & 0x1FU);
    }
    void acknowledge_interrupt(unsigned bit);
    void reset_divider();
    [[nodiscard]] bool joypad_wake() const { return (joypad() & 15U) != 15U; }
    // Bits Right Left Up Down A B Select Start; 1 means physically pressed.
    void set_buttons(std::uint8_t pressed);
    bool serial_clock(bool input_high);
    [[nodiscard]] bool serial_active() const { return (serial_control_ & 0x80U) != 0; }
    [[nodiscard]] bool serial_internal() const { return (serial_control_ & 1U) != 0; }
    [[nodiscard]] std::uint8_t serial_data() const { return serial_data_; }
    [[nodiscard]] std::uint8_t serial_control() const { return serial_control_; }
    [[nodiscard]] unsigned serial_bits() const { return serial_bits_; }
    [[nodiscard]] std::uint8_t *wram_data() { return wram_.data(); }
    [[nodiscard]] const std::uint8_t *wram_data() const { return wram_.data(); }
    static constexpr std::size_t WramSize = 8192;
    [[nodiscard]] bool dma_active() const { return dma_active_; }

  private:
    friend struct SnapshotAccess;
    std::array<std::uint8_t, 0x2000> wram_{};
    std::array<std::uint8_t, 0x7F> hram_{};
    std::array<std::uint8_t, 0x80> io_{};
    std::uint64_t cycles_ = 0;
    std::uint8_t joy_select_ = 0, buttons_ = 0;
    std::uint8_t serial_data_ = 0, serial_control_ = 0, serial_sent_ = 0;
    unsigned serial_bits_ = 0, serial_phase_ = 0;
    bool dma_active_ = false;
    unsigned dma_startup_ = 0, dma_phase_ = 0, dma_index_ = 0;
    std::uint16_t dma_source_ = 0, dma_pending_source_ = 0;
    [[nodiscard]] std::uint8_t joypad() const;
    [[nodiscard]] bool cpu_blocked(std::uint16_t address) const;
    bool shift_serial(bool input_high);
    void tick_dma();
};
} // namespace dmg

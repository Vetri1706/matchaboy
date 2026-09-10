#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace dmg {
class Cartridge {
  public:
    explicit Cartridge(std::vector<std::uint8_t> rom,
                       std::optional<std::size_t> ram_override = std::nullopt);
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    void tick();
    [[nodiscard]] std::size_t rom_banks() const { return rom_.size() / 0x4000U; }
    [[nodiscard]] std::uint8_t type() const { return type_; }
    [[nodiscard]] bool rumble_active() const { return rumble_active_; }
    [[nodiscard]] const std::vector<std::uint8_t> &ram() const { return ram_; }

  private:
    friend struct SnapshotAccess;
    std::array<std::uint8_t, 32> rom_identity_{};
    enum class Controller { None, Mbc1, Mbc3, Mbc5 };
    Controller controller_ = Controller::None;
    std::vector<std::uint8_t> rom_, ram_;
    std::uint8_t type_ = 0;
    bool ram_enabled_ = false, has_rtc_ = false;
    bool has_rumble_ = false, rumble_active_ = false;
    std::uint8_t low_bank_ = 1, high_bank_ = 0, mode_ = 0, ram_select_ = 0, latch_previous_ = 0xFF;
    std::array<std::uint8_t, 5> rtc_{}, latched_rtc_{};
    std::uint32_t rtc_subsecond_ = 0;
    [[nodiscard]] std::size_t ram_index(std::uint16_t address) const;
    void rtc_second();
};
} // namespace dmg

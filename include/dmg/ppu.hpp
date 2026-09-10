#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
namespace dmg {
class Ppu {
  public:
    std::array<std::uint8_t, 8192> vram{};
    std::array<std::uint8_t, 160> oam{};
    std::array<std::uint8_t, 160 * 144> framebuffer{};
    explicit Ppu(std::uint8_t &interrupt_flags);
    void tick();
    void drive_scroll_bus(std::uint16_t address, std::uint8_t value);
    void release_scroll_bus();
    void initialize_postboot();
    // Live write buses reach PPU consumers before CPU-visible storage latches.
    void drive_control_bus(std::uint8_t value);
    void release_control_bus();
    void drive_palette_bus(std::uint16_t address, std::uint8_t value, bool settling);
    void release_palette_bus();
    [[nodiscard]] std::uint8_t read(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    [[nodiscard]] bool vram_blocked(bool write = false) const;
    [[nodiscard]] bool oam_blocked(bool write = false) const;
    void set_dma_active(bool active) { dma_active_ = active; }
    [[nodiscard]] std::uint64_t frames() const { return frames_; }
    [[nodiscard]] unsigned mode() const { return mode_; }
    [[nodiscard]] unsigned dot() const { return dot_; }
    [[nodiscard]] unsigned reported_mode() const { return reported_mode_; }
    [[nodiscard]] unsigned fifo_depth() const { return fifo_size_; }
    [[nodiscard]] unsigned output_x() const { return screen_x_; }
    [[nodiscard]] unsigned fetch_phase() const { return static_cast<unsigned>(fetch_stage_); }
#ifdef ENABLE_AUTOPSY
    // Provenance is diagnostic history, not hardware state. After restoring a
    // release snapshot it is unknown until real fetches refill these slots.
    void invalidate_fifo_provenance() {
        background_tile_ids_.fill(0xFFFF);
        object_tile_ids_.fill(0xFFFF);
    }
#endif

  private:
    friend struct SnapshotAccess;
#ifdef ENABLE_AUTOPSY
    friend class Autopsy;
#endif
    struct Object {
        std::uint8_t y{}, x{}, tile{}, flags{};
        unsigned index{};
    };
    struct ObjectPixel {
        std::uint8_t color{}, flags{};
    };
    enum class FetchStage { Tile, Low, High, Push };
    enum class LcdSample { Clock, Enable };
    bool lcd_cp_enabled_{};
    std::uint8_t &interrupts_;
    std::uint16_t scroll_bus_address_{};
    std::uint8_t scroll_bus_value_{};
    unsigned scroll_bus_age_{};
    std::uint8_t fetch_scx() const {
        return scroll_bus_address_ == 0xFF43 && scroll_bus_age_ > 1 ? scroll_bus_value_ : scx_;
    }
    std::uint8_t fetch_scy() const {
        return scroll_bus_address_ == 0xFF42 && scroll_bus_age_ > 1 ? scroll_bus_value_ : scy_;
    }
    bool control_bus_active_{};
    std::uint8_t control_bus_value_{};
    unsigned control_bus_age_{};
    std::uint8_t control_lcdc(unsigned delay = 0) const {
        return control_bus_active_ && control_bus_age_ > delay ? control_bus_value_ : lcdc_;
    }
    std::uint8_t window_lcdc() const {
        return static_cast<std::uint8_t>((lcdc_ & 0xDFU) | (control_lcdc() & 0x20U));
    }
    bool window_match_emitted_{};
    bool window_match_consumed_{};
    std::uint8_t lcdc_{0x91}, stat_select_{}, scy_{}, scx_{}, ly_{}, lyc_{}, bgp_{0xFC},
        obp0_{0xFF}, obp1_{0xFF}, wy_{}, wx_{};
    unsigned line_{}, dot_{}, mode_{2};
    unsigned reported_mode_{2}, mode_delay_{}, compare_delay_{};
    std::uint64_t frames_{};
    bool stat_line_{};
    bool ly_compare_{true}, first_line_{}, vclk_{}, paru_{}, dma_active_{};
    unsigned stat_glitch_ticks_{};
    std::uint16_t palette_bus_address_{};
    std::uint8_t palette_bus_value_{};
    bool palette_bus_settling_{};
    [[nodiscard]] std::uint8_t output_palette(std::uint16_t address, LcdSample phase) const;
    std::array<Object, 10> objects_{};
    unsigned object_count_{}, next_object_{};
    std::array<ObjectPixel, 8> object_fifo_{};
    Object active_object_{};
    unsigned object_wait_{}, object_phase_{};
    std::uint8_t object_low_{}, object_high_{};
    bool object_fetching_{};
    int last_object_tile_{-10000};
    std::array<std::uint8_t, 8> background_fifo_{};
#ifdef ENABLE_AUTOPSY
    std::array<std::uint16_t, 8> background_tile_ids_{}, object_tile_ids_{};
#endif
    unsigned fifo_head_{}, fifo_size_{}, fetch_ticks_{}, fetch_tile_x_{};
    FetchStage fetch_stage_{FetchStage::Tile};
    bool discard_first_fetch_{}, fetched_window_{};
    std::uint8_t fetched_tile_{}, fetched_low_{}, fetched_high_{};
    unsigned previsible_discard_{};
    unsigned screen_x_{}, discard_{}, window_line_{}, window_fetch_line_{};
    unsigned window_match_wait_{};
    std::uint8_t window_match_wx_{};
    bool window_match_pending_{};
    bool window_y_triggered_{true}, window_active_{};
    [[nodiscard]] bool enabled() const { return (lcdc_ & 0x80U) != 0; }
    void update_stat();
    void set_mode(unsigned mode);
    [[nodiscard]] std::uint8_t read_oam(unsigned address) const;
    [[nodiscard]] std::uint16_t tile_data_address(unsigned plane) const;
    void begin_line();
    void search_oam();
    void prepare_transfer();
    void begin_transfer();
    void transfer();
    void reset_fetcher();
    void fetch_background();
    void push_background();
    [[nodiscard]] std::uint8_t pop_background();
    [[nodiscard]] bool start_window();
    [[nodiscard]] std::uint16_t object_data_address(unsigned plane) const;
    [[nodiscard]] bool start_object();
    void fetch_object();
    void merge_object();
    void output_pixel(bool insert_zero = false);
};
} // namespace dmg

#include "dmg/ppu.hpp"
#include <algorithm>
namespace dmg {
// Per-dot fetch/output. References: gbdev.io/pandocs/{Rendering,pixel_fifo,STAT}.html
Ppu::Ppu(std::uint8_t &interrupt_flags) : interrupts_(interrupt_flags) {
}
void Ppu::initialize_postboot() {
    lcd_cp_enabled_ = false;
    // Boot-ROM instruction timing places the first cartridge fetch here,
    // after LY has reset on physical line153. This is initialization only.
    line_ = 153;
    dot_ = 397;
    ly_ = 0;
    mode_ = reported_mode_ = 1;
    mode_delay_ = compare_delay_ = 0;
    paru_ = true;
    vclk_ = false;
}

bool Ppu::vram_blocked(bool write) const {
    return enabled() && (reported_mode_ == 3 || (!write && !first_line_ && mode_ == 3));
}
bool Ppu::oam_blocked(bool write) const {
    if (!enabled())
        return false;
    // The CPU write gate opens before the end-of-OAM mode indication, and
    // closes after the next scan begins. Reads have no such two-dot opening.
    if (write)
        return (mode_ == 2 && reported_mode_ == 2) || reported_mode_ == 3;
    return mode_ == 2 || reported_mode_ == 2 || reported_mode_ == 3;
}
std::uint8_t Ppu::read_oam(unsigned address) const {
    return dma_active_ ? 0xFF : oam[address];
}
std::uint8_t Ppu::read(std::uint16_t address) const {
    switch (address) {
    case 0xFF40:
        return lcdc_;
    case 0xFF41:
        return static_cast<std::uint8_t>(0x80U | stat_select_ | (ly_compare_ ? 4U : 0U) |
                                         reported_mode_);
    case 0xFF42:
        return scy_;
    case 0xFF43:
        return scx_;
    case 0xFF44:
        return ly_;
    case 0xFF45:
        return lyc_;
    case 0xFF47:
        return bgp_;
    case 0xFF48:
        return obp0_;
    case 0xFF49:
        return obp1_;
    case 0xFF4A:
        return wy_;
    case 0xFF4B:
        return wx_;
    default:
        return 0xFF;
    }
}
void Ppu::drive_scroll_bus(std::uint16_t address, std::uint8_t value) {
    scroll_bus_address_ = address;
    scroll_bus_value_ = value;
    scroll_bus_age_ = 0;
}
void Ppu::release_scroll_bus() {
    scroll_bus_address_ = 0;
}
void Ppu::drive_control_bus(std::uint8_t value) {
    // LCDC write data is visible to its transparent consumers before the
    // CPU-visible register's T3 latch. Each consumer keeps its own sampling
    // edge; the bus clock does not advance the PPU or timer twice.
    control_bus_active_ = true;
    control_bus_value_ = value;
    control_bus_age_ = 0;
}
void Ppu::release_control_bus() {
    control_bus_active_ = false;
}
void Ppu::drive_palette_bus(std::uint16_t address, std::uint8_t value, bool settling) {
    palette_bus_address_ = address;
    palette_bus_value_ = value;
    palette_bus_settling_ = settling;
}
void Ppu::release_palette_bus() {
    palette_bus_address_ = 0;
}
std::uint8_t Ppu::output_palette(std::uint16_t address, LcdSample phase) const {
    const auto stored = read(address);
    if (palette_bus_address_ != address)
        return stored;
    // The first driven dot exposes old/new palette bits together. Subsequent
    // dots see driven data before the CPU-visible register latch is updated.
    return palette_bus_settling_ && phase == LcdSample::Clock
               ? static_cast<std::uint8_t>(stored | palette_bus_value_)
               : palette_bus_value_;
}
void Ppu::write(std::uint16_t address, std::uint8_t value) {
    switch (address) {
    case 0xFF40: {
        const bool was_enabled = enabled();
        const bool window_was_enabled = (lcdc_ & 0x20U) != 0;
        lcdc_ = value;
        if (was_enabled != enabled())
            lcd_cp_enabled_ = false;
        if (window_was_enabled && (lcdc_ & 0x20U) == 0)
            window_active_ = false;

        if (was_enabled && !enabled()) {
            line_ = dot_ = 0;
            ly_ = 0;
            mode_ = 0;
            reported_mode_ = mode_delay_ = compare_delay_ = 0;
            first_line_ = false;
            vclk_ = paru_ = false;
            object_count_ = 0;
            window_line_ = 0;
            window_y_triggered_ = false;
            window_active_ = false;
            window_match_pending_ = false;
            object_fetching_ = false;
            stat_glitch_ticks_ = 0;
            framebuffer.fill(0);
        } else if (!was_enabled && enabled()) {
            line_ = dot_ = 0;
            ly_ = 0;
            window_line_ = 0;
            window_y_triggered_ = false;
            // LCD startup skips OAM search; its first line is four dots short.
            // Original Mooneye lcdon_{write_,}timing-GS checkpoints cover this.
            first_line_ = true;
            mode_ = reported_mode_ = mode_delay_ = compare_delay_ = 0;
            object_count_ = 0;
            if (ly_ == wy_)
                window_y_triggered_ = true;
            ly_compare_ = ly_ == lyc_;
        }
        update_stat();
        break;
    }
    case 0xFF41:
        stat_select_ = static_cast<std::uint8_t>(value & 0x78U);
        stat_glitch_ticks_ = enabled() ? 4U : 0U;
        update_stat();
        break;
    case 0xFF42:
        scy_ = value;
        break;
    case 0xFF43:
        scx_ = value;
        break;
    case 0xFF44:
        break;
    case 0xFF45:
        lyc_ = value;
        if (enabled())
            ly_compare_ = ly_ == lyc_;
        update_stat();
        break;
    case 0xFF47:
        bgp_ = value;
        break;
    case 0xFF48:
        obp0_ = value;
        break;
    case 0xFF49:
        obp1_ = value;
        break;
    case 0xFF4A:
        wy_ = value;
        break;
    case 0xFF4B:
        if (wx_ != value)
            window_match_consumed_ = false;
        wx_ = value;
        break;
    default:
        break;
    }
}
void Ppu::update_stat() {
    // The comparison/STAT latch clock stops with LCDC.7. Preserve its value;
    // restarting a true comparison as true must not manufacture a new edge.
    if (!enabled())
        return;
    const unsigned selects = stat_glitch_ticks_ != 0 ? 0x78U : stat_select_;
    // RUTU's short line-clock pulse supplies Mode2. PARU supplies both the
    // VBlank request and Mode1 STAT; overlapping sources share this OR line.
    const bool next =
        (((selects & 0x40U) != 0 && ly_compare_) ||
         ((selects & 0x20U) != 0 && vclk_ && !paru_) ||
         ((selects & 0x10U) != 0 && paru_) ||
         ((selects & 8U) != 0 && mode_ == 0 && mode_delay_ == 0 && !paru_));
    if (next && !stat_line_)
        interrupts_ = static_cast<std::uint8_t>(interrupts_ | 2U);
    stat_line_ = next;
}
void Ppu::set_mode(unsigned mode) {
    mode_ = mode;
    if (mode == 0)
        lcd_cp_enabled_ = false;
    // STAT and CPU video-memory access trail the internal interrupt/fetch
    // transition. Keeping the signals separate is required by intr_2_mode*.
    // Pixel completion changes mode during this dot; line/OAM transitions
    // occur after advancing the dot counter, so they have one more tick left.
    mode_delay_ = mode == 0 ? 1U : 2U;
    update_stat();
}
void Ppu::begin_line() {
    lcd_cp_enabled_ = false;
    set_mode(2);
    object_count_ = 0;
    if (ly_ == wy_)
        window_y_triggered_ = true;
    update_stat();
}
void Ppu::search_oam() {
    if ((dot_ & 1U) == 0)
        return;
    const unsigned index = dot_ / 2U, base = index * 4U;
    const int y = static_cast<int>(read_oam(base)) - 16, height = (lcdc_ & 4U) != 0 ? 16 : 8,
              line = static_cast<int>(line_);
    if (line >= y && line < y + height && object_count_ < objects_.size())
        objects_[object_count_++] = Object{read_oam(base), read_oam(base + 1), 0, 0, index};
}
void Ppu::reset_fetcher() {
    fifo_head_ = fifo_size_ = 0;
#ifdef ENABLE_AUTOPSY
    background_tile_ids_.fill(0xFFFF);
#endif
    fetch_stage_ = FetchStage::Tile;
    fetch_ticks_ = fetch_tile_x_ = 0;
}
void Ppu::prepare_transfer() {
    next_object_ = 0;
    object_fetching_ = false;
    object_fifo_.fill(ObjectPixel{});
#ifdef ENABLE_AUTOPSY
    object_tile_ids_.fill(0xFFFF);
#endif
    last_object_tile_ = -10000;
    screen_x_ = 0;
    window_match_consumed_ = false;
    window_match_pending_ = false;
    discard_ = fetch_scx() & 7U;
    // The initial fetched row traverses the real eight-pixel shifter.
    // Its output clocks advance the offscreen OBJ/WX comparators.
    previsible_discard_ = 8;
    window_active_ = false;
    reset_fetcher();
    discard_first_fetch_ = true;
}
void Ppu::begin_transfer() {
    set_mode(3);
    std::sort(objects_.begin(), objects_.begin() + object_count_,
              [](const Object &a, const Object &b) {
                  return a.x < b.x || (a.x == b.x && a.index < b.index);
              });
    update_stat();
}
void Ppu::push_background() {
    // BG data latches wait for the eight-pixel shifter to empty before loading.
    if (fifo_size_ != 0)
        return;
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
        const unsigned shift = 7U - pixel;
        const auto color = static_cast<std::uint8_t>(((fetched_low_ >> shift) & 1U) |
                                                     (((fetched_high_ >> shift) & 1U) << 1U));
        background_fifo_[(fifo_head_ + fifo_size_) & 7U] = color;
#ifdef ENABLE_AUTOPSY
        background_tile_ids_[(fifo_head_ + fifo_size_) & 7U] = fetched_tile_;
#endif
        ++fifo_size_;
    }
    if (discard_first_fetch_)
        discard_first_fetch_ = false;
    else
        ++fetch_tile_x_;
    // The fetch counter holds at its terminal state until the shifter loads,
    // then restarts immediately. Push represents the variable wait while the
    // FIFO is occupied; there is no additional fixed sleep after a load.
    fetch_stage_ = FetchStage::Tile;
    fetch_ticks_ = 0;
}
void Ppu::fetch_background() {
    if (fetch_stage_ == FetchStage::Push) {
        push_background();
        return;
    }
    if (++fetch_ticks_ < 2)
        return;
    fetch_ticks_ = 0;
    switch (fetch_stage_) {
    case FetchStage::Tile: {
        // LCDC.5 takes effect at the next tile fetch. Preserve the source of an
        // in-flight row across its low/high reads and drain its queued pixels.
        // The shared tile counter continues when background fetching resumes.
        fetched_window_ = window_active_;
        // Fine SCX is sampled at the first tile read, including a rewrite
        // after Mode 3 begins but before the non-discarded fetch completes.
        if (fetch_tile_x_ == 0 && !fetched_window_)
            discard_ = fetch_scx() & 7U;
        const unsigned y = fetched_window_ ? window_fetch_line_ : ((line_ + fetch_scy()) & 255U),
                       x = fetched_window_ ? fetch_tile_x_ : ((fetch_scx() >> 3U) + fetch_tile_x_);
        const unsigned map = fetched_window_ ? ((control_lcdc(1) & 0x40U) != 0 ? 0x1C00U : 0x1800U)
                                            : ((control_lcdc(1) & 8U) != 0 ? 0x1C00U : 0x1800U);
        fetched_tile_ = vram[map + (y >> 3U) * 32U + (x & 31U)];
        fetch_stage_ = FetchStage::Low;
        break;
    }
    case FetchStage::Low:
        fetched_low_ = vram[tile_data_address(0)];
        fetch_stage_ = FetchStage::High;
        break;
    case FetchStage::High:
        fetched_high_ = vram[tile_data_address(1)];
        fetch_stage_ = FetchStage::Push;
        push_background();
        break;
    case FetchStage::Push:
        break;
    }
}
std::uint16_t Ppu::tile_data_address(unsigned plane) const {
    const unsigned y = fetched_window_ ? window_fetch_line_ : ((line_ + fetch_scy()) & 255U);
    const int tile_base =
        (control_lcdc(1) & 0x10U) != 0
            ? static_cast<int>(fetched_tile_) * 16
            : 0x1000 + static_cast<int>(static_cast<std::int8_t>(fetched_tile_)) * 16;
    return static_cast<std::uint16_t>(tile_base + static_cast<int>((y & 7U) * 2U + plane));
}
std::uint8_t Ppu::pop_background() {
    const auto result = background_fifo_[fifo_head_];
    fifo_head_ = (fifo_head_ + 1U) & 7U;
    --fifo_size_;
    return result;
}
bool Ppu::start_window() {
    if (window_match_pending_) {
        // WX selects the first nametable read through a two-dot fetch phase.
        // A write before that read can revoke the match; once the tile ID has
        // been latched, changing WX does not undo the active window row.
        window_match_pending_ = false;
        if (wx_ == window_match_wx_ && ((lcdc_ | window_lcdc()) & 0x21U) == 0x21U) {
            if (window_match_wait_ != 0) {
                --window_match_wait_;
                window_match_pending_ = true;
                return true;
            }
            window_active_ = true;
            previsible_discard_ = 0;
            window_fetch_line_ = window_line_++ & 255U;
            reset_fetcher();
            // A rising enable can finish the current background pixel.
            fetch_ticks_ = window_match_emitted_ ? 0U : 1U;
            discard_first_fetch_ = false;
            discard_ = wx_ == 0 ? 7U + (fetch_scx() & 7U) : (wx_ < 7 ? 7U - wx_ : 0U);
            last_object_tile_ = -10000;
            return true;
        }
        // The revised WX can itself match on this dot (for example 6 -> 7).
    }
    if (window_active_ || !window_y_triggered_ || (window_lcdc() & 0x21U) != 0x21U || wx_ >= 167)
        return false;
    bool match = false;
    if (wx_ < 7) {
        // The comparator advances before visible X=0, including WX=0.
        // Its seven offscreen window pixels must actually traverse the FIFO:
        // those clocks can fetch another tile before an LCDC rewrite arrives.
        match = previsible_discard_ == 7U - wx_;
    } else {
        match = previsible_discard_ == 0 && discard_ == 0 && fifo_size_ != 0 &&
                screen_x_ == wx_ - 7U;
    }
    if (!match)
        return false;
    window_match_pending_ = true;
    window_match_consumed_ = true;
    window_match_wx_ = wx_;
    window_match_emitted_ = control_bus_active_ && control_bus_age_ == 1 &&
                            (lcdc_ & 0x20U) == 0 && (window_lcdc() & 0x20U) != 0 &&
                            previsible_discard_ == 0 && discard_ == 0 && fifo_size_ != 0;
    if (window_match_emitted_)
        output_pixel();
    // Hardware photos and the original Mealybug WX0 oracle show a one-dot
    // additional activation delay whenever initial fine scroll is nonzero.
    window_match_wait_ = wx_ == 0 && (fetch_scx() & 7U) != 0 ? 1U : 0U;
    return true;
}
std::uint16_t Ppu::object_data_address(unsigned plane) const {
    const unsigned height = (control_lcdc() & 4U) != 0 ? 16U : 8U;
    unsigned row = static_cast<unsigned>(static_cast<int>(line_) -
                                        (static_cast<int>(active_object_.y) - 16));
    row &= height - 1U;
    if ((active_object_.flags & 0x40U) != 0)
        row = height - 1U - row;
    unsigned tile = active_object_.tile;
    if (height == 16)
        tile &= 0xFEU;
    return static_cast<std::uint16_t>(tile * 16U + row * 2U + plane);
}
bool Ppu::start_object() {
    if ((control_lcdc(1) & 2U) == 0)
        return false;
    while (next_object_ < object_count_) {
        const Object &object = objects_[next_object_];
        const int left = static_cast<int>(object.x) - 8;
        const unsigned pipeline_x = previsible_discard_ != 0 ? 8U - previsible_discard_ : screen_x_ + 8U;
        if (object.x > pipeline_x)
            return false;
        ++next_object_;
        if (object.x >= 168)
            continue;
        if (object.x != 0 && left + 8 <= static_cast<int>(screen_x_))
            continue;
        active_object_ = object;
        active_object_.tile = read_oam(object.index * 4U + 2U);
        active_object_.flags = read_oam(object.index * 4U + 3U);
        const int coordinate =
            window_active_ ? left - (static_cast<int>(wx_) - 7) : left + static_cast<int>(fetch_scx());
        const int tile = coordinate >= 0 ? coordinate / 8 : (coordinate - 7) / 8;
        const unsigned fine = static_cast<unsigned>(coordinate) & 7U;
        object_wait_ = tile == last_object_tile_ ? 0U : (fine < 5 ? 5U - fine : 0U);
        if (object.x == 0 && tile != last_object_tile_)
            object_wait_ = 5;
        last_object_tile_ = tile;
        object_phase_ = 0;
        object_fetching_ = true;
        return true;
    }
    return false;
}
void Ppu::fetch_object() {
    // XYLO -> AROR -> sprite-match gates is combinational; cancellation
    // sees the incoming control bit before the pixel output mux does.
    if ((control_lcdc() & 2U) == 0) {
        // Object fetching can be cancelled at the next dot by LCDC.1. Resume
        // background clock/output instead of charging the unused fetch tail.
        object_fetching_ = false;
        transfer();
        return;
    }
    if (object_wait_ != 0) {
        fetch_background();
        --object_wait_;
        return;
    }
    ++object_phase_;
    // Background fetches retain VRAM during the three object setup dots.
    // Object plane reads own the following dots; pixel output stays stalled.
    if (object_phase_ <= 3)
        fetch_background();
    if (object_phase_ == 4)
        object_low_ = vram[object_data_address(0)];
    if (object_phase_ == 6) {
        object_high_ = vram[object_data_address(1)];
        merge_object();
        object_fetching_ = false;
    }
}
void Ppu::merge_object() {
    const int left = static_cast<int>(active_object_.x) - 8;
    for (unsigned pixel = 0; pixel < 8; ++pixel) {
        const int fifo_index = left + static_cast<int>(pixel) - static_cast<int>(screen_x_);
        if (fifo_index < 0 || fifo_index >= 8)
            continue;
        const unsigned bit = (active_object_.flags & 0x20U) != 0 ? pixel : 7U - pixel;
        const auto color = static_cast<std::uint8_t>(((object_low_ >> bit) & 1U) |
                                                     (((object_high_ >> bit) & 1U) << 1U));
        auto &destination = object_fifo_[static_cast<unsigned>(fifo_index)];
        if (color != 0 && destination.color == 0) {
            destination = ObjectPixel{color, active_object_.flags};
#ifdef ENABLE_AUTOPSY
            object_tile_ids_[static_cast<unsigned>(fifo_index)] =
                static_cast<std::uint16_t>(object_data_address(0) / 16U);
#endif
        }
    }
}
void Ppu::output_pixel(bool insert_zero) {
    // Initial CP activation traverses the SACU-clocked horizontal counter,
    // XAJO and WUSA before TOBA; later CP edges use the enabled TOBA path.
    // LCD data remains combinational. These discrete sampling buckets are
    // inferred from that ordering and original image collisions, not a
    // measurement of analogue gate propagation delay.
    const auto phase = lcd_cp_enabled_ ? LcdSample::Clock : LcdSample::Enable;
    const auto output_lcdc = control_lcdc(phase == LcdSample::Clock ? 1U : 0U);
    lcd_cp_enabled_ = true;
    // A window comparator glitch clocks the LCD/object path with color zero
    // while holding the background shifter. It never creates a ninth slot.
    const std::uint8_t background = insert_zero ? 0 : pop_background();
    const std::uint8_t color = (output_lcdc & 1U) != 0 ? background : 0;
    const ObjectPixel object = object_fifo_[0];
    std::uint8_t shade =
        (output_lcdc & 1U) != 0
            ? static_cast<std::uint8_t>((output_palette(0xFF47, phase) >> (color * 2U)) & 3U)
            : 0;
    if ((output_lcdc & 2U) != 0 && object.color != 0 && ((object.flags & 0x80U) == 0 || color == 0)) {
        const std::uint8_t palette = output_palette((object.flags & 0x10U) != 0 ? 0xFF49 : 0xFF48, phase);
        shade = static_cast<std::uint8_t>((palette >> (object.color * 2U)) & 3U);
    }
    framebuffer[line_ * 160U + screen_x_] = shade;
    for (unsigned i = 0; i < 7; ++i)
        object_fifo_[i] = object_fifo_[i + 1];
    object_fifo_[7] = ObjectPixel{};
#ifdef ENABLE_AUTOPSY
    for (unsigned i = 0; i < 7; ++i)
        object_tile_ids_[i] = object_tile_ids_[i + 1];
    object_tile_ids_[7] = 0xFFFF;
#endif
    window_match_consumed_ = false;
    if (++screen_x_ == 160) {
        set_mode(0);
        update_stat();
    }
}
void Ppu::transfer() {
    if (object_fetching_) {
        fetch_object();
        return;
    }
    // Match precedes FIFO availability because WX=0..6 occurs offscreen.
    if (start_window()) {
        // An uncommitted WX match does not halt the dummy shifter. If a WX
        // write revokes it on the next dot, the background clock stays aligned.
        if (window_match_pending_ && previsible_discard_ != 0 && fifo_size_ != 0) {
            static_cast<void>(pop_background());
            --previsible_discard_;
        }
        fetch_background();
        return;
    }
    if (fifo_size_ == 0) {
        fetch_background();
        return;
    }
    if (previsible_discard_ != 0) {
        if (start_object()) {
            fetch_object();
            return;
        }
        static_cast<void>(pop_background());
        --previsible_discard_;
        fetch_background();
        return;
    }
    if (discard_ != 0) {
        // Discarded pixels clock the same shifter as visible pixels. Retire
        // the final slot before a waiting tile parallel-loads on this dot.
        static_cast<void>(pop_background());
        --discard_;
        fetch_background();
        return;
    }
    if (start_object()) {
        fetch_object();
        return;
    }
    // WX matching the shifter's tile boundary inserts one color-zero pixel
    // if the window is already active or LCDC.5 has been cleared. The DMG
    // comparator is consumed until X advances or WX changes: a refill must
    // not manufacture a second match at the initial window coordinate.
    // This is visible in the original Mealybug WX4/5 and WIN_EN/WX images;
    // the disabled-window case is also documented in Pan Docs issue 376.
    const bool insert_zero = !window_match_consumed_ && window_y_triggered_ && wx_ >= 7 &&
        screen_x_ == static_cast<unsigned>(wx_ - 7U) &&
        (window_active_ || (window_lcdc() & 0x20U) == 0) && fifo_size_ == 8;
    // Retire the final queued pixel before parallel-loading the next tile.
    if (fifo_size_ != 0)
        output_pixel(insert_zero);
    fetch_background();
}
void Ppu::tick() {
    if (scroll_bus_address_ != 0)
        ++scroll_bus_age_;
    if (control_bus_active_)
        ++control_bus_age_;
    if (window_active_ && (control_lcdc(1) & 0x20U) == 0)
        window_active_ = false;
    if (!enabled())
        return;
    if (mode_delay_ != 0 && --mode_delay_ == 0) {
        reported_mode_ = mode_;
        update_stat();
    }
    if (compare_delay_ != 0 && --compare_delay_ == 0) {
        ly_compare_ = ly_ == lyc_;
        update_stat();
    }
    if (stat_glitch_ticks_ != 0) {
        --stat_glitch_ticks_;
        if (stat_glitch_ticks_ == 0)
            update_stat();
    }
    if (mode_ == 2) {
        search_oam();
        // VRAM precharge overlaps the final two OAM-search dots. This phase
        // placement preserves the measured first-visible dot while clocking
        // the six fetch dots and all eight real dummy shifts.
        if (dot_ >= 78)
            fetch_background();
    } else if (first_line_ && mode_ == 0 && dot_ >= 74 && dot_ < 76)
        fetch_background();
    else if (mode_ == 3) {
        transfer();
    }
    ++dot_;
    if (dot_ == 1) {
        const bool next_paru = (ly_ & 0x90U) == 0x90U;
        if (next_paru && !paru_)
            interrupts_ = static_cast<std::uint8_t>(interrupts_ | 1U);
        paru_ = next_paru;
        update_stat();
    }
    if (dot_ == 3) {
        vclk_ = false;
        update_stat();
    }
    if ((first_line_ && dot_ == 74) || (mode_ == 2 && dot_ == 78))
        prepare_transfer();
    if ((first_line_ && dot_ == 76) || (mode_ == 2 && dot_ == 80))
        begin_transfer();
    if (line_ == 153 && dot_ == 4) {
        ly_ = 0;
        ly_compare_ = ly_ == lyc_;
        update_stat();
    }
    const unsigned line_dots = first_line_ ? 452U : 456U;
    // FF44 exposes the LY ripple counter directly. In the DMG netlist RUTU
    // clocks it on the opposite hclk edge from the horizontal counter; ROPO
    // separately clocks the LYC comparison. Do not merge those signals with
    // the physical line/mode transition. The last-dot LY edge also satisfies
    // the original hblank_ly_scx_timing-GS read checkpoints at every fine SCX.
    if (dot_ == line_dots - 1U) {
        ly_ = static_cast<std::uint8_t>(line_ == 153 ? 0U : line_ + 1U);
        vclk_ = true;
        update_stat();
    }
    if (dot_ != line_dots)
        return;
    first_line_ = false;
    dot_ = 0;
    ++line_;
    if (line_ == 154) {
        line_ = 0;
        window_line_ = 0;
        window_y_triggered_ = false;
    }
    ly_ = static_cast<std::uint8_t>(line_);
    ly_compare_ = false;
    compare_delay_ = 2;
    if (line_ < 144)
        begin_line();
    else {
        set_mode(1);
        if (line_ == 144) {
            ++frames_;
        }
        update_stat();
    }
}
} // namespace dmg

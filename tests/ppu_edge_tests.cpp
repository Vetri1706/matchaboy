#include "dmg/ppu.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned assertions = 0;
void require(bool condition, const std::string &message) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(message);
}
void advance(dmg::Ppu &ppu, unsigned dots) {
    for (unsigned i = 0; i < dots; ++i)
        ppu.tick();
}
void transfer_start(dmg::Ppu &ppu) {
    for (unsigned i = 0; i < 456 && ppu.mode() != 3; ++i)
        ppu.tick();
    require(ppu.mode() == 3, "transfer starts");
}
void transfer_end(dmg::Ppu &ppu) {
    for (unsigned i = 0; i < 376 && ppu.mode() == 3; ++i)
        ppu.tick();
    require(ppu.mode() == 0, "all 160 pixels emitted before HBlank");
}
void enable(dmg::Ppu &ppu, std::uint8_t lcdc = 0x91) {
    ppu.write(0xFF40, 0);
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, lcdc);
}
void startup_checkpoints() {
    // Published hardware observations in the original lcdon_timing-GS source.
    // Each sample is n NOPs followed by LD A,(DE), i.e. 4*n+8 dots after LCDC.
    constexpr std::array<unsigned, 24> nops{0, 17, 60, 110, 130, 174, 224, 244,
                                            1, 18, 61, 111, 131, 175, 225, 245,
                                            2, 19, 62, 112, 132, 176, 226, 246};
    constexpr std::array<std::uint8_t, 24> ly{0, 0, 0, 0, 1, 1, 1, 2, 0, 0, 0, 1,
                                              1, 1, 2, 2, 0, 0, 0, 1, 1, 1, 2, 2};
    constexpr std::array<std::uint8_t, 24> stat{0x84, 0x84, 0x87, 0x84, 0x82, 0x83, 0x80, 0x82,
                                                0x84, 0x87, 0x84, 0x80, 0x82, 0x80, 0x80, 0x82,
                                                0x84, 0x87, 0x84, 0x82, 0x83, 0x80, 0x82, 0x83};
    constexpr std::array<std::uint8_t, 24> stat_lyc1{
        0x80, 0x80, 0x83, 0x80, 0x86, 0x87, 0x84, 0x82, 0x80, 0x83, 0x80, 0x80,
        0x86, 0x84, 0x80, 0x82, 0x80, 0x83, 0x80, 0x86, 0x87, 0x84, 0x82, 0x83};
    constexpr std::array<bool, 24> oam_read{false, false, true,  false, true, true,  false, true,
                                            false, true,  false, true,  true, false, true,  true,
                                            false, true,  false, true,  true, false, true,  true};
    constexpr std::array<bool, 24> vram_read{false, false, true,  false, false, true,  false, false,
                                             false, true,  false, false, true,  false, false, true,
                                             false, true,  false, false, true,  false, false, true};
    for (unsigned i = 0; i < nops.size(); ++i) {
        std::uint8_t irq = 0;
        dmg::Ppu ppu(irq);
        enable(ppu, 0x81);
        advance(ppu, nops[i] * 4 + 8);
        require(ppu.read(0xFF44) == ly[i], "LCD enable LY checkpoint " + std::to_string(i));
        require(ppu.read(0xFF41) == stat[i], "LCD enable STAT checkpoint " + std::to_string(i));
        require(ppu.oam_blocked() == oam_read[i], "LCD enable OAM read gate " + std::to_string(i));
        require(ppu.vram_blocked() == vram_read[i],
                "LCD enable VRAM read gate " + std::to_string(i));
        std::uint8_t second_irq = 0;
        dmg::Ppu second(second_irq);
        second.write(0xFF45, 1);
        enable(second, 0x81);
        advance(second, nops[i] * 4 + 8);
        require(second.read(0xFF41) == stat_lyc1[i], "LYC=1 comparison delay " + std::to_string(i));
    }
}
void separate_bitplane_sampling() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0] = 0xFF; // row 0, low plane: color bit 0
    ppu.vram[3] = 0xFF; // row 1, high plane: color bit 1
    enable(ppu);
    transfer_start(ppu);
    advance(ppu, 8); // initial dummy row, then real tile and low plane
    ppu.write(0xFF42, 1);
    advance(ppu, 5);
    require(ppu.framebuffer[0] == 3, "SCY sampled independently for each DMG bitplane");

    std::uint8_t second_irq = 0;
    dmg::Ppu second(second_irq);
    second.vram[0x1800] = 1;
    second.vram[0x10] = 0xFF;   // unsigned tile 1, low plane
    second.vram[0x1011] = 0xFF; // signed tile 1, high plane
    enable(second);
    transfer_start(second);
    advance(second, 8);
    second.write(0xFF40, 0x81);
    advance(second, 5);
    require(second.framebuffer[0] == 3, "LCDC.4 sampled at each data-plane fetch");
}
void ly_counter_and_mode_have_separate_edges() {
    // FF44 is the raw counter, whereas STAT coincidence has its own clock.
    // The original Mooneye HBlank/SCX ROM reads LY on the last physical dot.
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    enable(ppu);
    advance(ppu, 450);
    require(ppu.read(0xFF44) == 0, "startup LY before counter edge");
    ppu.tick();
    require(ppu.read(0xFF44) == 1 && ppu.mode() == 0,
            "startup LY edge precedes physical line transition");
    require((ppu.read(0xFF41) & 4U) != 0,
            "LY counter advance does not asynchronously clock coincidence");
    ppu.tick();
    require(ppu.dot() == 0 && ppu.mode() == 2, "short startup line still has 452 physical dots");
    require((ppu.read(0xFF41) & 4U) == 0, "comparison is cleared at line transition");
    advance(ppu, 454);
    require(ppu.read(0xFF44) == 1, "normal LY before counter edge");
    ppu.tick();
    require(ppu.read(0xFF44) == 2 && ppu.mode() == 0,
            "normal last-dot LY read sees next counter value during HBlank");
    ppu.tick();
    require(ppu.dot() == 0 && ppu.mode() == 2, "normal physical line remains exactly 456 dots");
    advance(ppu, 141 * 456 + 454);
    irq = 0;
    ppu.tick();
    require(ppu.read(0xFF44) == 144 && ppu.mode() == 0 && (irq & 1U) == 0,
            "LY144 edge alone does not start VBlank or request its interrupt");
    ppu.tick();
    require(ppu.mode() == 1 && (irq & 1U) == 0,
            "physical VBlank mode precedes the PARU sampling edge");
    ppu.tick();
    require((irq & 1U) != 0, "PARU edge requests VBlank on the next line-clock phase");
}
void palette_is_sampled_at_output() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    for (unsigned row = 0; row < 8; ++row)
        ppu.vram[row * 2] = 0xFF;
    enable(ppu);
    transfer_start(ppu);
    advance(ppu, 12 + 20);
    for (unsigned x = 0; x < 20; ++x)
        require(ppu.framebuffer[x] == 1, "past pixels retain old palette");
    require(ppu.framebuffer[20] == 0, "future pixel not rendered early");
    ppu.write(0xFF47, 0x0C);
    ppu.tick();
    require(ppu.framebuffer[19] == 1, "palette write does not redraw previous pixels");
    require(ppu.framebuffer[20] == 3, "next dot observes new palette");
}
void window_does_not_trigger_behind_beam() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    for (unsigned row = 0; row < 8; ++row) {
        ppu.vram[row * 2] = 0xFF;
        ppu.vram[16 + row * 2 + 1] = 0xFF;
    }
    for (unsigned i = 0; i < 1024; ++i)
        ppu.vram[0x1C00 + i] = 1;
    ppu.write(0xFF4B, 167);
    enable(ppu, 0xF1);
    transfer_start(ppu);
    advance(ppu, 12 + 80);
    ppu.write(0xFF4B, 47); // trigger point x=40 has already passed
    transfer_end(ppu);
    for (unsigned x = 0; x < 160; ++x)
        require(ppu.framebuffer[x] == 1, "window comparator requires an exact X match");
}
void stat_persistent_line() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    enable(ppu);
    ppu.write(0xFF41, 0x78);
    require((irq & 2) != 0, "STAT write glitch requests edge");
    irq = 0;
    advance(ppu, 300);
    require((irq & 2) == 0, "coincidence holds OR line high through mode3");
    ppu.write(0xFF45, 1);
    advance(ppu, 156);
    require((irq & 2) == 0, "overlapping coincidence/mode sources block retrigger");
}
void stat_vblank_source_handoff() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.write(0xFF41, 0x30); // consecutive Mode2 and Mode1 sources share one line
    advance(ppu, 456 * 144);
    require((irq & 2U) != 0 && ppu.mode() == 1, "VBlank entry requested STAT");
    irq = 0;
    for (unsigned dot = 0; dot < 8; ++dot) {
        ppu.tick();
        require((irq & 2U) == 0, "Mode2 pulse hands off to Mode1 without a low gap");
    }
}
void coincidence_clock_stops_with_lcd() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.write(0xFF41, 0x40);
    advance(ppu, 456 * 144);
    ppu.write(0xFF45, 144);
    require((ppu.read(0xFF41) & 4) != 0, "coincidence set before disabling LCD");
    ppu.write(0xFF40, 0);
    irq = 0;
    ppu.write(0xFF45, 0);
    advance(ppu, 20);
    require((ppu.read(0xFF41) & 4) != 0, "comparison latch retained while LCD clock stops");
    require(irq == 0, "changing LYC with stopped LCD clock does not trigger IRQ");
    ppu.write(0xFF40, 0x80);
    require((ppu.read(0xFF41) & 4) != 0, "same true comparison survives enable");
    require(irq == 0, "true-to-true coincidence across restart is not a rising edge");
}
void window_six_dot_restart() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    for (unsigned row = 0; row < 8; ++row) {
        ppu.vram[row * 2] = 0xFF;
        ppu.vram[16 + row * 2 + 1] = 0xFF;
    }
    for (unsigned i = 0; i < 1024; ++i)
        ppu.vram[0x1C00 + i] = 1;
    ppu.write(0xFF4B, 87);
    enable(ppu, 0xF1);
    transfer_start(ppu);
    advance(ppu, 12 + 80);
    require(ppu.framebuffer[79] == 1, "background reaches window boundary");
    for (unsigned dot = 0; dot < 6; ++dot) {
        ppu.tick();
        require(ppu.framebuffer[80] == 0, "window fetch consumes six actual dots");
    }
    ppu.tick();
    require(ppu.framebuffer[80] == 2, "first window pixel emitted on seventh dot");
}
void discarded_startup_fetch_is_not_visible() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0] = 0xFF;
    ppu.vram[17] = 0xFF;
    ppu.vram[0x1801] = 1;
    enable(ppu);
    transfer_start(ppu);
    advance(ppu, 4);
    require(ppu.fifo_depth() > 0 && ppu.fifo_depth() <= 8 && ppu.output_x() == 0,
            "dummy row traverses the real eight-pixel shifter without visible output");
    ppu.write(0xFF43, 8);
    advance(ppu, 9);
    require(ppu.framebuffer[0] == 2, "real startup fetch observes intervening SCX write");
}
void window_zero_discards_seven_real_pixels() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0x1C00] = 1;
    ppu.vram[0x1C01] = 2;
    ppu.vram[16] = 0x01; // Tile 1: only its last pixel is color 1.
    ppu.vram[33] = 0x80; // Tile 2: only its first pixel is color 2.
    ppu.write(0xFF4B, 0);
    enable(ppu, 0xF1);
    transfer_start(ppu);
    transfer_end(ppu);
    require(ppu.framebuffer[0] == 1 && ppu.framebuffer[1] == 2 && ppu.framebuffer[2] == 0,
            "WX0 discards seven genuine window pixels and crosses into the next tile");
}
void window_disable_and_retrigger() {
    // Primary hardware findings: Mealybug WIN_EN notes. Disabling finishes
    // the current window tile; reactivation at a later WX uses the next row.
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0x1C00] = 1;
    for (unsigned row = 0; row < 8; ++row)
        ppu.vram[row * 2 + 1] = 0xFF; // background color 2
    ppu.vram[16] = 0xFF;             // window row 0, color 1
    ppu.vram[18] = ppu.vram[19] = 0xFF; // window row 1, color 3
    ppu.write(0xFF4B, 7);
    enable(ppu, 0xF1);
    transfer_start(ppu);
    for (unsigned n = 0; n < 40 && ppu.output_x() < 1; ++n)
        ppu.tick();
    require(ppu.output_x() == 1, "first window pixel emitted before disable");
    ppu.write(0xFF40, 0xD1);
    for (unsigned i = 0; i < 80 && ppu.output_x() < 10; ++i)
        ppu.tick();
    require(ppu.output_x() == 10, "disabled window drains without a deadlock");
    for (unsigned x = 0; x < 8; ++x)
        require(ppu.framebuffer[x] == 1, "window disable drains current tile");
    require(ppu.framebuffer[8] == 2 && ppu.framebuffer[9] == 2,
            "next tile resumes background without discarding fine-scroll pixels");
    ppu.write(0xFF40, 0xF1);
    for (unsigned i = 0; i < 80 && ppu.output_x() < 20; ++i)
        ppu.tick();
    require(ppu.output_x() == 20, "background continues after enable");
    require(ppu.framebuffer[19] == 2, "enable alone cannot retrigger a passed WX");
    ppu.write(0xFF4B, 31); // future visible x=24
    for (unsigned i = 0; i < 80 && ppu.output_x() < 26; ++i)
        ppu.tick();
    require(ppu.output_x() == 26, "future window retrigger makes progress");
    require(ppu.framebuffer[23] == 2 && ppu.framebuffer[24] == 3,
            "new future WX restarts window using its next row on the same line");
}
void window_zero_insertion_holds_background_only() {
    // A disabled window match at a BG tile boundary clocks one zero pixel
    // while preserving the next queued BG pixel. Fine SCX shifts that boundary.
    for (unsigned fine = 0; fine < 8; ++fine) {
        std::uint8_t irq = 0;
        dmg::Ppu ppu(irq);
        for (unsigned row = 0; row < 8; ++row)
            ppu.vram[row * 2] = 0xFF;
        ppu.write(0xFF43, static_cast<std::uint8_t>(fine));
        ppu.write(0xFF4B, static_cast<std::uint8_t>(15 - fine));
        enable(ppu);
        transfer_start(ppu);
        transfer_end(ppu);
        const unsigned x = 8 - fine;
        require(ppu.framebuffer[x - 1] == 1 && ppu.framebuffer[x] == 0 &&
                    ppu.framebuffer[x + 1] == 1,
                "disabled window inserts one zero at the fine-SCX tile boundary");
    }

    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0x1C00] = 1;
    ppu.vram[16] = 0xFF; // window color 1
    ppu.vram[33] = 0xFF; // object color 2, behind nonzero BG/window
    ppu.oam[0] = 16;
    ppu.oam[1] = 8;
    ppu.oam[2] = 2;
    ppu.oam[3] = 0x80;
    ppu.write(0xFF48, 0xE4);
    ppu.write(0xFF4B, 4); // first three window pixels are offscreen
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF40, 0xF3); // retain the normal first OAM scan
    transfer_start(ppu);
    for (unsigned i = 0; i < 80 && ppu.output_x() < 2; ++i)
        ppu.tick();
    require(ppu.output_x() == 2, "window active before secondary WX write");
    ppu.write(0xFF4B, 12); // x=5, next window tile boundary
    transfer_end(ppu);
    require(ppu.framebuffer[4] == 1 && ppu.framebuffer[5] == 2,
            "inserted zero reveals the correctly aligned behind-BG object pixel");
}
void initial_window_match_is_consumed() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.vram[0x1C00] = 1;
    ppu.vram[16] = 0xFF;
    ppu.write(0xFF4B, 7);
    enable(ppu, 0xF1);
    transfer_start(ppu);
    transfer_end(ppu);
    for (unsigned x = 0; x < 8; ++x)
        require(ppu.framebuffer[x] == 1,
                "initial window match cannot retrigger during the refill stall");
}
void early_window_comparator_latches_tile() {
    // Original m3_wx_{4,5,6}_change writes WX at the dot-92 boundary. The
    // first two positions have reached the tile read; WX6 is still revocable.
    for (const std::uint8_t wx : {4, 5, 6}) {
        std::uint8_t irq = 0;
        dmg::Ppu ppu(irq);
        ppu.write(0xFF47, 0xE4);
        ppu.write(0xFF4B, wx);
        ppu.write(0xFF40, 0xF1);
        for (unsigned i = 0; i < 1024; ++i)
            ppu.vram[0x1C00 + i] = 1;
        ppu.vram[1] = 0xFF; // background color 2
        ppu.vram[16] = 0xAA; // window row 0 alternating 1/0
        ppu.vram[18] = ppu.vram[19] = 0xFF; // window row 1 color 3
        advance(ppu, 92);
        ppu.write(0xFF4B, 31);
        transfer_end(ppu);
        const unsigned first = wx == 6 ? 2U : (wx == 4 ? 0U : 1U);
        require(ppu.framebuffer[0] == first,
                "WX4/5 first tile latched before WX6 can survive a same-dot rewrite");
        if (wx == 6)
            require(ppu.framebuffer[24] == 1,
                    "cancelled offscreen match does not consume a window row");
    }
}
void object_fetch_cancel_and_dma_bus() {
    std::uint8_t irq = 0;
    dmg::Ppu ppu(irq);
    ppu.write(0xFF47, 0xE4);
    ppu.write(0xFF48, 0xE4);
    ppu.write(0xFF40, 0x93);
    for (unsigned row = 0; row < 8; ++row) {
        ppu.vram[row * 2] = 0xFF;
        ppu.vram[16 + row * 2 + 1] = 0xFF;
    }
    ppu.oam[0] = 16;
    ppu.oam[1] = 8;
    ppu.oam[2] = 1;
    advance(ppu, 80 + 12 + 1);
    require(ppu.output_x() == 0, "object fetch stalls pixel output");
    ppu.write(0xFF40, 0x91);
    ppu.tick();
    require(ppu.output_x() == 1 && ppu.framebuffer[0] == 1,
            "clearing LCDC.1 cancels an in-flight object fetch");

    ppu.write(0xFF40, 0x93);
    advance(ppu, 456 - ppu.dot());
    ppu.set_dma_active(true);
    advance(ppu, 456);
    require(ppu.framebuffer[160] == 1, "DMA owns OAM search bus and PPU reads FF");
    ppu.set_dma_active(false);
    advance(ppu, 456);
    require(ppu.framebuffer[320] == 2, "normal OAM search resumes after DMA releases bus");
}
} // namespace

int main() {
    try {
        startup_checkpoints();
        ly_counter_and_mode_have_separate_edges();
        separate_bitplane_sampling();
        palette_is_sampled_at_output();
        window_does_not_trigger_behind_beam();
        stat_persistent_line();
        stat_vblank_source_handoff();
        coincidence_clock_stops_with_lcd();
        window_six_dot_restart();
        window_zero_discards_seven_real_pixels();
        discarded_startup_fetch_is_not_visible();
        object_fetch_cancel_and_dma_bus();
        window_disable_and_retrigger();
        early_window_comparator_latches_tile();
        window_zero_insertion_holds_background_only();
        initial_window_match_is_consumed();
        std::cout << "PPU edge checks passed: " << assertions << " assertions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "PPU edge failure: " << error.what() << '\n';
        return 1;
    }
}

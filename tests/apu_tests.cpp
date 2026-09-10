#include "dmg/apu.hpp"
#include <array>
#include <cstdlib>
#include <iostream>

namespace {
unsigned checks{};
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) { std::cerr << "APU failure: " << message << '\n'; std::exit(1); }
}
void ticks(dmg::Apu &apu, unsigned count) { while (count--) apu.tick(); }
void frames(dmg::Apu &apu, unsigned count) { while (count--) apu.frame_sequencer_tick(); }
void pulse(dmg::Apu &apu, unsigned channel, std::uint8_t envelope = 0xF0) {
    apu.write(0xFF26, 0x80);
    const std::uint16_t base = channel == 0 ? 0xFF10 : 0xFF15;
    apu.write(base + 1, 0x80);
    apu.write(base + 2, envelope);
    apu.write(base + 3, 0xFE);
    apu.write(base + 4, 0x87);
}
}
int main() {
    {
        dmg::Apu apu;
        check(apu.read(0xFF26) == 0x70, "power-off status mask");
        apu.write(0xFF12, 0xF3);
        check(apu.read(0xFF12) == 0, "power-off envelope is not writable");
        apu.write(0xFF11, 63);
        check(apu.state().pulse[0].length == 1, "DMG length is writable with power off");
        apu.write(0xFF30, 0xAB);
        check(apu.read(0xFF30) == 0xAB, "wave RAM survives power off");
        apu.initialize_postboot();
        check(apu.read(0xFF26) == 0xF1, "DMG post-boot active channel");
        check(apu.read(0xFF10) == 0x80 && apu.read(0xFF11) == 0xBF &&
              apu.read(0xFF14) == 0xBF && apu.read(0xFF1C) == 0x9F,
              "hardware register masks");
    }
    {
        dmg::Apu apu;
        pulse(apu, 0);
        check(apu.channel_levels()[0] == 0, "first duty step outputs zero before divider starts");
        ticks(apu, 7);
        check(apu.state().pulse[0].position == 0, "pulse divider does not expire early");
        ticks(apu, 1);
        check(apu.state().pulse[0].position == 1, "pulse period is four times 2048-f");
        ticks(apu, 16);
        const auto phase = apu.state().pulse[0].position;
        apu.write(0xFF14, 0x87);
        check(apu.state().pulse[0].position == phase, "retrigger preserves duty phase");
        apu.write(0xFF11, 63);
        apu.write(0xFF14, 0xC7);
        frames(apu, 1);
        check(!(apu.read(0xFF26) & 1), "length clocks on frame-sequencer step zero");
        apu.write(0xFF14, 0xC7);
        check(apu.state().pulse[0].length == 63, "odd-phase trigger applies extra length clock");
        apu.write(0xFF12, 0);
        check(!(apu.read(0xFF26) & 1), "disabling DAC immediately stops channel");
    }
    {
        dmg::Apu apu;
        pulse(apu, 1, 0x29);
        frames(apu, 8);
        check(apu.state().pulse[1].envelope.volume == 3, "envelope increments after eight sequencer steps");
        frames(apu, 8 * 20);
        check(apu.state().pulse[1].envelope.volume == 15, "envelope saturates at fifteen");
        check(apu.read(0xFF26) & 2, "envelope saturation does not clear channel status");
    }
    {
        dmg::Apu apu;
        apu.write(0xFF26, 0x80);
        apu.write(0xFF10, 0x11);
        apu.write(0xFF12, 0xF0);
        apu.write(0xFF13, 0);
        apu.write(0xFF14, 0x84); // 1024 -> 1536; second calculation overflows.
        check(apu.read(0xFF26) & 1, "first sweep overflow precheck accepts 1536");
        frames(apu, 3);
        check(apu.state().pulse[0].sweep_shadow == 1536, "sweep updates shadow frequency");
        check(!(apu.read(0xFF26) & 1), "second sweep overflow check disables channel");
        apu.write(0xFF10, 0x19);
        apu.write(0xFF14, 0x84);
        apu.write(0xFF10, 0x11);
        check(!(apu.read(0xFF26) & 1), "clearing used sweep negate disables channel");
    }
    {
        dmg::Apu apu;
        apu.write(0xFF26, 0x80);
        apu.write(0xFF30, 0xA5);
        apu.write(0xFF31, 0x3C);
        apu.write(0xFF1A, 0x80);
        apu.write(0xFF1C, 0x20);
        apu.write(0xFF1D, 0xFF);
        apu.write(0xFF1E, 0x87);
        check(apu.read(0xFF30) == 0xFF, "active wave bus is inaccessible outside fetch");
        ticks(apu, 8);
        check(apu.channel_levels()[2] == 5, "wave starts with low nibble after trigger pipeline");
        check(apu.read(0xFF3F) == 0xA5, "active wave access aliases current fetch byte");
        ticks(apu, 2);
        check(apu.channel_levels()[2] == 3, "wave fetch advances to next byte high nibble");
        apu.write(0xFF1C, 0x40);
        check(apu.channel_levels()[2] == 1, "wave volume shifts digital sample");
    }
    {
        dmg::Apu apu;
        apu.write(0xFF26, 0x80);
        apu.write(0xFF21, 0xF0);
        apu.write(0xFF22, 0);
        apu.write(0xFF23, 0x80);
        ticks(apu, 8);
        check(apu.state().noise.lfsr == 0x3FFF, "noise XOR feedback advances 15-bit LFSR");
        ticks(apu, 8 * 14);
        check(apu.channel_levels()[3] == 15, "noise digital output follows inverted low bit");
        apu.write(0xFF22, 0xE0);
        const auto lfsr = apu.state().noise.lfsr;
        ticks(apu, 1000);
        check(apu.state().noise.lfsr == lfsr, "noise shifts fourteen and fifteen stop LFSR clock");
        apu.write(0xFF22, 8);
        apu.write(0xFF23, 0x80);
        ticks(apu, 8);
        check(apu.state().noise.lfsr == 0x3FBF, "short noise copies feedback to bit six");
    }
    for (unsigned delay = 1; delay <= 2; ++delay) {
        dmg::Apu apu;
        apu.write(0xFF26, 0x80);
        apu.write(0xFF30, 0x11);
        apu.write(0xFF31, 0x22);
        apu.write(0xFF1A, 0x80);
        apu.write(0xFF1D, 0xFE);
        apu.write(0xFF1E, 0x87);
        ticks(apu, 10 + delay);
        apu.write(0xFF1E, 0x87);
        apu.write(0xFF1A, 0);
        check(apu.read(0xFF30) == (delay == 2 ? 0x22 : 0x11),
              "wave retrigger corrupts upcoming byte only during address phase");
    }
    {
        dmg::Apu apu;
        pulse(apu, 0, 0xF1);
        apu.write(0xFF24, 0x77);
        apu.write(0xFF25, 0x11);
        apu.set_sample_rate(48000);
        ticks(apu, 4096);
        const auto saved = apu.state();
        ticks(apu, 30000);
        std::array<std::int16_t, 2048> first{}, second{};
        const auto first_count = apu.drain_samples(first);
        apu.restore(saved);
        ticks(apu, 30000);
        const auto second_count = apu.drain_samples(second);
        check(first_count > 0 && first_count == second_count && first == second,
              "restore reproduces pending PCM and future filter output exactly");
        const auto before = apu.state().pulse[0].position;
        apu.set_sample_rate(0);
        ticks(apu, 8);
        check(apu.state().pulse[0].position != before && apu.state().sample_count == 0,
              "disabling PCM preserves hardware channel evolution");
        apu.set_sample_rate(48000);
        ticks(apu, 4096);
        std::array<std::int16_t, 256> warmup{};
        (void)apu.drain_samples(warmup);
        const auto charge = apu.state().highpass_capacitor;
        check(charge[0] != 0 || charge[1] != 0, "real DAC output charges high-pass capacitor");
        apu.write(0xFF12, 0);
        ticks(apu, 4096);
        std::array<std::int16_t, 256> muted{};
        const auto count = apu.drain_samples(muted);
        bool silent = count > 0;
        for (std::size_t i = 0; i < count; ++i) silent &= muted[i] == 0;
        check(silent, "all DACs off disconnects high-pass output");
        check(apu.state().highpass_capacitor == charge, "disconnected high-pass capacitor retains its charge");
    }
    std::cout << "APU: " << checks << " checks passed\n";
}

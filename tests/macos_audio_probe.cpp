#include "macos_audio.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "gba_core.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("usage: macos_audio_probe ROM");
        const std::filesystem::path path(argv[1]);
        std::unique_ptr<dmg::Bus> bus;
        std::unique_ptr<dmg::Cpu> cpu;
        std::unique_ptr<GbaCore> gba;
        if (path.extension() == ".gba") {
            gba = std::make_unique<GbaCore>(path);
            gba->enable_audio();
        } else {
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("ROM unavailable");
            std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
            bus = std::make_unique<dmg::Bus>(std::move(bytes));
            cpu = std::make_unique<dmg::Cpu>(*bus);
            bus->apu.set_sample_rate(48000);
        }
        MacAudio audio;
        if (!audio.open()) throw std::runtime_error("actual macOS audio device unavailable");
        std::array<std::int16_t, 8192> samples{};
        auto next = std::chrono::steady_clock::now();
        std::uint64_t generated = 0;
        for (unsigned frame = 0; frame < 240; ++frame) {
            // Enter starts each original arcade cartridge after its title loads.
            const bool start = frame >= 30 && frame < 34;
            if (gba) {
                gba->set_buttons(start ? 8 : 0);
                gba->run_frame();
            } else {
                bus->set_buttons(start ? 0x80 : 0);
                const auto end = bus->cycles() + 70224;
                while (bus->cycles() < end) {
                    const auto before = bus->cycles();
                    cpu->step();
                    if (bus->cycles() == before) throw std::runtime_error("CPU stopped during playback probe");
                }
            }
            const auto count = gba ? gba->drain_audio(samples) : bus->apu.drain_samples(samples);
            generated += count / 2;
            audio.submit(std::span(samples).first(count));
            next += std::chrono::nanoseconds(16742706) + std::chrono::microseconds(audio.pacing_adjustment_us());
            std::this_thread::sleep_until(next);
            if (frame == 120) {
                // Pause/mute must return all queued packets immediately. Resume
                // starts a fresh queue instead of replaying pre-pause sound.
                audio.reset();
                if (audio.queued()) throw std::runtime_error("reset retained old playback buffers");
                const auto completed = audio.stats().completed_frames;
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                if (audio.stats().completed_frames != completed)
                    throw std::runtime_error("completion counter advanced after synchronous reset");
                next = std::chrono::steady_clock::now();
            }
        }
        const auto stats = audio.stats();
        audio.reset();
        const bool passed = stats.available && !stats.error && stats.peak && stats.completed_frames > 48000 &&
                            stats.submitted_frames > stats.completed_frames && !stats.dropped_frames;
        std::cout << "{\"passed\":" << (passed ? "true" : "false")
                  << ",\"system\":\"" << (gba ? "GBA" : "GB") << "\",\"generated_frames\":" << generated
                  << ",\"submitted_frames\":" << stats.submitted_frames
                  << ",\"completed_frames\":" << stats.completed_frames << ",\"peak\":" << stats.peak
                  << ",\"underruns\":" << stats.underruns << ",\"dropped_frames\":" << stats.dropped_frames
                  << ",\"max_gap_ms\":" << stats.max_gap_ms << ",\"reset_verified\":true}\n";
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "macos_audio_probe: " << error.what() << '\n';
        return 1;
    }
}

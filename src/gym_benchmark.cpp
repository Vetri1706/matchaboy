#include "dmg/gym.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using Clock = std::chrono::steady_clock;
std::uint64_t resident_peak() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS usage{};
    usage.cb = static_cast<DWORD>(sizeof(usage));
    if (GetProcessMemoryInfo(GetCurrentProcess(), &usage, usage.cb) == 0)
        throw std::runtime_error("GetProcessMemoryInfo failed");
    return static_cast<std::uint64_t>(usage.PeakWorkingSetSize);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
        throw std::runtime_error("getrusage failed");
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}
std::string hex(std::span<const std::uint8_t> data) {
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (auto byte : data) text << std::setw(2) << unsigned(byte);
    return text.str();
}
}
int main(int argc, char **argv) {
    try {
        if (argc < 2) throw std::invalid_argument("usage: gym_benchmark ROM [--instances 16] [--workers N] [--seconds 10] [--warmup 1] [--min-fps 50000] [--report FILE]");
        const std::string rom_path = argv[1];
        std::size_t count = 16, workers = 0;
        double seconds = 10, warmup = 1, minimum = 50000;
        std::string report_path;
        for (int i = 2; i < argc; ++i) {
            const std::string key = argv[i];
            if (++i == argc) throw std::invalid_argument("missing option value");
            const std::string value = argv[i];
            if (key == "--instances") count = std::stoull(value);
            else if (key == "--workers") workers = std::stoull(value);
            else if (key == "--seconds") seconds = std::stod(value);
            else if (key == "--warmup") warmup = std::stod(value);
            else if (key == "--min-fps") minimum = std::stod(value);
            else if (key == "--report") report_path = value;
            else throw std::invalid_argument("unknown option: " + key);
        }
        if (!std::isfinite(seconds) || !std::isfinite(warmup) || !std::isfinite(minimum) ||
            seconds <= 0 || warmup < 0 || minimum < 0)
            throw std::invalid_argument("invalid benchmark duration or threshold");
        std::ifstream input(rom_path, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open benchmark ROM");
        std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        const auto identity = dmg::snapshot_rom_identity(rom);
        dmg::HeadlessGym gym(std::move(rom), count, workers);
        std::vector<std::uint8_t> actions(count), dones(count);
        std::vector<float> rewards(count);
        std::uint32_t random = 0x4D415443;
        const auto batch = [&] {
            for (auto &action : actions) {
                random ^= random << 13U; random ^= random >> 17U; random ^= random << 5U;
                action = static_cast<std::uint8_t>(random);
            }
            gym.step(actions, rewards, dones);
            for (auto done : dones)
                if (done) throw std::runtime_error("benchmark ROM terminated; refusing to count inactive frames");
        };
        const auto warmed = Clock::now();
        while (std::chrono::duration<double>(Clock::now() - warmed).count() < warmup)
            batch();
        // Reset after warmup so timed work and input sequence are repeatable.
        gym.reset_all(); random = 0x4D415443;
        const auto started = Clock::now();
        std::uint64_t batches = 0;
        do { batch(); ++batches; }
        while (std::chrono::duration<double>(Clock::now() - started).count() < seconds);
        const double elapsed = std::chrono::duration<double>(Clock::now() - started).count();
        std::uint64_t cycles = 0, instructions = 0, ppu_frames = 0;
        for (std::size_t i = 0; i < count; ++i) {
            cycles += gym.bus(i).cycles(); instructions += gym.cpu(i).instructions;
            ppu_frames += gym.bus(i).ppu.frames();
        }
        const double frames = static_cast<double>(cycles) / dmg::HeadlessGym::CyclesPerFrame;
        const double fps = frames / elapsed;
        const bool passed = fps >= minimum;
        auto final = std::make_unique<dmg::MachineSnapshot>();
        if (dmg::save_snapshot(gym.cpu(0), gym.bus(0), *final) != dmg::SnapshotResult::Ok)
            throw std::runtime_error("cannot hash benchmark final machine state");
        const auto timed_hash = dmg::snapshot_hash(*final);
        // Independent of the time-limited sample, this fixed input workload
        // gives other hosts/toolchains directly comparable correctness hashes.
        gym.reset_all(); random = 0x4D415443;
        for (unsigned frame = 0; frame < 64; ++frame) batch();
        std::vector<std::uint64_t> replay_hashes;
        for (std::size_t i = 0; i < count; ++i) {
            if (dmg::save_snapshot(gym.cpu(i), gym.bus(i), *final) != dmg::SnapshotResult::Ok)
                throw std::runtime_error("cannot capture fixed-workload correctness hash");
            replay_hashes.push_back(dmg::snapshot_hash(*final));
        }
        std::ostringstream report;
        report << std::setprecision(12) << "{\n  \"passed\": " << (passed ? "true" : "false")
               << ",\n  \"rom_sha256\": \"" << hex(identity) << "\",\n  \"instances\": " << count
               << ",\n  \"workers\": " << gym.workers() << ",\n  \"warmup_seconds\": " << warmup
               << ",\n  \"requested_seconds\": " << seconds << ",\n  \"seconds\": " << elapsed
               << ",\n  \"minimum_fps\": " << minimum << ",\n  \"aggregate_fps\": " << fps
               << ",\n  \"executed_t_cycles\": " << cycles << ",\n  \"instructions\": " << instructions
               << ",\n  \"cycle_equivalent_frames\": " << frames << ",\n  \"completed_ppu_frames\": " << ppu_frames
               << ",\n  \"batch_steps\": " << batches << ",\n  \"peak_resident_bytes\": " << resident_peak()
               << ",\n  \"frame_quantum_t_cycles\": 70224,\n  \"input_seed\": 1296127043"
               << ",\n  \"audio_synthesis\": false,\n  \"ppu_enabled\": true,\n  \"accuracy_shortcuts\": false"
               << ",\n  \"timed_instance0_snapshot_hash\": " << timed_hash
               << ",\n  \"correctness_frames_per_instance\": 64,\n  \"correctness_hashes\": [";
        for (std::size_t i = 0; i < count; ++i)
            report << (i == 0 ? "" : ", ") << replay_hashes[i];
        report << "]\n}\n";
        if (!report_path.empty()) {
            std::ofstream output(report_path);
            output << report.str(); output.close();
            if (!output) throw std::runtime_error("cannot preserve benchmark report");
        }
        std::cout << report.str() << (passed ? "PASS" : "BELOW TARGET") << ": " << fps
                  << " aggregate executed frames/s across " << count << " machines\n";
        return passed ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "benchmark error: " << error.what() << '\n';
        return 2;
    }
}

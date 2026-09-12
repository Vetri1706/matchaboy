#include "friend_session.hpp"
#include "dmg/linked_pair.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "link_fixture.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
void check(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
struct Files {
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("matchaboy-session-edges-" + matcha::FriendSession::make_room_code());
    Files() { std::filesystem::create_directories(directory); }
    ~Files() { std::error_code ignored; std::filesystem::remove_all(directory, ignored); }
    std::filesystem::path rom(const std::string &name, std::span<const std::uint8_t> bytes) {
        const auto path = directory / name;
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        file.close(); check(bool(file), "Cannot write authored test cartridge");
        return path;
    }
};
struct Machine {
    dmg::Bus bus;
    dmg::Cpu cpu;
    explicit Machine(const std::vector<std::uint8_t> &rom) : bus(rom), cpu(bus) {}
};
matcha::FriendTransportOptions options(bool host, std::uint16_t port, const std::string &code) {
    return {host, "127.0.0.1", port, code};
}
void drive_to(matcha::FriendSession &a, matcha::FriendSession &b, std::uint64_t frame) {
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    while ((a.frames() < frame || b.frames() < frame) && Clock::now() < deadline) {
        if (a.frames() < frame) a.advance(0x10);
        if (b.frames() < frame) b.advance(0x20);
        check(!a.finished(), "Host stopped: " + a.status());
        check(!b.finished(), "Join stopped: " + b.status());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(a.frames() == frame && b.frames() == frame, "Confirmed session frames failed to progress");
}

void reject_rom_mismatch(Files &files) {
    const auto rom_a = linked_gb_rom(); auto rom_b = rom_a; rom_b[0x2000] ^= 1;
    const auto path_a = files.rom("original.gb", rom_a), path_b = files.rom("different-revision.gb", rom_b);
    Machine a(rom_a), b(rom_b); const auto code = matcha::FriendSession::make_room_code();
    matcha::FriendSession host(options(true, 0, code), path_a, &a.bus, &a.cpu, nullptr);
    matcha::FriendSession join(options(false, host.port(), code), path_b, &b.bus, &b.cpu, nullptr);
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while ((!host.finished() || !join.finished()) && Clock::now() < deadline) {
        host.advance(0); join.advance(0); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host.finished() && join.finished(), "Different ROM revisions did not terminate the session");
    check(a.bus.cycles() == 0 && b.bus.cycles() == 0 && !host.frames() && !join.frames(),
          "A mismatched-ROM session advanced an unverified console");
    check(host.status().find("same ROM") != std::string::npos || join.status().find("same ROM") != std::string::npos,
          "Mismatch did not identify the ROM compatibility problem");
    check(!a.bus.serial_endpoint && !b.bus.serial_endpoint, "Failed handshake left a cable attached");
}

void snapshots_waiting_and_terminal_cleanup(Files &files) {
    const auto rom = linked_gb_rom();
    const auto path = files.rom("snapshot-pair.gb", rom);
    Machine a(rom), b(rom);
    a.bus.poke(0xC001, 1); b.bus.poke(0xC001, 0);
    a.bus.poke(0xC100, 0xA5); b.bus.poke(0xC100, 0x5A);
    const auto room = matcha::FriendSession::make_room_code();
    matcha::FriendSession host(options(true, 0, room), path, &a.bus, &a.cpu, nullptr);
    matcha::FriendSession join(options(false, host.port(), room), path, &b.bus, &b.cpu, nullptr);
    for (unsigned i = 0; i < 10; ++i) check(!host.advance(0x10), "Host ran before friend handshake/input");
    check(!a.bus.cycles() && !b.bus.cycles(), "Waiting for handshake changed CPU clock state");
    drive_to(host, join, 30);
    for (unsigned i = 0; i < 200; ++i) host.advance(0x10);
    check(host.frames() <= 36 && join.frames() == 30, "Host advanced beyond confirmed peer input horizon");
    const auto waiting_cycles = a.bus.cycles();
    for (unsigned i = 0; i < 20; ++i) check(!host.advance(0x10), "Host invented a frame after confirmed inputs ran out");
    check(a.bus.cycles() == waiting_cycles, "A stalled linked session advanced peripheral cycles");
    drive_to(host, join, 90);
    check(a.bus.peek(0xC100) == 0xA5 && b.bus.peek(0xC100) == 0x5A,
          "Save/snapshot exchange overwrote the local player's distinct initial state");
    check(a.bus.serial_endpoint && b.bus.serial_endpoint, "Active sessions did not attach their real cable endpoints");
    // Mutate one actual local machine after the replicas synchronized. The
    // next scheduled digest must detect this real divergence and detach both.
    a.bus.poke(0xC100, 0xB4);
    const auto deadline = Clock::now() + std::chrono::seconds(8);
    while ((!host.finished() || !join.finished()) && Clock::now() < deadline) {
        host.advance(0x10); join.advance(0x20); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(host.finished() && join.finished(), "A mutated live replica did not terminate both sessions");
    check(host.status().find("desync") != std::string::npos || join.status().find("desync") != std::string::npos,
          "Live replica divergence was not diagnosed as desynchronization");
    check(!a.bus.serial_endpoint && !b.bus.serial_endpoint, "Terminal session retained callbacks into its remote console");
    std::array<std::int16_t, 32> audio{};
    check(host.drain_audio(audio) == 0 && join.drain_audio(audio) == 0,
          "Terminal session exposed old queued audio");
    const auto before_a = a.cpu.instructions, before_b = b.cpu.instructions;
    a.cpu.step(); b.cpu.step();
    check(a.cpu.instructions > before_a && b.cpu.instructions > before_b,
          "Local CPUs could not resume safely while terminal session objects remained alive");
}

std::vector<std::uint8_t> clock_rom(unsigned delay_nops, std::uint8_t output, bool internal) {
    std::vector<std::uint8_t> rom(32768);
    rom[0x100] = 0xC3; rom[0x101] = 0x50; rom[0x102] = 1;
    std::vector<std::uint8_t> program{0xF3, 0x31, 0xFE, 0xFF};
    program.insert(program.end(), delay_nops, 0x00);
    const std::array<std::uint8_t, 13> transfer{0x3E, output, 0xE0, 0x01, 0x3E,
        static_cast<std::uint8_t>(internal ? 0x81 : 0x80), 0xE0, 0x02,
        0xCD, 0x00, 0x03, 0x18, 0xFB};
    program.insert(program.end(), transfer.begin(), transfer.end());
    std::copy(program.begin(), program.end(), rom.begin() + 0x150);
    rom[0x300] = 0xC9; // CALL/RET keep real instructions spanning cable edges.
    return rom;
}
void cable_boundaries(bool dual_probe) {
    for (unsigned master = 0; master < 2; ++master) {
        for (unsigned delay = 0; delay < 32; ++delay) {
            Machine a(clock_rom(0, 0xA5, master == 0));
            Machine b(clock_rom(delay, 0x3C, master == 1));
            dmg::LinkedPair pair(a.bus, a.cpu, b.bus, b.cpu);
            pair.run_frame();
            check(a.bus.serial_data() == 0x3C && b.bus.serial_data() == 0xA5,
                  "M-cycle phase sweep changed actual cable data, role=" + std::to_string(master) + " delay=" + std::to_string(delay));
            check(a.bus.serial_bits() == 8 && b.bus.serial_bits() == 8 && pair.edges() == 8,
                  "Cable generated a missing/extra serial edge");
            check((a.bus.iflag & 8) && (b.bus.iflag & 8), "Completed cable transfer omitted a serial interrupt");
        }
    }
    if (dual_probe) {
        for (unsigned delay = 0; delay < 6; ++delay) {
            Machine a(clock_rom(0, 0xA5, true)), b(clock_rom(delay, 0x3C, true));
            dmg::LinkedPair pair(a.bus, a.cpu, b.bus, b.cpu);
            try { pair.run_frame(); }
            catch (const std::exception &e) { throw std::runtime_error("Dual internal clocks, delay " + std::to_string(delay) + ": " + e.what()); }
            check(a.bus.serial_bits() == 8 && b.bus.serial_bits() == 8, "Dual internal clocks failed to complete");
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc <= 2 && (argc == 1 || std::string(argv[1]) == "--dual-clock-probe"),
              "usage: friend_session_edge_tests [--dual-clock-probe]");
        Files files;
        reject_rom_mismatch(files);
        snapshots_waiting_and_terminal_cleanup(files);
        cable_boundaries(argc == 2);
        std::cout << "PASS friend session edges: ROM mismatch/no speculation, distinct initial snapshots, "
                     "live desync detection/terminal cleanup and 64 real cable clock alignments\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL friend session edges: " << e.what() << '\n';
        return 1;
    }
}

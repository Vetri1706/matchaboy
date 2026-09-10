#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Trace {
    std::uint64_t cycles = 0;
    std::uint16_t pc = 0, sp = 0;
    std::array<std::uint8_t, 8> regs{};
    std::array<std::uint8_t, 3> bytes{};
    bool ime = false;
    unsigned mode = 0, dot = 0;
    unsigned fifo = 0, fetch = 0, x = 0;
    std::uint8_t ly = 0, stat = 0, ie = 0, iflag = 0;
    bool dma = false, vram_locked = false, oam_locked = false;
    std::uint8_t opcode = 0;
    bool executed = false;
};

std::uint64_t number(const std::string &text) {
    std::size_t used = 0;
    if (text.empty() || text.front() == '-')
        throw std::runtime_error("expected unsigned integer");
    const auto n = std::stoull(text, &used, 0);
    if (used != text.size())
        throw std::runtime_error("invalid integer: " + text);
    return n;
}

void dump_trace(std::ostream &out, const std::vector<Trace> &ring, std::size_t cursor,
                std::uint64_t total, const dmg::Cpu &cpu) {
    out << "CPU-step trace (entry state; T = elapsed T-cycles; EXEC marks instruction "
           "retirement):\n";
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(total, ring.size()));
    for (std::size_t j = 0; j < count; ++j) {
        const auto &t = ring[(cursor + ring.size() - count + j) % ring.size()];
        const auto word = [&](std::size_t i) {
            return (static_cast<unsigned>(t.regs[i]) << 8U) | t.regs[i + 1];
        };
        out << "T=" << std::dec << t.cycles << " PC=" << std::hex << std::setfill('0')
            << std::setw(4) << t.pc << " SP=" << std::setw(4) << t.sp << " A=" << std::setw(2)
            << unsigned(t.regs[0]) << " F=" << std::setw(2) << unsigned(t.regs[1])
            << " BC=" << std::setw(4) << word(2) << " DE=" << std::setw(4) << word(4)
            << " HL=" << std::setw(4) << word(6) << " IME=" << t.ime << " IE=" << std::setw(2)
            << unsigned(t.ie) << " IF=" << std::setw(2) << unsigned(t.iflag)
            << " STAT=" << std::setw(2) << unsigned(t.stat) << std::dec << " PPU=" << t.mode
            << " LY=" << unsigned(t.ly) << " DOT=" << t.dot << " DMA=" << t.dma
            << " VRAM_LOCK=" << t.vram_locked << " OAM_LOCK=" << t.oam_locked << "  "
            << "FIFO=" << t.fifo << " FETCH_PHASE=" << t.fetch << " X=" << t.x
            << " EXEC=" << t.executed << " OPCODE=" << std::hex << std::setw(2)
            << unsigned(t.opcode) << std::dec << "  "
            << dmg::Cpu::disassemble(t.pc, t.bytes[0], t.bytes[1], t.bytes[2]) << '\n';
    }
    out << "Final: " << cpu.describe() << '\n';
}

void save_frame(const dmg::Bus &bus, const std::string &path) {
    if (path.empty())
        return;
    std::ofstream image(path, std::ios::binary);
    if (!image)
        throw std::runtime_error("cannot open frame output: " + path);
    image << "P5\n160 144\n255\n";
    for (const auto shade : bus.ppu.framebuffer)
        image.put(static_cast<char>(255U - (shade & 3U) * 85U));
    image.close();
    if (!image)
        throw std::runtime_error("cannot write frame output");
}
} // namespace

int main(int argc, char **argv) {
    try {
        std::string rom_path, report_path, frame_path, trace_path;
        std::string protocol = "blargg";
        std::uint64_t max_cycles = 4000000000ULL, frame_target = 0;
        std::size_t trace_capacity = 256;
        std::uint64_t wall_seconds = 180;
        std::optional<std::size_t> ram_override;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--help") {
                std::cout
                    << "dmg ROM.gb [--max-cycles N] [--timeout SECONDS] [--frames N]\n"
                       "           [--report result.json] [--frame-out image.pgm]\n"
                       "           [--trace-out trace.txt] [--trace-capacity N]\n"
                       "           [--ram-size BYTES] (explicit cartridge hardware override)\n"
                       "           [--protocol blargg|mooneye|mealybug]\n"
                       "Exit: 0 verified pass (or requested frame run completed), 1 ROM failure,\n"
                       "      2 configuration/error, 3 inconclusive timeout. Starts after DMG "
                       "boot.\n";
                return 0;
            }
            const auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::runtime_error("missing value for " + arg);
                return argv[i];
            };
            if (arg == "--max-cycles")
                max_cycles = number(value());
            else if (arg == "--timeout")
                wall_seconds = number(value());
            else if (arg == "--frames")
                frame_target = number(value());
            else if (arg == "--report")
                report_path = value();
            else if (arg == "--frame-out")
                frame_path = value();
            else if (arg == "--trace-out")
                trace_path = value();
            else if (arg == "--trace-capacity")
                trace_capacity = static_cast<std::size_t>(number(value()));
            else if (arg == "--ram-size")
                ram_override = static_cast<std::size_t>(number(value()));
            else if (arg == "--protocol")
                protocol = value();
            else if (!arg.empty() && arg.front() == '-')
                throw std::runtime_error("unknown option: " + arg);
            else if (rom_path.empty())
                rom_path = arg;
            else
                throw std::runtime_error("only one ROM may be supplied");
        }
        if (rom_path.empty())
            throw std::runtime_error("usage: dmg ROM.gb --help");
        if (protocol != "blargg" && protocol != "mooneye" && protocol != "mealybug")
            throw std::runtime_error("unknown test protocol");
        if (trace_capacity == 0 || trace_capacity > 1000000)
            throw std::runtime_error("trace capacity must be 1..1000000");
        if (wall_seconds == 0 || max_cycles == 0)
            throw std::runtime_error("timeouts must be positive");
        const auto size = std::filesystem::file_size(rom_path);
        if (size < 0x150 || size > 8U * 1024U * 1024U)
            throw std::runtime_error("ROM size outside 336 bytes..8 MiB");
        std::ifstream file(rom_path, std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open ROM: " + rom_path);
        std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>(file),
                                      std::istreambuf_iterator<char>()};
        dmg::Bus bus(std::move(rom), ram_override);
        dmg::Cpu cpu(bus);
        std::vector<Trace> ring(trace_capacity);
        std::size_t cursor = 0, serial_seen = 0;
        std::uint64_t steps = 0;
        bool memory_running = false;
        std::string status = "timeout", mechanism = "cycle limit";
        int exit_code = 3;
        const auto started = std::chrono::steady_clock::now();
        while (bus.cycles() < max_cycles) {
            auto &t = ring[cursor];
            t = {bus.cycles(),
                 cpu.pc,
                 cpu.sp,
                 {cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l},
                 {bus.peek(cpu.pc), bus.peek(static_cast<std::uint16_t>(cpu.pc + 1)),
                  bus.peek(static_cast<std::uint16_t>(cpu.pc + 2))},
                 cpu.ime,
                 bus.ppu.mode(),
                 bus.ppu.dot(),
                 bus.ppu.fifo_depth(),
                 bus.ppu.fetch_phase(),
                 bus.ppu.output_x(),
                 bus.peek(0xFF44),
                 bus.peek(0xFF41),
                 bus.ie,
                 bus.iflag,
                 bus.dma_active(),
                 bus.ppu.vram_blocked(),
                 bus.ppu.oam_blocked()};
            cursor = (cursor + 1) % ring.size();
            ++steps;
            const auto instructions_before = cpu.instructions;
            cpu.step();
            t.executed = cpu.instructions != instructions_before;
            t.opcode = t.executed ? cpu.last_opcode : 0;
            if (t.executed)
                t.bytes = cpu.last_bytes;
            if (protocol == "mealybug" && t.executed && cpu.last_opcode == 0x40) {
                // The author's capture protocol stops at this real software
                // breakpoint. Only comparison with the original PNG is a pass.
                status = "captured";
                mechanism = "Mealybug LD B,B framebuffer capture; not a test verdict";
                exit_code = 0;
                break;
            }
            if (protocol == "mooneye" && cpu.instructions != instructions_before &&
                cpu.last_opcode == 0x40) {
                const bool pass = cpu.b == 3 && cpu.c == 5 && cpu.d == 8 && cpu.e == 13 &&
                                  cpu.h == 21 && cpu.l == 34;
                const bool fail = cpu.b == 0x42 && cpu.c == 0x42 && cpu.d == 0x42 &&
                                  cpu.e == 0x42 && cpu.h == 0x42 && cpu.l == 0x42;
                if (pass || fail) {
                    status = pass ? "passed" : "failed";
                    mechanism = "Mooneye LD B,B register protocol";
                    exit_code = pass ? 0 : 1;
                    std::cout << "\nMooneye " << (pass ? "Passed" : "Failed") << " at "
                              << cpu.describe() << '\n';
                    break;
                }
            }
            if (cpu.locked) {
                status = "failed";
                mechanism = "illegal opcode lockup";
                exit_code = 1;
                break;
            }
            if (bus.serial_output.size() > serial_seen) {
                std::cout << bus.serial_output.substr(serial_seen) << std::flush;
                serial_seen = bus.serial_output.size();
                if (protocol == "blargg" &&
                    (bus.serial_output.find("Failed") != std::string::npos ||
                     bus.serial_output.find("FAILED") != std::string::npos)) {
                    status = "failed";
                    mechanism = "serial";
                    exit_code = 1;
                    break;
                }
                const auto pass = bus.serial_output.find("Passed");
                if (protocol == "blargg" && pass != std::string::npos &&
                    bus.serial_output.find('\n', pass) != std::string::npos) {
                    status = "passed";
                    mechanism = "serial";
                    exit_code = 0;
                    break;
                }
            }
            if ((steps & 1023U) == 0) {
                const bool signature = bus.peek(0xA001) == 0xDE && bus.peek(0xA002) == 0xB0 &&
                                       bus.peek(0xA003) == 0x61;
                if (protocol == "blargg" && signature) {
                    const auto code = bus.peek(0xA000);
                    if (code == 0x80)
                        memory_running = true;
                    if (memory_running && code < 0x80) {
                        std::cout << "\n[cartridge RAM test log]\n";
                        for (std::uint16_t address = 0xA004; address < 0xC000; ++address) {
                            const auto c = bus.peek(address);
                            if (c == 0)
                                break;
                            std::cout.put(static_cast<char>(c));
                        }
                        status = code == 0 ? "passed" : "failed";
                        mechanism = "cartridge RAM signature";
                        exit_code = code == 0 ? 0 : 1;
                        break;
                    }
                }
                if (std::chrono::steady_clock::now() - started >=
                    std::chrono::seconds(wall_seconds)) {
                    mechanism = "wall time limit";
                    break;
                }
            }
            if (frame_target != 0 && bus.ppu.frames() >= frame_target) {
                status = "completed";
                mechanism = "requested frames; not a test verdict";
                exit_code = 0;
                break;
            }
        }
        const double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        save_frame(bus, frame_path);
        if (exit_code != 0 || !trace_path.empty()) {
            if (trace_path.empty())
                dump_trace(std::cerr, ring, cursor, steps, cpu);
            else {
                std::ofstream trace(trace_path);
                if (!trace)
                    throw std::runtime_error("cannot open trace output");
                dump_trace(trace, ring, cursor, steps, cpu);
                trace.close();
                if (!trace)
                    throw std::runtime_error("cannot write trace output");
            }
        }
        std::ostringstream report;
        report << "{\n  \"status\": \"" << status << "\",\n  \"mechanism\": \"" << mechanism
               << "\",\n  \"t_cycles\": " << bus.cycles()
               << ",\n  \"m_cycles\": " << bus.cycles() / 4
               << ",\n  \"instructions\": " << cpu.instructions
               << ",\n  \"frames\": " << bus.ppu.frames() << ",\n  \"seconds\": " << seconds
               << ",\n  \"serial_bytes\": " << serial_seen
               << ",\n  \"cartridge_ram_bytes\": " << bus.cartridge.ram().size()
               << ",\n  \"ram_override\": " << (ram_override ? "true" : "false")
               << ",\n  \"registers\": {\"pc\": " << cpu.pc << ", \"sp\": " << cpu.sp
               << ", \"a\": " << unsigned(cpu.a) << ", \"f\": " << unsigned(cpu.f)
               << ", \"b\": " << unsigned(cpu.b) << ", \"c\": " << unsigned(cpu.c)
               << ", \"d\": " << unsigned(cpu.d) << ", \"e\": " << unsigned(cpu.e)
               << ", \"h\": " << unsigned(cpu.h) << ", \"l\": " << unsigned(cpu.l) << "}"
               << ",\n  \"ppu\": {\"mode\": " << bus.ppu.mode() << ", \"dot\": " << bus.ppu.dot()
               << ", \"ly\": " << unsigned(bus.peek(0xFF44))
               << ", \"stat\": " << unsigned(bus.peek(0xFF41))
               << ", \"fifo_depth\": " << bus.ppu.fifo_depth()
               << ", \"fetch_phase\": " << bus.ppu.fetch_phase()
               << ", \"output_x\": " << bus.ppu.output_x() << "}"
               << ",\n  \"bus\": {\"dma\": " << (bus.dma_active() ? "true" : "false")
               << ", \"vram_locked\": " << (bus.ppu.vram_blocked() ? "true" : "false")
               << ", \"oam_locked\": " << (bus.ppu.oam_blocked() ? "true" : "false")
               << ", \"ie\": " << unsigned(bus.ie) << ", \"if\": " << unsigned(bus.iflag)
               << "}\n}\n";
        std::cerr << '\n' << report.str();
        if (!report_path.empty()) {
            std::ofstream output(report_path);
            if (!output)
                throw std::runtime_error("cannot open report output");
            output << report.str();
            output.close();
            if (!output)
                throw std::runtime_error("cannot write report output");
        }
        std::cout.flush();
        std::cerr.flush();
        if (!std::cout || !std::cerr)
            throw std::runtime_error("cannot write execution log");
        return exit_code;
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        return 2;
    }
}

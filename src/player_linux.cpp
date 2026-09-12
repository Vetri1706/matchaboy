#include "arcade_library.hpp"
#include "dmg/autopsy.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "friend_session.hpp"
#include "gameboy_save.hpp"
#include "gba_core.hpp"
#include "keyboard_mapping.hpp"
#include "linux_audio.hpp"
#include "linux_keyboard_preferences.hpp"
#include "player_theme.hpp"
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <poll.h>
#include <spawn.h>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

extern char **environ;

namespace {
namespace key = matcha::keyboard;
using Clock = std::chrono::steady_clock;
constexpr auto frame_period = std::chrono::nanoseconds(16742706);
constexpr unsigned canvas_width = 1280, canvas_height = 900;
using RGB = std::uint32_t;
constexpr RGB background = 0x081218, panel = 0x10232c, foreground = 0xd9e6ef, muted = 0x7a99ab,
              cyan = 0x40d9ef, green = 0x73e699, orange = 0xffa653;
std::string hex(std::uint64_t value, unsigned width = 4) {
    std::ostringstream result;
    result << std::uppercase << std::hex << std::setfill('0') << std::setw(static_cast<int>(width))
           << value;
    return result.str();
}
unsigned number(const std::string &value, unsigned maximum) {
    std::size_t used = 0;
    unsigned long result = 0;
    try {
        result = std::stoul(value, &used, 0);
    } catch (...) {
        throw std::runtime_error("Invalid number: " + value);
    }
    if (used != value.size() || result > maximum || value.empty() || value[0] == '-')
        throw std::runtime_error("Number out of range: " + value);
    return static_cast<unsigned>(result);
}
std::string json(const std::string &value) {
    std::string result = "\"";
    for (const unsigned char c : value) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c == '\n')
            result += "\\n";
        else if (c < 32)
            result += ' ';
        else
            result += static_cast<char>(c);
    }
    return result + '"';
}
std::uint32_t crc(std::span<const std::uint8_t> bytes) {
    std::uint32_t result = 0xffffffffU;
    for (auto byte : bytes) {
        result ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            result = (result >> 1) ^ (0xedb88320U & (0U - (result & 1U)));
    }
    return ~result;
}
void be32(std::vector<std::uint8_t> &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void chunk(std::vector<std::uint8_t> &out, const char *name, std::span<const std::uint8_t> bytes) {
    be32(out, static_cast<std::uint32_t>(bytes.size()));
    const auto begin = out.size();
    out.insert(out.end(), name, name + 4);
    out.insert(out.end(), bytes.begin(), bytes.end());
    be32(out, crc(std::span<const std::uint8_t>(out).subspan(begin)));
}
// PNG RGB with stored DEFLATE blocks: screenshots need no zlib/runtime library.
void png(const std::filesystem::path &path, unsigned width, unsigned height,
         std::span<const RGB> pixels) {
    if (pixels.size() != static_cast<std::size_t>(width) * height)
        throw std::runtime_error("Invalid screenshot dimensions.");
    std::vector<std::uint8_t> raw;
    raw.reserve(height * (width * 3 + 1));
    for (unsigned y = 0; y < height; ++y) {
        raw.push_back(0);
        for (unsigned x = 0; x < width; ++x) {
            const auto p = pixels[y * width + x];
            raw.push_back(static_cast<std::uint8_t>(p >> 16));
            raw.push_back(static_cast<std::uint8_t>(p >> 8));
            raw.push_back(static_cast<std::uint8_t>(p));
        }
    }
    std::vector<std::uint8_t> deflate{0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (const auto byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    for (std::size_t offset = 0; offset < raw.size();) {
        const auto size = std::min<std::size_t>(65535, raw.size() - offset);
        deflate.push_back(offset + size == raw.size() ? 1 : 0);
        const auto n = static_cast<std::uint16_t>(size);
        deflate.push_back(n & 255);
        deflate.push_back(n >> 8);
        deflate.push_back(static_cast<std::uint8_t>(~n));
        deflate.push_back(static_cast<std::uint8_t>((~n) >> 8));
        deflate.insert(deflate.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                       raw.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    }
    be32(deflate, (b << 16) | a);
    std::vector<std::uint8_t> out{137, 80, 78, 71, 13, 10, 26, 10}, ihdr;
    be32(ihdr, width);
    be32(ihdr, height);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", deflate);
    chunk(out, "IEND", {});
    if (!path.parent_path().empty())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(out.data()),
               static_cast<std::streamsize>(out.size()));
    if (!file)
        throw std::runtime_error("Cannot write screenshot: " + path.string());
}

struct Runtime {
    std::unique_ptr<dmg::Autopsy> autopsy = std::make_unique<dmg::Autopsy>();
    std::unique_ptr<dmg::Bus> bus;
    std::unique_ptr<dmg::Cpu> cpu;
    std::unique_ptr<GbaCore> gba;
    // Detach the cable before either referenced CPU is destroyed.
    std::unique_ptr<matcha::FriendSession> link;
    std::filesystem::path rom;
    std::uint16_t buttons{};
    bool paused{}, inspector{}, green_palette{};
    Runtime() = default;
    explicit Runtime(const std::filesystem::path &path) : rom(path) {
        auto ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".gba") {
            gba = std::make_unique<GbaCore>(path);
            return;
        }
        if (ext != ".gb")
            throw std::runtime_error("Choose a Game Boy (.gb) or Game Boy Advance (.gba) ROM.");
        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("Cannot open ROM: " + path.string());
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                        std::istreambuf_iterator<char>()};
        if (input.bad() || bytes.size() < 0x150 || bytes.size() > 32 * 1024 * 1024)
            throw std::runtime_error("Invalid Game Boy cartridge.");
        if (bytes[0x143] == 0xc0)
            throw std::runtime_error("Game Boy Color-only games are not supported.");
        bus = std::make_unique<dmg::Bus>(std::move(bytes));
        cpu = std::make_unique<dmg::Cpu>(*bus);
        load_gameboy_save(path, *bus);
        bus->apu.set_sample_rate(0);
    }
    bool loaded() const { return bus || gba; }
    void set_buttons(std::uint16_t value) {
        buttons = value;
        if (link)
            return;
        if (bus)
            bus->set_buttons(static_cast<std::uint8_t>(value));
        if (gba) {
            constexpr std::array<unsigned, 10> order{4, 5, 6, 7, 0, 1, 2, 3, 9, 8};
            std::uint16_t mapped = 0;
            for (unsigned i = 0; i < 10; ++i)
                if (value & (1U << i))
                    mapped |= static_cast<std::uint16_t>(1U << order[i]);
            gba->set_buttons(mapped);
        }
    }
    void enable_audio() {
        if (link)
            return;
        if (gba)
            gba->enable_audio();
        if (bus)
            bus->apu.set_sample_rate(48000);
    }
    std::size_t drain(std::span<std::int16_t> output) {
        return link  ? link->drain_audio(output)
               : gba ? gba->drain_audio(output)
               : bus ? bus->apu.drain_samples(output)
                     : 0;
    }
    void flush() {
        if (gba)
            gba->flush_save();
        else if (bus)
            flush_gameboy_save(rom, *bus);
    }
    std::uint64_t frames() const {
        return link ? link->frames() : gba ? gba->frames() : bus ? bus->ppu.frames() : 0;
    }
    void step() {
        if (link || !loaded())
            return;
        if (gba)
            gba->step();
        else {
            bus->autopsy = autopsy.get();
            cpu->step();
        }
    }
    bool frame() {
        if (!loaded())
            return false;
        if (link)
            return link->advance(buttons);
        if (gba) {
            gba->run_frame();
            return true;
        }
        bus->autopsy = inspector ? autopsy.get() : nullptr;
        const auto target = bus->ppu.frames() + 1, deadline = bus->cycles() + 70224 * 2;
        while (bus->ppu.frames() < target && bus->cycles() < deadline) {
            const auto before = bus->cycles();
            cpu->step();
            if (bus->cycles() == before)
                break;
        }
        return bus->ppu.frames() >= target;
    }
    unsigned width() const { return gba ? 240 : 160; }
    unsigned height() const { return gba ? 160 : 144; }
    std::vector<RGB> pixels() const {
        std::vector<RGB> out(width() * height());
        if (gba) {
            const auto source = gba->pixels();
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto p = source[i];
                out[i] = ((p & 255) << 16) | (p & 0xff00) | ((p >> 16) & 255);
            }
        } else if (bus) {
            const auto &palette = green_palette ? matcha::theme::shades : matcha::theme::grayscale;
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto c = palette[bus->ppu.framebuffer[i] & 3];
                out[i] = (static_cast<unsigned>(c.r * 255) << 16) |
                         (static_cast<unsigned>(c.g * 255) << 8) | static_cast<unsigned>(c.b * 255);
            }
        }
        return out;
    }
    void capture(const std::filesystem::path &path) {
        png(path, width(), height(), pixels());
        std::ofstream report(path.string() + ".json");
        report << "{\"system\":" << json(gba ? "GBA" : "GB") << ",\"frames\":" << frames()
               << ",\"buttons\":" << buttons << ",\"width\":" << width()
               << ",\"height\":" << height();
        if (link)
            report << ",\"netplay\":{\"frames\":" << link->frames()
                   << ",\"verified_frames\":" << link->verified_frames()
                   << ",\"status\":" << json(link->status()) << "}";
        report << "}\n";
        if (!report)
            throw std::runtime_error("Cannot write screenshot report.");
    }
};
void advance_frames(Runtime &runtime, unsigned count) {
    if (!runtime.loaded())
        return;
    const auto target = runtime.frames() + count;
    auto progressed = Clock::now();
    unsigned idle_calls = 0;
    while (runtime.frames() < target) {
        const auto before = runtime.frames();
        runtime.frame();
        if (runtime.frames() > before) {
            progressed = Clock::now();
            idle_calls = 0;
        } else if (++idle_calls > 4096 || Clock::now() - progressed > std::chrono::seconds(5)) {
            throw std::runtime_error(
                "The ROM did not produce another LCD frame (LCD disabled or CPU stopped).");
        }
    }
}
struct Options {
    std::filesystem::path rom, capture, stop_file;
    std::string game, code, join;
    unsigned frames = 120, buttons = 0, input_frames = 0, port = 0;
    bool host{}, headless{}, window_test{}, library{}, paused{}, controls = true, inspector{},
                                                                 green{};
};
Options options(int argc, char **argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help") {
            std::cout << "Matchaboy Linux player\n  Matchaboy [ROM.gb|ROM.gba] [--game GAME_ID] "
                         "[--library]\n  --headless | --window-test --capture FILE.png --frames "
                         "N\n  --buttons MASK --input-frames N --paused --inspector --green "
                         "--hide-controls\n  --net-host PORT | --net-join HOST:PORT --net-code "
                         "32_HEX_CHARACTERS\n  --net-stop-file PATH\n";
            std::exit(0);
        } else if (arg == "--headless")
            result.headless = true;
        else if (arg == "--window-test")
            result.window_test = true;
        else if (arg == "--library")
            result.library = true;
        else if (arg == "--paused")
            result.paused = true;
        else if (arg == "--hide-controls")
            result.controls = false;
        else if (arg == "--inspector")
            result.inspector = true;
        else if (arg == "--green")
            result.green = true;
        else if (arg.starts_with("--")) {
            if (++i >= argc)
                throw std::runtime_error("Missing argument for " + arg);
            const std::string value = argv[i];
            if (arg == "--capture")
                result.capture = value;
            else if (arg == "--game")
                result.game = value;
            else if (arg == "--frames")
                result.frames = number(value, 10000000);
            else if (arg == "--buttons")
                result.buttons = number(value, 1023);
            else if (arg == "--input-frames")
                result.input_frames = number(value, 1000000);
            else if (arg == "--net-host") {
                result.host = true;
                result.port = number(value, 65535);
            } else if (arg == "--net-join")
                result.join = value;
            else if (arg == "--net-code")
                result.code = value;
            else if (arg == "--net-stop-file")
                result.stop_file = value;
            else
                throw std::runtime_error("Unknown option: " + arg);
        } else if (result.rom.empty())
            result.rom = arg;
        else
            throw std::runtime_error("Choose only one ROM.");
    }
    if (!result.code.empty() && !result.host && result.join.empty())
        throw std::runtime_error("--net-code requires --net-host or --net-join.");
    if (result.headless && result.window_test)
        throw std::runtime_error("Choose headless or window-test mode.");
    if ((result.headless || result.window_test) && result.capture.empty())
        throw std::runtime_error("Capture mode requires --capture FILE.png.");
    if (result.host && !result.join.empty())
        throw std::runtime_error("Choose host or join.");
    if (!result.game.empty() && !result.rom.empty())
        throw std::runtime_error("Choose a bundled game or ROM path.");
    if ((result.host || !result.join.empty()) && (result.library || result.paused))
        throw std::runtime_error("Friend play cannot start paused or in the library.");
    if (!result.stop_file.empty() && (!result.headless || (!result.host && result.join.empty())))
        throw std::runtime_error("--net-stop-file requires headless friend play.");
    return result;
}
matcha::FriendTransportOptions network_options(const Options &o) {
    matcha::FriendTransportOptions out;
    out.host = o.host;
    out.code = o.code;
    out.port = static_cast<std::uint16_t>(o.port);
    if (!o.host) {
        const auto separator = o.join.rfind(':');
        if (separator == std::string::npos)
            throw std::runtime_error("Use HOST:PORT for --net-join.");
        out.address = o.join.substr(0, separator);
        out.port = static_cast<std::uint16_t>(number(o.join.substr(separator + 1), 65535));
    }
    if (!out.port)
        throw std::runtime_error("Choose a non-zero UDP port.");
    return out;
}
std::unique_ptr<Runtime> start_link(const std::filesystem::path &rom,
                                    const matcha::FriendTransportOptions &config) {
    auto next = std::make_unique<Runtime>(rom);
    next->link = std::make_unique<matcha::FriendSession>(config, rom, next->bus.get(),
                                                         next->cpu.get(), next->gba.get());
    return next;
}

struct Hit {
    int x, y, w, h, command;
    bool enabled;
};
enum Command {
    Library = 1,
    Open,
    Settings,
    Controls,
    Pause,
    Inspector,
    Palette,
    Sound,
    Screenshot,
    Host,
    Join,
    Details,
    Disconnect,
    Step,
    Frame,
    ClosePane,
    ApplySettings,
    Balanced,
    Classic,
    SetAll,
    PickFile,
    ParentFolder,
    Connect,
    CopyCode,
    CopyDiagnostics,
    Quit,
    QuitWithoutSaving,
    GameCredits
};
enum class Pane { Closed, Keyboard, Files, Host, Join, Details, Error, SaveError };
constexpr std::array<int, 15> settings_commands{Balanced, Classic,   SetAll,       3002, 3001, 3003,
                                                3000,     3004,      3005,         3008, 3009, 3007,
                                                3006,     ClosePane, ApplySettings};
class Player {
    Display *display_{};
    Window window_{};
    GC gc_{};
    Atom delete_window_{}, clipboard_atom_{}, utf8_{}, targets_{}, paste_atom_{};
    XftDraw *text_draw_{};
    XftFont *font_{};
    unsigned font_pixels_{};
    std::map<RGB, XftColor> text_colors_;
    int width_ = 1280, height_ = 900;
    bool running_ = true, dirty_ = true, library_ = true, controls_ = true, muted_ = false,
         prior_paused_{}, library_prior_paused_{}, quit_prior_paused_{}, shutdown_saved_{},
         quit_without_save_{}, noninteractive_{};
    unsigned selected_{}, file_scroll_{}, settings_focus_{};
    std::uint64_t displayed_frames_{};
    key::Mapping mapping_ = key::balanced(), draft_ = key::balanced();
    std::unique_ptr<Runtime> runtime_;
    LinuxAudio audio_;
    Pane pane_ = Pane::Closed;
    std::vector<Hit> hits_;
    int capture_ = -1, field_ = 0, menu_ = -1;
    bool capture_all_{};
    std::string message_, error_, clipboard_, address_ = "127.0.0.1", port_ = "27888", code_,
                                              location_, room_code_, connection_host_;
    bool is_host_{};
    std::filesystem::path directory_, capture_path_;
    std::vector<std::filesystem::directory_entry> files_;
    std::future<std::unique_ptr<Runtime>> opening_;
    std::future<void> credits_opening_;
    std::string credits_status_ = "idle";
    Clock::time_point next_frame_ = Clock::now(), next_paint_ = Clock::now();
    bool pending() const { return opening_.valid(); }
    bool linked() const { return runtime_->link != nullptr; }
    bool game() const { return runtime_->loaded() && !library_; }
    int sx(int x) const { return x * width_ / static_cast<int>(canvas_width); }
    int sy(int y) const { return y * height_ / static_cast<int>(canvas_height); }
    unsigned long color(RGB c) const {
        const auto *visual = DefaultVisual(display_, DefaultScreen(display_));
        auto component = [](unsigned byte, unsigned long mask) {
            if (!mask)
                return 0UL;
            unsigned shift = 0;
            while (!(mask & 1UL)) {
                mask >>= 1;
                ++shift;
            }
            return ((byte * mask + 127) / 255) << shift;
        };
        return component((c >> 16) & 255, visual->red_mask) |
               component((c >> 8) & 255, visual->green_mask) |
               component(c & 255, visual->blue_mask);
    }
    void rect(int x, int y, int w, int h, RGB c) {
        XSetForeground(display_, gc_, color(c));
        XFillRectangle(display_, window_, gc_, sx(x), sy(y),
                       static_cast<unsigned>(std::max(1, sx(w))),
                       static_cast<unsigned>(std::max(1, sy(h))));
    }
    void update_font() {
        const auto scale = std::min(static_cast<double>(width_) / canvas_width,
                                    static_cast<double>(height_) / canvas_height);
        const auto size = static_cast<unsigned>(std::clamp(16.0 * scale, 13.0, 32.0));
        if (font_ && size == font_pixels_)
            return;
        const auto name = "monospace:pixelsize=" + std::to_string(size);
        auto *replacement = XftFontOpenName(display_, DefaultScreen(display_), name.c_str());
        if (!replacement)
            throw std::runtime_error(
                "Cannot load a readable system font. Install a Fontconfig monospace font.");
        if (font_)
            XftFontClose(display_, font_);
        font_ = replacement;
        font_pixels_ = size;
    }
    int text_width(const std::string &value) const {
        XGlyphInfo extents{};
        XftTextExtentsUtf8(display_, font_, reinterpret_cast<const FcChar8 *>(value.data()),
                           static_cast<int>(value.size()), &extents);
        return extents.xOff;
    }
    std::string clip(const std::string &value, std::size_t columns) const {
        if (value.size() <= columns || columns < 4)
            return value;
        auto end = columns - 3;
        while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
            --end;
        return value.substr(0, end) + "...";
    }
    std::string fit(const std::string &value, int logical_width) const {
        if (text_width(value) <= sx(logical_width))
            return value;
        auto end = value.size();
        while (end) {
            --end;
            while (end && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
                --end;
            auto result = value.substr(0, end) + "...";
            if (text_width(result) <= sx(logical_width))
                return result;
        }
        return "...";
    }
    void text(int x, int y, const std::string &value, RGB c = foreground) {
        auto found = text_colors_.find(c);
        if (found == text_colors_.end()) {
            XRenderColor channels{static_cast<unsigned short>(((c >> 16) & 255) * 257),
                                  static_cast<unsigned short>(((c >> 8) & 255) * 257),
                                  static_cast<unsigned short>((c & 255) * 257), 65535};
            XftColor allocated{};
            if (!XftColorAllocValue(display_, DefaultVisual(display_, DefaultScreen(display_)),
                                    DefaultColormap(display_, DefaultScreen(display_)), &channels,
                                    &allocated))
                throw std::runtime_error("Cannot allocate a display text color.");
            found = text_colors_.emplace(c, allocated).first;
        }
        XftDrawStringUtf8(text_draw_, &found->second, font_, sx(x), sy(y),
                          reinterpret_cast<const FcChar8 *>(value.data()),
                          static_cast<int>(value.size()));
    }
    void lines(int x, int y, const std::string &value, unsigned columns, unsigned limit,
               RGB c = foreground) {
        const auto glyph = std::max(1, text_width("M"));
        columns = std::max(1U, static_cast<unsigned>(sx(static_cast<int>(columns) * 9) / glyph));
        std::istringstream input(value);
        std::string word, row;
        unsigned line = 0;
        while (input >> word) {
            if (!row.empty() && row.size() + word.size() + 1 > columns) {
                text(x, y + static_cast<int>(line++) * 22, row, c);
                row.clear();
                if (line >= limit)
                    return;
            }
            if (!row.empty())
                row += ' ';
            row += word;
        }
        if (!row.empty() && line < limit)
            text(x, y + static_cast<int>(line) * 22, row, c);
    }
    void button(int x, int y, int w, const std::string &label, int command, bool enabled = true,
                int h = 38) {
        rect(x, y, w, h, enabled ? panel : 0x102027);
        text(x + 12, y + h / 2 + 5, fit(label, w - 24), enabled ? foreground : muted);
        if (pane_ == Pane::Keyboard && enabled && settings_commands[settings_focus_] == command) {
            XSetForeground(display_, gc_, color(cyan));
            XDrawRectangle(display_, window_, gc_, sx(x + 1), sy(y + 1),
                           static_cast<unsigned>(std::max(1, sx(w - 3))),
                           static_cast<unsigned>(std::max(1, sy(h - 3))));
        }
        hits_.push_back({x, y, w, h, command, enabled});
    }
    void field(int x, int y, int w, const std::string &value, int index) {
        rect(x, y, w, 38, field_ == index ? 0x1d404c : 0x152c35);
        const bool secret = index == 2 && (pane_ == Pane::Host || pane_ == Pane::Join);
        text(x + 10, y + 24, fit(secret ? std::string(value.size(), '*') : value, w - 24));
        hits_.push_back({x, y, w, 38, 2000 + index, true});
    }
    std::string key_label(unsigned bit) const { return key::key_name(mapping_[bit]); }
    void controls_panel() {
        rect(892, 174, 356, 616, panel);
        text(912, 205, "KEYBOARD CONTROLS", cyan);
        text(912, 233, "Game says A? Press " + key_label(4) + ".", muted);
        constexpr std::array<unsigned, 10> order{2, 1, 3, 0, 4, 5, 8, 9, 7, 6};
        int y = 274;
        for (unsigned bit : order) {
            if (bit >= 8 && !runtime_->gba)
                continue;
            const bool held = (runtime_->buttons & (1U << bit)) != 0;
            rect(1087, y - 21, 137, 31, held ? 0x215841 : 0x172f39);
            text(912, y, std::string(key::button_names[bit]), held ? green : foreground);
            text(1100, y, key_label(bit), held ? green : foreground);
            y += 41;
        }
        button(912, 715, 306, "Keyboard settings...  Ctrl+,", Settings);
        text(912, 778, "Esc: pause   Select: " + key_label(6), muted);
    }
    void framebuffer() {
        const auto pixels = runtime_->pixels();
        const unsigned source_w = runtime_->width(), source_h = runtime_->height();
        const int available = controls_ ? 824 : 1192;
        const int scale = std::max(1, std::min(sx(available) / static_cast<int>(source_w),
                                               sy(620) / static_cast<int>(source_h)));
        const unsigned w = source_w * static_cast<unsigned>(scale),
                       h = source_h * static_cast<unsigned>(scale);
        const int x = controls_ ? sx(36) + (sx(824) - static_cast<int>(w)) / 2
                                : (width_ - static_cast<int>(w)) / 2,
                  y = sy(172) + (sy(620) - static_cast<int>(h)) / 2;
        XImage *image =
            XCreateImage(display_, DefaultVisual(display_, DefaultScreen(display_)),
                         static_cast<unsigned>(DefaultDepth(display_, DefaultScreen(display_))),
                         ZPixmap, 0, nullptr, w, h, 32, 0);
        if (!image)
            throw std::runtime_error("Cannot allocate display image.");
        image->data =
            static_cast<char *>(std::calloc(static_cast<std::size_t>(image->bytes_per_line), h));
        if (!image->data) {
            XDestroyImage(image);
            throw std::bad_alloc();
        }
        for (unsigned py = 0; py < h; ++py)
            for (unsigned px = 0; px < w; ++px)
                XPutPixel(image, static_cast<int>(px), static_cast<int>(py),
                          color(pixels[(py / static_cast<unsigned>(scale)) * source_w +
                                       px / static_cast<unsigned>(scale)]));
        XPutImage(display_, window_, gc_, image, 0, 0, x, y, w, h);
        XDestroyImage(image);
    }
    void draw_library() {
        text(36, 145, "HOMEBREW LIBRARY", cyan);
        text(36, 174, "Pick a game, or open your own GB / GBA cartridge.", muted);
        const auto games = arcade_games();
        constexpr unsigned per_page = 6;
        if (games.empty()) {
            text(36, 230, "No bundled games are available. Use Open game to load a cartridge.");
            return;
        }
        selected_ = std::min(selected_, static_cast<unsigned>(games.size() - 1));
        const unsigned first = selected_ / per_page * per_page;
        const unsigned last = std::min(first + per_page, static_cast<unsigned>(games.size()));
        text(958, 174,
             std::to_string(games.size()) + " games  /  page " +
                 std::to_string(first / per_page + 1) + " of " +
                 std::to_string((games.size() + per_page - 1) / per_page),
             muted);
        for (unsigned i = first; i < last; ++i) {
            const unsigned slot = i - first;
            const int x = 36 + static_cast<int>(slot % 2) * 620,
                      y = 198 + static_cast<int>(slot / 2) * 172;
            const auto &g = games[i];
            rect(x, y, 594, 158, i == selected_ ? 0x1c3c45 : panel);
            text(x + 18, y + 27, fit(g.title, 558),
                 i == selected_ ? cyan : foreground);
            text(x + 18, y + 51, fit("By " + std::string(g.author), 558), muted);
            text(x + 18, y + 75, fit(std::string(g.system) + "  /  " + g.players, 558), green);
            text(x + 18, y + 99, fit(g.description, 558), muted);
            text(x + 18, y + 123, fit("License: " + std::string(g.license), 558), muted);
            text(x + 18, y + 147, "PLAY  /  " + std::string(g.genre), green);
            hits_.push_back({x, y, 594, 158, 100 + static_cast<int>(i), !pending() && !linked()});
        }
        text(36, 748, fit(std::string(games[selected_].title) + "  /  " + games[selected_].source,
                          1200), cyan);
        lines(36, 780, key::arcade_help(games[selected_].controls, mapping_), 125, 3);
        text(36, 884,
             "Arrow keys: browse   Enter: play   Ctrl+O: open game   Ctrl+,: keyboard settings",
             muted);
    }
    void draw_inspector() {
        rect(28, 165, 1224, 637, panel);
        text(48, 194, runtime_->gba ? "GBA HARDWARE INSPECTOR" : "DMG SILICON AUTOPSY", cyan);
        if (runtime_->gba) {
            const auto state = runtime_->gba->inspect(0x02000000);
            for (unsigned i = 0; i < 16; ++i)
                text(48 + static_cast<int>(i / 8) * 240, 233 + static_cast<int>(i % 8) * 28,
                     "R" + std::to_string(i) + "  " + hex(state.registers[i], 8));
            text(545, 233, "CPSR " + hex(state.cpsr, 8));
            text(545, 267, "DISPCNT " + hex(state.dispcnt) + "  DISPSTAT " + hex(state.dispstat));
            text(545, 301, "VCOUNT " + std::to_string(state.vcount));
            text(545, 335, "SIOCNT " + hex(state.siocnt) + "  RCNT " + hex(state.rcnt));
            text(545, 369, "IF " + hex(state.interrupt_flags) + "  SEND " + hex(state.serial_send));
            text(48, 493, "EWRAM 02000000", cyan);
            for (unsigned row = 0; row < 12; ++row) {
                std::string value = hex(0x02000000 + row * 16, 8) + "  ";
                for (unsigned col = 0; col < 16; ++col)
                    value += hex(state.memory[row * 16 + col], 2) + " ";
                text(48, 522 + static_cast<int>(row) * 21, value);
            }
        } else {
            runtime_->autopsy->capture(*runtime_->cpu, *runtime_->bus);
            const auto state = runtime_->autopsy->snapshot();
            text(48, 231, runtime_->cpu->describe());
            text(48, 261,
                 "Cycles " + std::to_string(state.cycles) + "  LY " + std::to_string(state.ly) +
                     " DOT " + std::to_string(state.dot) + " MODE " + std::to_string(state.mode));
            text(48, 291,
                 "VRAM " + std::string(state.vram_locked ? "LOCKED" : "OPEN") + "  OAM " +
                     (state.oam_locked ? "LOCKED" : "OPEN") + "  IE " + hex(state.ie, 2) + " IF " +
                     hex(state.interrupt_flags, 2));
            text(48, 335, "PIXEL FIFO / NEXT OUTPUT AT LEFT", cyan);
            for (unsigned i = 0; i < 8; ++i) {
                const int x = 48 + static_cast<int>(i) * 72;
                rect(x, 353, 61, 45, 0x25453b);
                text(x + 12, 379, std::to_string(state.background_fifo[i]), green);
                text(x + 9, 424, hex(state.background_tile_ids[i], 3), muted);
                rect(x, 440, 61, 45, 0x453d29);
                text(x + 12, 466, std::to_string(state.object_fifo[i]), orange);
                text(x + 9, 511, hex(state.object_tile_ids[i], 3), muted);
            }
            text(48, 548,
                 "Fetcher " + std::to_string(state.fetch_phase) + " / tick " +
                     std::to_string(state.fetch_ticks) + "  tile " + hex(state.tile, 2));
            text(48, 579,
                 "SCX discard " + std::to_string(state.fine_discard) + "  window " +
                     std::to_string(state.window) + "  object fetch " +
                     std::to_string(state.object_fetching));
            text(710, 232, "RETIRED INSTRUCTIONS", cyan);
            const unsigned count = std::min(22U, state.trace_count);
            for (unsigned i = 0; i < count; ++i) {
                const auto &trace = state.trace[state.trace_count - count + i];
                text(710, 263 + static_cast<int>(i) * 22,
                     hex(trace.pc) + "  " + clip(trace.text.data(), 47));
            }
        }
        button(48, 744, 180, "Step  Ctrl+S", Step, !linked());
        button(244, 744, 180, "Frame  Ctrl+F", Frame, !linked());
        text(450, 769, "Tab returns to the game. Inspection does not change linked input.", muted);
    }
    void draw_menu() {
        if (menu_ < 0)
            return;
        const int x = 24 + menu_ * 132, y = 41;
        std::vector<std::pair<std::string, int>> entries;
        switch (menu_) {
        case 0:
            entries = {{"Open game...  Ctrl+O", Open},
                       {"Library", Library},
                       {"Screenshot  F12", Screenshot},
                       {"Quit  Ctrl+Q", Quit}};
            break;
        case 1:
            entries = {{"Pause / Resume  Esc", Pause},
                       {"Sound on / off", Sound},
                       {"Game credits and licenses", GameCredits}};
            break;
        case 2:
            entries = {{"Controls  Ctrl+Shift+C", Controls},
                       {"Inspector  Tab", Inspector},
                       {"Change GB palette", Palette}};
            break;
        case 3:
            entries = {{"Host friend play...", Host},
                       {"Join friend play...", Join},
                       {"Connection details", Details},
                       {"Disconnect", Disconnect}};
            break;
        default:
            entries = {{"Keyboard settings  Ctrl+,", Settings},
                       {"Step instruction  Ctrl+S", Step},
                       {"Advance frame  Ctrl+F", Frame}};
            break;
        }
        rect(x - 4, y - 4, 306, static_cast<int>(entries.size()) * 42 + 8, 0x25404c);
        int row = y;
        for (const auto &[label, command] : entries) {
            button(x, row, 298, label, command, allowed(command), 40);
            row += 42;
        }
    }
    bool allowed(int command) const {
        if (command == Quit)
            return true;
        if (pending())
            return false;
        if (command == Library || command == Open || command == Host || command == Join)
            return !linked() && ((command != Host && command != Join) || runtime_->loaded());
        if (command == Pause || command == Step || command == Frame)
            return game() && !linked();
        if (command == Details || command == Disconnect)
            return linked();
        if (command == Screenshot)
            return true;
        if (command == GameCredits)
            return !credits_opening_.valid();
        if (command == Inspector || command == Palette || command == Sound || command == Controls)
            return game();
        return true;
    }
    void draw_pane() {
        if (pane_ == Pane::Closed)
            return;
        rect(180, 146, 920, 650, 0x243943);
        rect(184, 150, 912, 642, background);
        if (pane_ == Pane::Keyboard) {
            text(216, 185, "KEYBOARD SETTINGS", cyan);
            text(216, 217,
                 "Keys use physical US positions. Choose a key, then press its replacement.",
                 muted);
            button(216, 236, 198, "Balanced", Balanced);
            button(430, 236, 198, "Classic", Classic);
            button(644, 236, 230, "Set all keys...", SetAll);
            constexpr std::array<unsigned, 10> order{2, 1, 3, 0, 4, 5, 8, 9, 7, 6};
            for (unsigned row = 0; row < order.size(); ++row) {
                const auto bit = order[row];
                const int x = 216 + static_cast<int>(row / 5) * 428,
                          y = 302 + static_cast<int>(row % 5) * 65;
                text(x, y + 25, std::string(key::button_names[bit]));
                button(x + 140, y, 240,
                       capture_ == static_cast<int>(bit) ? "Press a key..."
                                                         : key::key_name(draft_[bit]),
                       3000 + static_cast<int>(bit));
            }
            text(216, 664,
                 "Tab / Shift+Tab: focus. Enter / Space: activate. Escape: cancel capture.", muted);
            const auto problem = key::validate_mapping(draft_);
            lines(216, 696, error_.empty() ? problem : error_, 88, 2, orange);
            button(216, 741, 155, "Cancel", ClosePane);
            button(894, 741, 170, "Apply", ApplySettings, capture_ < 0 && problem.empty());
        } else if (pane_ == Pane::Files) {
            text(216, 185, "OPEN GAME", cyan);
            text(216, 214, "Browse folders, or enter the full path to a .gb or .gba ROM.", muted);
            field(216, 232, 710, location_, 0);
            button(940, 232, 125, "Open", PickFile);
            button(216, 285, 110, "Up", ParentFolder);
            text(344, 310, clip(directory_.string(), 80), muted);
            for (unsigned row = 0; row < 9 && row + file_scroll_ < files_.size(); ++row) {
                const auto &entry = files_[row + file_scroll_];
                std::error_code ec;
                const bool folder = entry.is_directory(ec);
                button(216, 333 + static_cast<int>(row) * 40, 848,
                       (folder ? "[folder] " : "[game]   ") +
                           clip(entry.path().filename().string(), 84),
                       10000 + static_cast<int>(row + file_scroll_), true, 36);
            }
            text(216, 720, "Mouse wheel / Page Up / Page Down: scroll. Enter opens the typed path.",
                 muted);
            button(216, 741, 155, "Cancel", ClosePane);
            lines(393, 762, error_, 70, 1, orange);
        } else if (pane_ == Pane::Host || pane_ == Pane::Join) {
            const bool host = pane_ == Pane::Host;
            text(216, 185, host ? "HOST FRIEND PLAY" : "JOIN FRIEND PLAY", cyan);
            text(216, 220,
                 "Both players need the same game ROM. Starting a link restarts the game.", muted);
            int y = 266;
            if (!host) {
                text(216, y + 24, "Host IP / name");
                field(442, y, 620, address_, 0);
                y += 74;
            }
            text(216, y + 24, "UDP port");
            field(442, y, 210, port_, 1);
            y += 74;
            text(216, y + 24, "Room code");
            field(442, y, 620, code_, 2);
            y += 66;
            if (host)
                button(442, y, 242, "Copy room code", CopyCode);
            lines(
                216, host ? 502 : 559,
                "Same Wi-Fi: your friend enters this computer's LAN IP. Internet play needs a VPN "
                "address or forwarded host UDP port. Room codes and game data travel unencrypted.",
                92, 4, muted);
            lines(216, 695, error_, 90, 2, orange);
            button(216, 741, 155, "Cancel", ClosePane);
            button(846, 741, 218, host ? "Start hosting" : "Join friend", Connect);
        } else if (pane_ == Pane::Details) {
            text(216, 185, "FRIEND PLAY CONNECTION", cyan);
            text(216, 223, runtime_->link ? clip(runtime_->link->status(), 94) : "Session ended.",
                 green);
            text(216, 257,
                 "Player " + std::string(is_host_ ? "1 (host)" : "2 (guest)") + "  /  UDP " +
                     (runtime_->link ? std::to_string(runtime_->link->port()) : "-"),
                 muted);
            if (runtime_->link) {
                std::istringstream input(runtime_->link->diagnostics());
                std::string row;
                int y = 306;
                while (std::getline(input, row) && y < 649) {
                    text(216, y, clip(row, 96));
                    y += 25;
                }
            }
            text(216, 693, "If the link stalls, copy diagnostics before disconnecting.", muted);
            button(216, 741, 155, "Close", ClosePane);
            button(406, 741, 228, "Copy room code", CopyCode);
            button(666, 741, 270, "Copy diagnostics", CopyDiagnostics);
        } else if (pane_ == Pane::SaveError) {
            text(216, 185, "UNABLE TO SAVE PROGRESS", orange);
            lines(216, 247, message_, 91, 11, foreground);
            lines(216, 566,
                  "Your game is still in memory. Fix the save folder and retry, or keep playing. "
                  "Quit without saving discards the progress since your last successful save.",
                  91, 5, muted);
            button(216, 741, 235, "Retry save and quit", Quit);
            button(473, 741, 190, "Keep playing", ClosePane);
            button(686, 741, 378, "Quit without saving", QuitWithoutSaving);
        } else {
            text(216, 185, "MATCHABOY", cyan);
            lines(216, 247, message_, 91, 18, orange);
            button(216, 741, 155, "Close", ClosePane);
        }
    }
    void paint() {
        hits_.clear();
        rect(0, 0, 1280, 900, background);
        rect(0, 0, 1280, 44, panel);
        const std::array<std::string, 5> menus{"File", "Game", "View", "Netplay", "Tools"};
        for (unsigned i = 0; i < menus.size(); ++i) {
            text(36 + static_cast<int>(i) * 132, 28, menus[i],
                 menu_ == static_cast<int>(i) ? cyan : foreground);
            hits_.push_back({24 + static_cast<int>(i) * 132, 0, 132, 42, 5000 + static_cast<int>(i),
                             pane_ == Pane::Closed});
        }
        rect(0, 46, 1280, 64, 0x0e242c);
        text(32, 85, "MATCHABOY", cyan);
        text(180, 85, "GB + GBA", muted);
        button(675, 57, 118, "Library", Library, allowed(Library));
        button(807, 57, 148, "Open game...", Open, allowed(Open));
        button(969, 57, 277, "Keyboard settings...", Settings, allowed(Settings));
        if (library_ || !runtime_->loaded())
            draw_library();
        else {
            text(36, 143, clip(runtime_->rom.filename().string(), 75), foreground);
            button(910, 119, 160, controls_ ? "Hide controls" : "Show controls", Controls);
            button(1086, 119, 160, runtime_->inspector ? "Back to game" : "Inspector", Inspector);
            if (runtime_->inspector)
                draw_inspector();
            else {
                framebuffer();
                if (controls_)
                    controls_panel();
            }
            button(36, 819, 180, runtime_->paused ? "Resume  Esc" : "Pause  Esc", Pause, !linked());
            button(233, 819, 180,
                   !audio_.available() ? "Audio unavailable"
                   : muted_            ? "Sound: off"
                                       : "Sound: on",
                   Sound);
            text(438, 844,
                 linked()           ? clip(runtime_->link->status(), 87)
                 : runtime_->paused ? "Paused"
                                    : "Playing",
                 linked() && runtime_->link->finished() ? orange : green);
            text(36, 887,
                 "Ctrl+,: settings   Ctrl+Shift+C: guide   Tab: inspector   F12: screenshot",
                 muted);
        }
        if (pending()) {
            rect(200, 375, 880, 115, panel);
            text(229, 418, "Starting friend play...", cyan);
            text(229, 453,
                 "Resolving the host and preparing the link. The window remains responsive.",
                 muted);
        }
        if (pane_ != Pane::Closed) {
            hits_.clear();
            draw_pane();
        } else
            draw_menu();
        XFlush(display_);
        dirty_ = false;
        displayed_frames_ = runtime_->frames();
    }
    std::array<bool, 256> pressed_{}, keys_down_{};
    void clear_input() {
        pressed_.fill(false);
        runtime_->set_buttons(0);
    }
    void sync_input() {
        std::uint16_t buttons = 0;
        for (unsigned code = 0; code < pressed_.size(); ++code)
            if (pressed_[code]) {
                const auto bit = key::key_bit(mapping_, key::from_linux_keycode(code));
                if (bit < 10)
                    buttons |= static_cast<std::uint16_t>(1U << bit);
            }
        runtime_->set_buttons(buttons);
        dirty_ = true;
    }
    void open_pane(Pane next) {
        clear_input();
        menu_ = -1;
        prior_paused_ = runtime_->paused;
        if (!linked())
            runtime_->paused = true;
        audio_.clear();
        pane_ = next;
        error_.clear();
        field_ = 0;
        dirty_ = true;
    }
    void close_pane() {
        capture_ = -1;
        capture_all_ = false;
        error_.clear();
        pane_ = Pane::Closed;
        clear_input();
        runtime_->paused = prior_paused_;
        next_frame_ = Clock::now();
        dirty_ = true;
    }
    void show_error(const std::string &what) {
        if (pane_ != Pane::Closed) {
            error_ = what;
            if (pane_ == Pane::Error)
                message_ = what;
            dirty_ = true;
            return;
        }
        open_pane(Pane::Error);
        message_ = what;
    }
    void load(const std::filesystem::path &path) {
        if (linked() || pending())
            throw std::runtime_error("Disconnect friend play before changing games.");
        runtime_->flush();
        auto next = std::make_unique<Runtime>(path);
        next->enable_audio();
        next->inspector = false;
        next->frame();
        if (pane_ != Pane::Closed)
            close_pane();
        runtime_ = std::move(next);
        library_ = false;
        clear_input();
        audio_.clear();
        next_frame_ = Clock::now();
        dirty_ = true;
    }
    void list_files(const std::filesystem::path &path) {
        auto directory = std::filesystem::absolute(path);
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto &entry : std::filesystem::directory_iterator(
                 directory, std::filesystem::directory_options::skip_permission_denied)) {
            std::error_code ec;
            if (entry.is_directory(ec)) {
                entries.push_back(entry);
                continue;
            }
            auto extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if ((extension == ".gb" || extension == ".gba") && entry.is_regular_file(ec))
                entries.push_back(entry);
        }
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            std::error_code ea, eb;
            const auto ad = a.is_directory(ea), bd = b.is_directory(eb);
            return ad != bd ? ad : a.path().filename() < b.path().filename();
        });
        files_ = std::move(entries);
        directory_ = std::move(directory);
        location_ = directory_.string() + "/";
        file_scroll_ = 0;
        error_.clear();
        dirty_ = true;
    }
    std::string &active_field() {
        if (pane_ == Pane::Files)
            return location_;
        if (field_ == 0)
            return address_;
        if (field_ == 1)
            return port_;
        return code_;
    }
    void input_text(const std::string &value) {
        auto &field = active_field();
        const auto limit = pane_ == Pane::Files ? 4096U
                           : field_ == 2        ? 32U
                           : field_ == 1        ? 5U
                                                : 253U;
        for (const unsigned char c : value) {
            if (c < 32 || c == 127 || field.size() >= limit)
                continue;
            if (pane_ != Pane::Files && field_ == 1 && !std::isdigit(c))
                continue;
            if (pane_ != Pane::Files && field_ == 2 && !std::isxdigit(c))
                continue;
            field += static_cast<char>(c);
        }
        dirty_ = true;
    }
    void set_clipboard(std::string value) {
        clipboard_ = std::move(value);
        XSetSelectionOwner(display_, clipboard_atom_, window_, CurrentTime);
        XSetSelectionOwner(display_, XA_PRIMARY, window_, CurrentTime);
        message_ = "Copied to clipboard";
        dirty_ = true;
    }
    void connect() {
        Options o;
        o.host = pane_ == Pane::Host;
        o.port = number(port_, 65535);
        o.code = code_;
        o.join = o.host ? "" : address_ + ":" + port_;
        const auto config = network_options(o);
        if (config.code.size() != 32 ||
            !std::all_of(config.code.begin(), config.code.end(),
                         [](unsigned char c) { return std::isxdigit(c) != 0; }))
            throw std::runtime_error("Enter the 32-character room code from your friend.");
        runtime_->flush();
        const auto path = runtime_->rom;
        close_pane();
        runtime_->paused = true;
        clear_input();
        audio_.clear();
        room_code_ = config.code;
        connection_host_ = config.address;
        is_host_ = config.host;
        // DNS and save/ROM setup run on an isolated runtime. Xlib and active
        // consoles remain exclusively owned by this UI thread.
        opening_ =
            std::async(std::launch::async, [path, config] { return start_link(path, config); });
        dirty_ = true;
    }
    void request_quit() {
        if (pane_ != Pane::SaveError)
            quit_prior_paused_ = pane_ == Pane::Closed ? runtime_->paused : prior_paused_;
        clear_input();
        // Detach before writing the local cartridge, retaining both the CPU
        // and its in-memory save if the filesystem rejects the write.
        if (runtime_->link) {
            runtime_->link->close();
            runtime_->link.reset();
            runtime_->enable_audio();
        }
        runtime_->paused = true;
        audio_.clear();
        try {
            runtime_->flush();
            shutdown_saved_ = true;
            running_ = false;
        } catch (const std::exception &e) {
            if (noninteractive_)
                throw;
            message_ = e.what();
            pane_ = Pane::SaveError;
            prior_paused_ = quit_prior_paused_;
            capture_ = -1;
            capture_all_ = false;
            menu_ = -1;
            error_.clear();
            dirty_ = true;
        }
    }
    void command(int command) {
        if (pane_ == Pane::Keyboard) {
            const auto found =
                std::find(settings_commands.begin(), settings_commands.end(), command);
            if (found != settings_commands.end())
                settings_focus_ = static_cast<unsigned>(found - settings_commands.begin());
        }
        if (command >= 5000 && command < 5005) {
            menu_ = menu_ == command - 5000 ? -1 : command - 5000;
            clear_input();
            dirty_ = true;
            return;
        }
        if (command >= 10000) {
            const auto index = static_cast<std::size_t>(command - 10000);
            if (index >= files_.size())
                return;
            const auto path = files_[index].path();
            std::error_code ec;
            if (files_[index].is_directory(ec))
                list_files(path);
            else
                load(path);
            return;
        }
        if (command >= 3000 && command < 3010) {
            capture_ = command - 3000;
            capture_all_ = false;
            error_.clear();
            dirty_ = true;
            return;
        }
        if (command >= 2000 && command < 2003) {
            field_ = command - 2000;
            dirty_ = true;
            return;
        }
        if (command >= 100 && command < 100 + static_cast<int>(arcade_games().size())) {
            selected_ = static_cast<unsigned>(command - 100);
            load(arcade_rom_path(selected_));
            return;
        }
        if (pane_ == Pane::Closed && !allowed(command))
            return;
        menu_ = -1;
        dirty_ = true;
        switch (command) {
        case Library:
            clear_input();
            if (!library_)
                library_prior_paused_ = runtime_->paused;
            library_ = true;
            runtime_->paused = true;
            audio_.clear();
            break;
        case Open: {
            open_pane(Pane::Files);
            const char *home = std::getenv("HOME");
            try {
                list_files(runtime_->loaded() ? runtime_->rom.parent_path()
                           : home             ? std::filesystem::path(home)
                                              : std::filesystem::current_path());
            } catch (const std::exception &e) {
                error_ = e.what();
            }
            break;
        }
        case Settings:
            open_pane(Pane::Keyboard);
            settings_focus_ = 0;
            draft_ = mapping_;
            capture_ = -1;
            capture_all_ = false;
            break;
        case GameCredits: {
            const auto path = std::filesystem::absolute(arcade_credits_path()).string();
            if (!std::filesystem::is_regular_file(path))
                throw std::runtime_error("The game credits file is missing: " + path);
            clear_input();
            auto completion = std::make_shared<std::promise<void>>();
            auto result = completion->get_future();
            // Spawn and reap on a worker: some desktop launchers wait until
            // their viewer closes. Never block rendering, netplay, or app exit.
            std::thread([completion, path] {
                try {
                    std::array<char *, 3> arguments{
                        const_cast<char *>("xdg-open"), const_cast<char *>(path.c_str()), nullptr};
                    pid_t child{};
                    const int spawned =
                        posix_spawnp(&child, "xdg-open", nullptr, nullptr, arguments.data(), environ);
                    if (spawned != 0)
                        throw std::runtime_error("Cannot open the game credits (" +
                                                 std::string(std::strerror(spawned)) +
                                                 "). Install xdg-utils and a text viewer, or open " +
                                                 path + " in your text editor.");
                    int status{};
                    pid_t waited{};
                    do {
                        waited = waitpid(child, &status, 0);
                    } while (waited < 0 && errno == EINTR);
                    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
                        throw std::runtime_error(
                            "The desktop could not open the game credits. Choose a default text "
                            "viewer, or open " + path + " in your text editor.");
                    completion->set_value();
                } catch (...) {
                    completion->set_exception(std::current_exception());
                }
            }).detach();
            credits_opening_ = std::move(result);
            credits_status_ = "opening";
            break;
        }
        case Controls:
            controls_ = !controls_;
            break;
        case Pause:
            clear_input();
            runtime_->paused = !runtime_->paused;
            audio_.clear();
            break;
        case Inspector:
            runtime_->inspector = !runtime_->inspector;
            break;
        case Palette:
            runtime_->green_palette = !runtime_->green_palette;
            break;
        case Sound:
            if (!audio_.available()) {
                show_error(audio_.status());
            } else {
                muted_ = !muted_;
                audio_.clear();
            }
            break;
        case Screenshot: {
            const char *home = std::getenv("HOME");
            const auto directory =
                (home ? std::filesystem::path(home) : std::filesystem::current_path()) /
                "Pictures/Matchaboy";
            const auto path =
                capture_path_.empty()
                    ? directory /
                          ("Matchaboy-" +
                           std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count()) +
                           ".png")
                    : capture_path_;
            capture_window(path);
            std::cout << "Screenshot saved: " << path << std::endl;
            break;
        }
        case Host:
            open_pane(Pane::Host);
            code_ = matcha::FriendSession::make_room_code();
            field_ = 1;
            break;
        case Join:
            open_pane(Pane::Join);
            code_.clear();
            field_ = 0;
            break;
        case Details:
            open_pane(Pane::Details);
            break;
        case Disconnect:
            runtime_->link->close();
            runtime_->link.reset();
            room_code_.clear();
            clear_input();
            runtime_->paused = true;
            runtime_->enable_audio();
            audio_.clear();
            break;
        case Step:
            runtime_->paused = true;
            clear_input();
            runtime_->step();
            audio_.clear();
            break;
        case Frame:
            runtime_->paused = true;
            clear_input();
            runtime_->frame();
            audio_.clear();
            break;
        case ClosePane:
            close_pane();
            break;
        case ApplySettings:
            if (capture_ < 0 && key::validate_mapping(draft_).empty()) {
                key::save_linux_mapping(key::linux_preferences_path(), draft_);
                mapping_ = draft_;
                close_pane();
            }
            break;
        case Balanced:
            draft_ = key::balanced();
            capture_ = -1;
            capture_all_ = false;
            error_.clear();
            break;
        case Classic:
            draft_ = key::classic();
            capture_ = -1;
            capture_all_ = false;
            error_.clear();
            break;
        case SetAll:
            capture_ = 2;
            capture_all_ = true;
            error_.clear();
            break;
        case PickFile: {
            auto path = std::filesystem::path(location_);
            if (path.is_relative())
                path = directory_ / path;
            if (std::filesystem::is_directory(path))
                list_files(path);
            else
                load(path);
            break;
        }
        case ParentFolder:
            list_files(directory_.parent_path());
            break;
        case Connect:
            connect();
            break;
        case CopyCode:
            set_clipboard(pane_ == Pane::Host ? code_ : room_code_);
            break;
        case CopyDiagnostics:
            if (runtime_->link)
                set_clipboard(runtime_->link->diagnostics() +
                              "\nLinux event-loop link service: active");
            break;
        case Quit:
            request_quit();
            break;
        case QuitWithoutSaving:
            if (pane_ == Pane::SaveError) {
                quit_without_save_ = true;
                running_ = false;
            }
            break;
        default:
            break;
        }
    }
    void capture_key(const XKeyEvent &event, KeySym symbol) {
        if (symbol == XK_Escape) {
            capture_ = -1;
            capture_all_ = false;
            error_.clear();
            dirty_ = true;
            return;
        }
        if (event.state & (ControlMask | Mod1Mask | Mod4Mask)) {
            error_ = "Choose one key without Ctrl, Alt or Super.";
            dirty_ = true;
            return;
        }
        const auto code = key::from_linux_keycode(event.keycode);
        if (!key::supported_key(code) || key::reserved_key(code)) {
            error_ = key::key_name(code) + " is not available for a game button.";
            dirty_ = true;
            return;
        }
        draft_[static_cast<unsigned>(capture_)] = key::canonical_key(code);
        error_.clear();
        if (capture_all_) {
            constexpr std::array<unsigned, 10> order{2, 1, 3, 0, 4, 5, 8, 9, 7, 6};
            const auto current =
                std::find(order.begin(), order.end(), static_cast<unsigned>(capture_));
            if (current != order.end() && current + 1 != order.end())
                capture_ = static_cast<int>(*(current + 1));
            else {
                capture_ = -1;
                capture_all_ = false;
            }
        } else
            capture_ = -1;
        dirty_ = true;
    }
    void key_event(XKeyEvent event, bool down) {
        const auto symbol = XLookupKeysym(&event, 0);
        const bool control = (event.state & ControlMask) != 0;
        bool repeat = false;
        if (event.keycode < keys_down_.size()) {
            repeat = down && keys_down_[event.keycode];
            keys_down_[event.keycode] = down;
        }
        if (down && symbol == XK_F12 && capture_ < 0) {
            if (!repeat)
                command(Screenshot);
            return;
        }
        // Controller state is held, not repeated. Commands fire once per
        // physical press; only editable text fields accept key autorepeat.
        if (repeat && pane_ != Pane::Files && pane_ != Pane::Host && pane_ != Pane::Join)
            return;
        if (!down) {
            if (event.keycode < pressed_.size())
                pressed_[event.keycode] = false;
            if (pane_ == Pane::Closed && !library_)
                sync_input();
            return;
        }
        // X11's state field describes modifiers before this event, so a
        // newly pressed Ctrl/Alt/Super must also be recognized by its keysym.
        const bool shortcut_modifier =
            symbol == XK_Control_L || symbol == XK_Control_R || symbol == XK_Alt_L ||
            symbol == XK_Alt_R || symbol == XK_Meta_L || symbol == XK_Meta_R ||
            symbol == XK_Super_L || symbol == XK_Super_R || symbol == XK_Hyper_L ||
            symbol == XK_Hyper_R || symbol == XK_ISO_Level3_Shift;
        if (shortcut_modifier) {
            clear_input();
            if (pane_ == Pane::Keyboard && capture_ >= 0)
                capture_key(event, symbol);
            return;
        }
        if (pane_ == Pane::Keyboard) {
            if (capture_ >= 0) {
                if (event.keycode < pressed_.size()) {
                    if (pressed_[event.keycode])
                        return;
                    pressed_[event.keycode] = true;
                }
                capture_key(event, symbol);
                return;
            }
            if (symbol == XK_Escape) {
                close_pane();
            } else if (symbol == XK_Tab || symbol == XK_ISO_Left_Tab) {
                const unsigned count = static_cast<unsigned>(settings_commands.size());
                const unsigned direction =
                    (event.state & ShiftMask) || symbol == XK_ISO_Left_Tab ? count - 1 : 1;
                do {
                    settings_focus_ = (settings_focus_ + direction) % count;
                } while (settings_commands[settings_focus_] == ApplySettings &&
                         !key::validate_mapping(draft_).empty());
                dirty_ = true;
            } else if (symbol == XK_Return || symbol == XK_KP_Enter || symbol == XK_space) {
                command(settings_commands[settings_focus_]);
            }
            return;
        }
        if (pane_ != Pane::Closed) {
            if (pane_ == Pane::SaveError && (symbol == XK_Return || symbol == XK_KP_Enter)) {
                command(Quit);
                return;
            }
            if (symbol == XK_Escape) {
                close_pane();
                return;
            }
            if (pane_ == Pane::Files || pane_ == Pane::Host || pane_ == Pane::Join) {
                if (control && (symbol == XK_a || symbol == XK_A)) {
                    active_field().clear();
                    dirty_ = true;
                    return;
                }
                if (control && (symbol == XK_v || symbol == XK_V)) {
                    XConvertSelection(display_, clipboard_atom_, utf8_, paste_atom_, window_,
                                      CurrentTime);
                    return;
                }
                if (symbol == XK_Tab) {
                    field_ = pane_ == Pane::Files  ? 0
                             : pane_ == Pane::Host ? (field_ == 1 ? 2 : 1)
                                                   : (field_ + 1) % 3;
                    dirty_ = true;
                    return;
                }
                if (symbol == XK_Return || symbol == XK_KP_Enter) {
                    command(pane_ == Pane::Files ? PickFile : Connect);
                    return;
                }
                if (symbol == XK_BackSpace) {
                    auto &value = active_field();
                    if (!value.empty()) {
                        do {
                            const auto c = static_cast<unsigned char>(value.back());
                            value.pop_back();
                            if ((c & 0xc0) != 0x80)
                                break;
                        } while (!value.empty());
                    }
                    dirty_ = true;
                    return;
                }
                if (pane_ == Pane::Files && (symbol == XK_Page_Up || symbol == XK_Page_Down)) {
                    scroll_files(symbol == XK_Page_Up ? -8 : 8);
                    return;
                }
                if (!control && !(event.state & (Mod1Mask | Mod4Mask))) {
                    std::array<char, 128> buffer{};
                    KeySym ignored{};
                    const int length = XLookupString(
                        &event, buffer.data(), static_cast<int>(buffer.size()), &ignored, nullptr);
                    if (length > 0)
                        input_text(std::string(buffer.data(), static_cast<std::size_t>(length)));
                }
            }
            return;
        }
        if (control) {
            clear_input();
            if (symbol == XK_q)
                command(Quit);
            else if (symbol == XK_comma)
                command(Settings);
            else if (symbol == XK_o)
                command(Open);
            else if (symbol == XK_l)
                command(Library);
            else if (symbol == XK_c && (event.state & ShiftMask))
                command(Controls);
            else if (symbol == XK_s)
                command((event.state & ShiftMask) ? Screenshot : Step);
            else if (symbol == XK_f)
                command(Frame);
            else if (symbol == XK_m && (event.state & ShiftMask))
                command(Sound);
            else if (symbol == XK_p)
                command(Palette);
            return;
        }
        if (event.state & (Mod1Mask | Mod4Mask)) {
            clear_input();
            return;
        }
        if (symbol == XK_Escape) {
            if (menu_ >= 0) {
                menu_ = -1;
                dirty_ = true;
            } else if (library_ && runtime_->loaded()) {
                library_ = false;
                runtime_->paused = library_prior_paused_;
                dirty_ = true;
            } else
                command(Pause);
            return;
        }
        if (symbol == XK_F12) {
            command(Screenshot);
            return;
        }
        if (pending())
            return;
        if (library_) {
            const auto count = static_cast<unsigned>(arcade_games().size());
            if (!count)
                return;
            if (symbol == XK_Return || symbol == XK_KP_Enter) {
                command(100 + static_cast<int>(selected_));
                return;
            }
            const unsigned vertical_step = count > 2 ? 2U : 1U;
            if (symbol == XK_Right || symbol == XK_Down)
                selected_ = (selected_ + (symbol == XK_Down ? vertical_step : 1U)) % count;
            if (symbol == XK_Left || symbol == XK_Up)
                selected_ = (selected_ + count - (symbol == XK_Up ? vertical_step : 1U)) % count;
            dirty_ = true;
            return;
        }
        if (symbol == XK_Tab) {
            command(Inspector);
            return;
        }
        if (event.keycode < pressed_.size()) {
            pressed_[event.keycode] = true;
            sync_input();
        }
    }
    void scroll_files(int amount) {
        const auto maximum = files_.size() > 9 ? static_cast<int>(files_.size() - 9) : 0;
        file_scroll_ =
            static_cast<unsigned>(std::clamp(static_cast<int>(file_scroll_) + amount, 0, maximum));
        dirty_ = true;
    }
    void selection_request(const XSelectionRequestEvent &request) {
        XEvent response{};
        response.xselection.type = SelectionNotify;
        response.xselection.display = request.display;
        response.xselection.requestor = request.requestor;
        response.xselection.selection = request.selection;
        response.xselection.target = request.target;
        response.xselection.time = request.time;
        response.xselection.property = None;
        const auto property = request.property == None ? request.target : request.property;
        if (request.target == targets_) {
            const std::array<Atom, 3> types{targets_, utf8_, XA_STRING};
            XChangeProperty(display_, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                            reinterpret_cast<const unsigned char *>(types.data()),
                            static_cast<int>(types.size()));
            response.xselection.property = property;
        } else if (request.target == utf8_ || request.target == XA_STRING) {
            XChangeProperty(display_, request.requestor, property, request.target, 8,
                            PropModeReplace,
                            reinterpret_cast<const unsigned char *>(clipboard_.data()),
                            static_cast<int>(clipboard_.size()));
            response.xselection.property = property;
        }
        XSendEvent(display_, request.requestor, False, 0, &response);
        XFlush(display_);
    }
    void event(XEvent &event) {
        switch (event.type) {
        case Expose:
            dirty_ = true;
            break;
        case ConfigureNotify:
            width_ = std::max(1, event.xconfigure.width);
            height_ = std::max(1, event.xconfigure.height);
            update_font();
            dirty_ = true;
            break;
        case ClientMessage:
            if (static_cast<Atom>(event.xclient.data.l[0]) == delete_window_)
                command(Quit);
            break;
        case FocusOut:
            keys_down_.fill(false);
            clear_input();
            audio_.clear();
            dirty_ = true;
            break;
        case KeyPress:
            key_event(event.xkey, true);
            break;
        case KeyRelease: { // Old X servers may represent auto-repeat as release/press.
            if (XPending(display_)) {
                XEvent next{};
                XPeekEvent(display_, &next);
                if (next.type == KeyPress && next.xkey.time == event.xkey.time &&
                    next.xkey.keycode == event.xkey.keycode)
                    break;
            }
            key_event(event.xkey, false);
            break;
        }
        case ButtonPress: {
            if (pane_ == Pane::Files && (event.xbutton.button == 4 || event.xbutton.button == 5)) {
                scroll_files(event.xbutton.button == 4 ? -3 : 3);
                break;
            }
            if (event.xbutton.button != 1)
                break;
            const int x = event.xbutton.x * static_cast<int>(canvas_width) / width_,
                      y = event.xbutton.y * static_cast<int>(canvas_height) / height_;
            bool handled = false;
            for (auto it = hits_.rbegin(); it != hits_.rend(); ++it)
                if (x >= it->x && x < it->x + it->w && y >= it->y && y < it->y + it->h) {
                    const auto hit = *it;
                    if (hit.enabled)
                        command(hit.command);
                    handled = true;
                    break;
                }
            if (!handled && menu_ >= 0) {
                menu_ = -1;
                dirty_ = true;
            }
            break;
        }
        case SelectionRequest:
            selection_request(event.xselectionrequest);
            break;
        case SelectionNotify:
            if (event.xselection.property == paste_atom_ &&
                (pane_ == Pane::Files || pane_ == Pane::Host || pane_ == Pane::Join)) {
                Atom type{};
                int format{};
                unsigned long count{}, after{};
                unsigned char *data{};
                if (XGetWindowProperty(display_, window_, paste_atom_, 0, 4096, True,
                                       AnyPropertyType, &type, &format, &count, &after,
                                       &data) == Success &&
                    data) {
                    if (format == 8 && after == 0)
                        input_text(std::string(reinterpret_cast<char *>(data),
                                               std::min<unsigned long>(count, 4096)));
                    XFree(data);
                }
                break;
            }
        default:
            break;
        }
    }
    void tick() {
        if (credits_opening_.valid() &&
            credits_opening_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                credits_opening_.get();
                credits_status_ = "opened";
            } catch (const std::exception &error) {
                credits_status_ = "failed";
                show_error(error.what());
            }
            dirty_ = true;
        }
        if (pending() && opening_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                runtime_ = opening_.get();
                clear_input();
                library_ = false;
                audio_.clear();
                next_frame_ = Clock::now();
                dirty_ = true;
            } catch (const std::exception &e) {
                runtime_->paused = true;
                show_error(e.what());
            }
        }
        if (runtime_->link)
            runtime_->link->service();
        const auto now = Clock::now();
        if (now >= next_frame_) {
            next_frame_ += frame_period;
            if (next_frame_ < now - frame_period * 2)
                next_frame_ = now + frame_period;
            if (game() && !pending() &&
                (runtime_->link || (!runtime_->paused && pane_ == Pane::Closed)))
                runtime_->frame();
            std::array<std::int16_t, 4096> samples{};
            const auto count = runtime_->drain(samples);
            if (!muted_ && !runtime_->paused && pane_ == Pane::Closed)
                audio_.push(std::span<const std::int16_t>(samples).first(count));
        }
        if (now >= next_paint_ &&
            (dirty_ || runtime_->frames() != displayed_frames_ || pane_ == Pane::Details)) {
            paint();
            next_paint_ = now + frame_period;
        }
    }
    void capture_window(const std::filesystem::path &path) {
        paint();
        XSync(display_, False);
        XImage *image = XGetImage(display_, window_, 0, 0, static_cast<unsigned>(width_),
                                  static_cast<unsigned>(height_), AllPlanes, ZPixmap);
        if (!image)
            throw std::runtime_error("Cannot capture the X11 window.");
        std::vector<RGB> pixels(static_cast<std::size_t>(width_) * height_);
        auto component = [](unsigned long p, unsigned long mask) {
            if (!mask)
                return 0U;
            unsigned shift = 0;
            while (!(mask & 1)) {
                mask >>= 1;
                ++shift;
            }
            return static_cast<unsigned>(((p >> shift) & mask) * 255 / mask);
        };
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x) {
                const auto p = XGetPixel(image, x, y);
                pixels[static_cast<std::size_t>(y) * width_ + x] =
                    (component(p, image->red_mask) << 16) | (component(p, image->green_mask) << 8) |
                    component(p, image->blue_mask);
            }
        XDestroyImage(image);
        png(path, static_cast<unsigned>(width_), static_cast<unsigned>(height_), pixels);
        std::ofstream report(path.string() + ".json");
        const bool settings = pane_ == Pane::Keyboard;
        report << "{\"system\":"
               << json(runtime_->gba   ? "GBA"
                       : runtime_->bus ? "GB"
                                       : "Library")
               << ",\"frames\":" << runtime_->frames() << ",\"buttons\":" << runtime_->buttons
               << ",\"paused\":" << (runtime_->paused ? "true" : "false")
               << ",\"controls_visible\":" << (controls_ ? "true" : "false")
               << ",\"inspector_view\":" << (runtime_->inspector ? "true" : "false")
               << ",\"library_view\":" << (library_ ? "true" : "false")
               << ",\"settings_open\":" << (settings ? "true" : "false")
               << ",\"credits_status\":" << json(credits_status_)
               << ",\"audio_available\":" << (audio_.available() ? "true" : "false")
               << ",\"audio_status\":" << json(audio_.status())
               << ",\"settings_focus\":" << settings_focus_ << ",\"settings_capture\":" << capture_
               << ",\"settings_error\":"
               << json(settings ? (error_.empty() ? key::validate_mapping(draft_) : error_) : "")
               << ",\"ui_mode\":"
               << json(settings                   ? "keyboard-settings"
                       : pane_ == Pane::Files     ? "file-picker"
                       : pane_ == Pane::Details   ? "connection-details"
                       : pane_ == Pane::Host      ? "host-dialog"
                       : pane_ == Pane::Join      ? "join-dialog"
                       : pane_ == Pane::Error     ? "message"
                       : pane_ == Pane::SaveError ? "save-error"
                       : library_                 ? "library"
                       : runtime_->inspector      ? "inspector"
                                                  : "player")
               << ",\"keyboard_mapping\":[";
        for (unsigned i = 0; i < mapping_.size(); ++i) {
            if (i)
                report << ',';
            report << mapping_[i];
        }
        report << "],\"settings_draft\":[";
        for (unsigned i = 0; i < draft_.size(); ++i) {
            if (i)
                report << ',';
            report << draft_[i];
        }
        report << "],\"width\":" << width_ << ",\"height\":" << height_;
        const auto games = arcade_games();
        report << ",\"library_selected\":"
               << json(selected_ < games.size() ? games[selected_].id : "")
               << ",\"library_games\":[";
        for (unsigned i = 0; i < games.size(); ++i) {
            if (i)
                report << ',';
            const auto &entry = games[i];
            report << "{\"id\":" << json(entry.id) << ",\"title\":" << json(entry.title)
                   << ",\"system\":" << json(entry.system) << ",\"author\":" << json(entry.author)
                   << ",\"license\":" << json(entry.license) << ",\"players\":" << json(entry.players)
                   << '}';
        }
        report << ']';
        if (runtime_->link)
            report << ",\"netplay\":{\"frames\":" << runtime_->link->frames()
                   << ",\"verified_frames\":" << runtime_->link->verified_frames()
                   << ",\"connected\":" << (runtime_->link->connected() ? "true" : "false")
                   << ",\"finished\":" << (runtime_->link->finished() ? "true" : "false")
                   << ",\"host\":" << (is_host_ ? "true" : "false")
                   << ",\"status\":" << json(runtime_->link->status()) << "}";
        report << "}\n";
        if (!report)
            throw std::runtime_error("Cannot write screenshot metadata.");
    }

  public:
    Player(std::unique_ptr<Runtime> runtime, const Options &o)
        : library_(o.library || !runtime->loaded()), controls_(o.controls),
          runtime_(std::move(runtime)) {
        capture_path_ = o.capture;
        noninteractive_ = o.window_test;
        library_prior_paused_ = runtime_->paused;
        if (library_)
            runtime_->paused = true;
        try {
            mapping_ = key::load_linux_mapping(key::linux_preferences_path());
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
        }
        display_ = XOpenDisplay(nullptr);
        if (!display_)
            throw std::runtime_error("No X11 display. Start a desktop session (XWayland is "
                                     "supported), or use --headless.");
        const auto screen = DefaultScreen(display_);
        window_ = XCreateSimpleWindow(display_, RootWindow(display_, screen), 20, 20, width_,
                                      height_, 0, 0, color(background));
        XStoreName(display_, window_, "Matchaboy - Game Boy and Game Boy Advance");
        XClassHint identity{};
        identity.res_name = const_cast<char *>("matchaboy");
        identity.res_class = const_cast<char *>("Matchaboy");
        XSetClassHint(display_, window_, &identity);
        XSizeHints hints{};
        hints.flags = PMinSize;
        hints.min_width = 1000;
        hints.min_height = 720;
        XSetWMNormalHints(display_, window_, &hints);
        delete_window_ = XInternAtom(display_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(display_, window_, &delete_window_, 1);
        clipboard_atom_ = XInternAtom(display_, "CLIPBOARD", False);
        utf8_ = XInternAtom(display_, "UTF8_STRING", False);
        targets_ = XInternAtom(display_, "TARGETS", False);
        paste_atom_ = XInternAtom(display_, "MATCHABOY_PASTE", False);
        XSelectInput(display_, window_,
                     ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask |
                         StructureNotifyMask | FocusChangeMask | PropertyChangeMask);
        Bool supported = False;
        XkbSetDetectableAutoRepeat(display_, True, &supported);
        gc_ = XCreateGC(display_, window_, 0, nullptr);
        text_draw_ = XftDrawCreate(display_, window_, DefaultVisual(display_, screen),
                                   DefaultColormap(display_, screen));
        if (!text_draw_)
            throw std::runtime_error("Cannot create the Linux text surface.");
        update_font();
        XMapWindow(display_, window_);
        XFlush(display_);
        runtime_->enable_audio();
        room_code_ = o.code;
        is_host_ = o.host;
    }
    ~Player() {
        if (runtime_->link)
            runtime_->link->close();
        if (display_) {
            if (font_)
                XftFontClose(display_, font_);
            if (text_draw_)
                XftDrawDestroy(text_draw_);
            for (auto &[value, color] : text_colors_) {
                (void)value;
                XftColorFree(display_, DefaultVisual(display_, DefaultScreen(display_)),
                             DefaultColormap(display_, DefaultScreen(display_)), &color);
            }
            if (gc_)
                XFreeGC(display_, gc_);
            if (window_)
                XDestroyWindow(display_, window_);
            XCloseDisplay(display_);
        }
    }
    int run(const Options &o) {
        const auto opened = Clock::now();
        bool captured = false;
        while (running_) {
            unsigned dispatched = 0;
            while (running_ && XPending(display_) && dispatched++ < 128) {
                XEvent input{};
                XNextEvent(display_, &input);
                try {
                    event(input);
                } catch (const std::exception &e) {
                    show_error(e.what());
                }
            }
            if (!running_)
                break;
            try {
                tick();
            } catch (const std::exception &e) {
                runtime_->paused = true;
                show_error(e.what());
            }
            if (o.window_test && !captured &&
                Clock::now() - opened > std::chrono::milliseconds(300)) {
                capture_window(o.capture);
                captured = true;
                running_ = false;
            }
            pollfd descriptor{ConnectionNumber(display_), POLLIN, 0};
            poll(&descriptor, 1, 4);
        }
        if (!shutdown_saved_ && !quit_without_save_) {
            runtime_->set_buttons(0);
            if (runtime_->link)
                runtime_->link->close();
            runtime_->flush();
        }
        return 0;
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        auto o = options(argc, argv);
        if (!o.game.empty()) {
            const auto games = arcade_games();
            const auto entry = std::find_if(games.begin(), games.end(),
                                            [&o](const auto &game) { return o.game == game.id; });
            if (entry == games.end())
                throw std::runtime_error("Unknown bundled game: " + o.game);
            o.rom = arcade_rom_path(static_cast<std::size_t>(entry - games.begin()));
        }
        std::unique_ptr<Runtime> runtime =
            o.rom.empty() ? std::make_unique<Runtime>() : std::make_unique<Runtime>(o.rom);
        const bool networking = o.host || !o.join.empty();
        if (networking) {
            if (!runtime->loaded())
                throw std::runtime_error("Choose a ROM for friend play.");
            runtime = start_link(o.rom, network_options(o));
            runtime->set_buttons(static_cast<std::uint16_t>(o.buttons));
        }
        runtime->inspector = o.inspector;
        runtime->green_palette = o.green;
        if (o.headless || o.window_test) {
            if (!runtime->loaded() && !o.library)
                throw std::runtime_error("Capture requires a ROM or --game.");
            if (networking) {
                const auto deadline = Clock::now() + std::chrono::seconds(90);
                while (runtime->frames() < o.frames && Clock::now() < deadline &&
                       !runtime->link->finished()) {
                    runtime->frame();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if (runtime->frames() < o.frames)
                    throw std::runtime_error("Friend capture did not finish: " +
                                             runtime->link->status());
                std::cout << "Friend capture ready: frame " << runtime->frames() << std::endl;
            } else {
                if (!o.input_frames)
                    runtime->set_buttons(static_cast<std::uint16_t>(o.buttons));
                advance_frames(*runtime, o.frames);
                if (o.input_frames) {
                    runtime->set_buttons(static_cast<std::uint16_t>(o.buttons));
                    advance_frames(*runtime, o.input_frames);
                }
            }
        }
        runtime->paused = !networking && (o.paused || o.headless || o.window_test);
        if (o.headless) {
            if (!o.stop_file.empty()) {
                const auto deadline = Clock::now() + std::chrono::seconds(10);
                while (!std::filesystem::exists(o.stop_file) && Clock::now() < deadline) {
                    runtime->link->service();
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                if (!std::filesystem::exists(o.stop_file))
                    throw std::runtime_error(
                        "Timed out waiting for the netplay capture coordinator.");
            }
            runtime->capture(o.capture);
            if (runtime->link)
                runtime->link->close();
            runtime->flush();
            return 0;
        }
        return Player(std::move(runtime), o).run(o);
    } catch (const std::exception &e) {
        std::cerr << "Matchaboy: " << e.what() << '\n';
        return 1;
    }
}

// Native Windows host for the same telemetry and dashboard as autopsy_main.mm.
// Only Windows SDK libraries are used; emulator/inspection data stays in the core.
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <commdlg.h>
#include <GL/gl.h>
#include <dxgi1_6.h>
#include <avrt.h>
#include <iomanip>
#include "dmg/autopsy.hpp"
#include "gba_core.hpp"
#include "windows_audio.hpp"
#include "arcade_library.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

// Driver-supported hybrid graphics hints. Windows per-app preferences take precedence.
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}
namespace {
constexpr unsigned canvas_width = 1280, canvas_height = 920;
struct Color { double r, g, b; };
constexpr Color foreground{0.85, 0.90, 0.94}, muted{0.48, 0.60, 0.67};
constexpr Color cyan{0.25, 0.85, 0.94}, green{0.45, 0.90, 0.60}, orange{1.0, 0.65, 0.32};
constexpr std::array<Color, 4> shades{{{0.80,0.87,0.61},{0.53,0.65,0.40},{0.29,0.43,0.31},{0.10,0.23,0.22}}};
constexpr std::array<Color, 4> grayscale{{{1,1,1},{2.0/3,2.0/3,2.0/3},{1.0/3,1.0/3,1.0/3},{0,0,0}}};
constexpr auto frame_period = std::chrono::nanoseconds(16742706); // 70224 / 4194304 seconds
std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!count) throw std::runtime_error("invalid UTF-8 text");
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), count);
    return out;
}
std::string utf8(const std::wstring &s) {
    if (s.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("invalid Windows path");
    std::string out(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), count, nullptr, nullptr);
    return out;
}
class GraphicsAdapter {
public:
    std::vector<std::wstring> detected;
    std::wstring low = L"Integrated GPU", high = L"Dedicated GPU";
    std::string renderer = "Unavailable";
    std::wstring executable;
    unsigned preference = 0;
    bool restart_required = false;
    static constexpr auto registry_path = L"Software\\Microsoft\\DirectX\\UserGpuPreferences";
    GraphicsAdapter() {
        std::array<wchar_t, 32768> path{};
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size == path.size()) throw std::runtime_error("Cannot locate app for graphics preferences");
        executable.assign(path.data(), size);
        const auto setting = read_setting();
        if (setting.find(L"GpuPreference=1;") != std::wstring::npos) preference = 1;
        if (setting.find(L"GpuPreference=2;") != std::wstring::npos) preference = 2;
        IDXGIFactory1 *factory{};
        if (FAILED(CreateDXGIFactory1(IID_IDXGIFactory1, reinterpret_cast<void **>(&factory)))) return;
        for (UINT i = 0; ; ++i) {
            IDXGIAdapter1 *adapter{};
            if (factory->EnumAdapters1(i, &adapter) != S_OK) break;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                detected.emplace_back(desc.Description);
                if (desc.VendorId == 0x8086) low = desc.Description;
                if (desc.VendorId == 0x10DE || desc.VendorId == 0x1002) high = desc.Description;
            }
            adapter->Release();
        }
        IDXGIFactory6 *ranked{};
        if (SUCCEEDED(factory->QueryInterface(IID_IDXGIFactory6, reinterpret_cast<void **>(&ranked)))) {
            for (auto rank : {DXGI_GPU_PREFERENCE_MINIMUM_POWER, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE}) {
                IDXGIAdapter1 *adapter{};
                if (SUCCEEDED(ranked->EnumAdapterByGpuPreference(0, rank, IID_IDXGIAdapter1, reinterpret_cast<void **>(&adapter)))) {
                    DXGI_ADAPTER_DESC1 desc{};
                    if (SUCCEEDED(adapter->GetDesc1(&desc)) && !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                        (rank == DXGI_GPU_PREFERENCE_MINIMUM_POWER ? low : high) = desc.Description;
                    adapter->Release();
                }
            }
            ranked->Release();
        }
        factory->Release();
    }
    std::wstring read_setting() const {
        DWORD bytes{};
        if (RegGetValueW(HKEY_CURRENT_USER, registry_path, executable.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS) return {};
        std::vector<wchar_t> value(bytes / sizeof(wchar_t) + 1);
        if (RegGetValueW(HKEY_CURRENT_USER, registry_path, executable.c_str(), RRF_RT_REG_SZ, nullptr, value.data(), &bytes) != ERROR_SUCCESS) return {};
        return value.data();
    }
    void select(unsigned choice) {
        auto value = read_setting();
        std::wstring preserved;
        for (std::size_t start = 0; start < value.size();) {
            auto end = value.find(L';', start);
            if (end == std::wstring::npos) end = value.size();
            const auto part = value.substr(start, end-start);
            if (!part.empty() && part.rfind(L"GpuPreference=", 0) != 0) preserved += part + L';';
            start = end + 1;
        }
        if (choice) preserved += L"GpuPreference=" + std::to_wstring(choice) + L";";
        HKEY key{};
        if (RegCreateKeyExW(HKEY_CURRENT_USER, registry_path, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            throw std::runtime_error("Cannot save GPU choice. Use Windows Settings > Display > Graphics.");
        const auto result = preserved.empty() ? RegDeleteValueW(key, executable.c_str()) :
            RegSetValueExW(key, executable.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(preserved.c_str()), static_cast<DWORD>((preserved.size()+1)*sizeof(wchar_t)));
        RegCloseKey(key);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Windows could not save the GPU preference");
        preference = choice;
        restart_required = true;
    }
    void report(const std::filesystem::path &path) const {
        std::ofstream out(path);
        out << "{\"renderer\":" << std::quoted(renderer) << ",\"preference\":" << preference
            << ",\"restart_required\":" << (restart_required ? "true" : "false") << ",\"detected\":[";
        for (std::size_t i = 0; i < detected.size(); ++i) {
            if (i) out << ',';
            out << std::quoted(utf8(detected[i]));
        }
        out << "]}\n";
    }
};
Gdiplus::Color ink(Color c) {
    return {255, static_cast<BYTE>(c.r * 255), static_cast<BYTE>(c.g * 255), static_cast<BYTE>(c.b * 255)};
}
void fill(Gdiplus::Graphics *context, double x, double y, double w, double h, Color color) {
    Gdiplus::SolidBrush brush(ink(color));
    context->FillRectangle(&brush, static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h));
}
std::map<double, std::unique_ptr<Gdiplus::Font>> fonts;
void text(Gdiplus::Graphics *context, double x, double y, const std::string &value,
          double size = 13, Color color = foreground) {
    const auto string = wide(value);
    auto &font = fonts[size];
    if (!font) font = std::make_unique<Gdiplus::Font>(L"Consolas", static_cast<float>(size), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::SolidBrush brush(ink(color));
    context->DrawString(string.c_str(), static_cast<INT>(string.size()), font.get(),
        Gdiplus::PointF(static_cast<float>(x), static_cast<float>(y)), Gdiplus::StringFormat::GenericTypographic(), &brush);
}
template<typename... Args> std::string format(const char *pattern, Args... args) {
    std::array<char, 2048> buffer{};
    std::snprintf(buffer.data(), buffer.size(), pattern, args...);
    return buffer.data();
}
void wrapped_text(Gdiplus::Graphics *context, double x, double y, const char *value,
                  unsigned columns, unsigned lines, double size = 15, Color color = foreground) {
    std::istringstream words(value ? value : "");
    std::string word, row;
    unsigned line = 0;
    while (words >> word) {
        if (!row.empty() && row.size() + word.size() + 1 > columns) {
            if (line + 1 == lines) { text(context, x, y + line*(size+4), row + "...", size, color); return; }
            text(context, x, y + line++*(size+4), row, size, color);
            row.clear();
        }
        if (!row.empty()) row += ' ';
        row += word;
    }
    if (!row.empty() && line < lines) text(context, x, y + line*(size+4), row, size, color);
}
struct Imaging {
    ULONG_PTR token{};
    Imaging() {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("cannot initialize Windows imaging");
    }
    ~Imaging() { fonts.clear(); Gdiplus::GdiplusShutdown(token); }
};
void save_png(Gdiplus::Bitmap &bitmap, const std::filesystem::path &path) {
    UINT count{}, bytes{};
    Gdiplus::GetImageEncodersSize(&count, &bytes);
    std::vector<BYTE> storage(bytes);
    auto *encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok)
        throw std::runtime_error("PNG encoder unavailable");
    for (UINT i = 0; i < count; ++i) {
        if (std::wstring(encoders[i].MimeType) != L"image/png") continue;
        if (bitmap.Save(path.c_str(), &encoders[i].Clsid, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("cannot write capture: " + utf8(path.wstring()));
        return;
    }
    throw std::runtime_error("PNG encoder unavailable");
}
void draw_logo(Gdiplus::Graphics *context, double x, double y, double size) {
    fill(context, x, y, size, size, {0.15,0.32,0.31});
    fill(context, x+size*.20, y+size*.14, size*.60, size*.70, {0.78,0.90,0.70});
    fill(context, x+size*.29, y+size*.23, size*.42, size*.28, {0.035,0.09,0.10});
    fill(context, x+size*.29, y+size*.64, size*.20, size*.06, {0.035,0.09,0.10});
    fill(context, x+size*.36, y+size*.57, size*.06, size*.20, {0.035,0.09,0.10});
    fill(context, x+size*.61, y+size*.62, size*.08, size*.08, {0.035,0.09,0.10});
}

class Runtime {
  public:
    std::unique_ptr<dmg::Autopsy> inspector = std::make_unique<dmg::Autopsy>();
    std::unique_ptr<dmg::Bus> bus;
    std::unique_ptr<dmg::Cpu> cpu;
    std::unique_ptr<GbaCore> gba;
    std::string title;
    bool library_view = true;
    unsigned library_selection = 0;
    bool library_was_paused = false;
    int library_game = -1;
    bool has_game() const { return bus || gba; }
    bool inspector_view = false;
    unsigned inspector_tab = 0;
    unsigned memory_region = 0, memory_offset = 0;
    static constexpr std::array<unsigned, 6> memory_bases{0x02000000,0x03000000,0x06000000,0x05000000,0x07000000,0x08000000};
    static constexpr std::array<unsigned, 6> memory_sizes{0x40000,0x8000,0x18000,0x400,0x400,0x2000000};
    void memory_page(int direction) {
        memory_offset = static_cast<unsigned>(std::clamp(static_cast<int>(memory_offset)+direction*256,0,static_cast<int>(memory_sizes[memory_region]-256)));
    }
    bool green_palette = false;
    bool controls_visible = true;
    unsigned lcd_left() const { return controls_visible ? 48 : 240; }
    bool sound_muted = false;
    bool paused = false;
    void enable_audio() { if (gba) gba->enable_audio(); else if (bus) bus->apu.set_sample_rate(48000); }
    std::size_t drain_audio(std::span<std::int16_t> samples) {
        return gba ? gba->drain_audio(samples) : bus ? bus->apu.drain_samples(samples) : 0;
    }
    unsigned trace_scroll = 0;
    std::uint16_t buttons = 0;
    unsigned lcd_width() const { return gba ? 240 : 160; }
    unsigned lcd_height() const { return gba ? 160 : 144; }
    unsigned display_height() const { return gba ? 533 : 720; }
    unsigned lcd_top() const { return 136+(720-display_height())/2; }
    Gdiplus::Color lcd_color(unsigned i) const {
        if (gba) { const auto p = gba->pixels()[i]; return {255, static_cast<BYTE>(p), static_cast<BYTE>(p>>8), static_cast<BYTE>(p>>16)}; }
        return ink((green_palette ? shades : grayscale)[bus->ppu.framebuffer[i]&3]);
    }
    void set_buttons(std::uint16_t value) {
        buttons = value;
        if (!gba) { if (bus) bus->set_buttons(static_cast<std::uint8_t>(value)); return; }
        constexpr std::array<unsigned, 10> mapping{4,5,6,7,0,1,2,3,9,8};
        std::uint16_t mapped = 0;
        for (unsigned i=0; i<mapping.size(); ++i) if (value & (1U<<i)) mapped |= static_cast<std::uint16_t>(1U<<mapping[i]);
        gba->set_buttons(mapped);
    }
    void flush_save() { if (gba) gba->flush_save(); }
    void toggle_inspector() { if (has_game() && !library_view) inspector_view = !inspector_view; }

    Runtime() = default;
    explicit Runtime(const std::filesystem::path &path) : title(utf8(path.filename().wstring())), library_view(false) {
        auto extension = path.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (extension == L".gba") { gba = std::make_unique<GbaCore>(path); return; }
        if (extension != L".gb") throw std::runtime_error("Choose a Game Boy (.gb) or Game Boy Advance (.gba) ROM.");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open ROM: " + utf8(path.wstring()));
        std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(stream)), {});
        if (rom.size() < 0x150) throw std::runtime_error("This file is too small to be a Game Boy ROM.");
        if (rom[0x143] == 0xC0) throw std::runtime_error("This game requires Game Boy Color hardware, which is not supported yet.");
        bus = std::make_unique<dmg::Bus>(std::move(rom));
        cpu = std::make_unique<dmg::Cpu>(*bus);
        bus->autopsy = inspector.get();
        bus->apu.set_sample_rate(0); // Headless warm-up does not queue old audio.
    }
    void step() { if (!has_game() || library_view) return; if (gba) { gba->step(); return; } bus->autopsy = inspector.get(); cpu->step(); }
    void run_frame() {
        if (!has_game() || library_view) return;
        if (gba) { gba->run_frame(); return; }
        // Disassembly/heatmap/scopes are inspection work, not part of playing.
        // Keep the player fast enough to feed the audio device continuously.
        bus->autopsy = inspector_view ? inspector.get() : nullptr;
        const auto target = bus->ppu.frames() + 1;
        const auto deadline = bus->cycles() + 70224 * 2;
        while (bus->ppu.frames() < target && bus->cycles() < deadline) {
            const auto before = bus->cycles();
            cpu->step();
            if (bus->cycles() == before) break; // STOP must leave the UI responsive for joypad wake.
        }
    }
    void position(unsigned line, unsigned dot) {
        if (!has_game()) throw std::runtime_error("Choose a game before positioning its PPU.");
        if (gba) throw std::runtime_error("PPU positioning is available for Game Boy only.");
        const auto deadline = bus->cycles() + 70224 * 2;
        do {
            const auto before = bus->cycles();
            step();
            if (bus->ppu.read(0xFF44) == line && bus->ppu.dot() >= dot && bus->ppu.dot() < dot + 24) return;
            if (bus->cycles() == before) break;
        } while (bus->cycles() < deadline);
        throw std::runtime_error("requested PPU position not reached (LCD disabled or CPU stopped)");
    }
    std::unique_ptr<Gdiplus::Bitmap> render_library() {
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(bitmap.get()); auto *context = &graphics;
        context->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        fill(context,0,0,1280,920,{0.035,0.06,0.08});
        fill(context,0,0,1280,88,{0.065,0.105,0.13});
        draw_logo(context,24,22,40); text(context,78,25,"MATCHABOY",26,cyan);
        text(context,282,34,"ORIGINAL ARCADE",13,muted);
        if (has_game()) {
            fill(context,834,20,190,48,{0.10,0.17,0.20});
            text(context,851,35,"Return to game",16,foreground);
        }
        fill(context,1040,20,208,48,{0.16,0.34,0.32});
        text(context,1061,35,"Open your game...",16,foreground);
        text(context,32,114,"Small games. Big discoveries.",27,foreground);
        text(context,32,153,"Pick a game, then see the machine behind it.",16,muted);
        text(context,32,196,"01 / GAME BOY",14,green);
        text(context,426,196,"02 / GAME BOY ADVANCE",14,cyan);
        const auto games = arcade_games();
        constexpr std::array<Color,10> accents{{{0.53,0.89,0.65},{0.83,0.73,0.99},{0.65,0.89,0.45},{0.97,0.73,0.39},{0.51,0.81,0.99},
            {0.99,0.67,0.42},{0.52,0.81,0.99},{0.90,0.64,0.97},{0.87,0.84,0.48},{0.57,0.90,0.71}}};
        for (unsigned i = 0; i < games.size() && i < 10; ++i) {
            const auto &game = games[i]; const bool selected = i == library_selection;
            const double x = i < 5 ? 32 : 426, y = 230 + (i%5)*122;
            const auto accent = accents[i];
            fill(context,x,y,370,110,selected ? Color{0.13,0.24,0.25} : Color{0.065,0.105,0.13});
            fill(context,x,y,selected ? 4 : 2,110,selected ? accent : Color{0.13,0.23,0.25});
            // Small geometric cartridge marks are UI decoration, never gameplay previews.
            fill(context,x+18,y+22,48,62,{0.035,0.075,0.09});
            fill(context,x+25,y+30,34,27,accent);
            fill(context,x+32+(i%3)*4,y+36,8,9,{0.035,0.075,0.09});
            fill(context,x+25,y+64,20,3,accent);
            fill(context,x+25,y+72,13,3,accent);
            text(context,x+84,y+23,game.title,19,selected ? foreground : Color{0.72,0.81,0.85});
            text(context,x+84,y+55,game.genre,13,muted);
            text(context,x+84,y+80,selected ? "SELECTED  >" : "SELECT TO EXPLORE",11,selected ? accent : muted);
        }
        fill(context,820,148,428,700,{0.065,0.105,0.13});
        if (!games.empty()) {
            const auto &game = games[std::min<std::size_t>(library_selection,games.size()-1)];
            text(context,844,169,std::string(game.system)+"  /  "+game.genre,13,green);
            text(context,844,202,game.title,25,foreground);
            wrapped_text(context,844,246,game.description,42,4,15,muted);
            fill(context,844,334,380,46,{0.28,0.63,0.49});
            text(context,866,347,"PLAY GAME",18,{0.02,0.08,0.07});
            text(context,1132,350,"ENTER",13,{0.02,0.08,0.07});
            text(context,844,407,"CONTROLS",13,cyan);
            wrapped_text(context,844,433,game.controls,44,6,14,foreground);
            text(context,844,554,"LEARN",13,cyan);
            wrapped_text(context,844,580,game.learn,48,6,13,muted);
            text(context,844,704,"INSPECT / PRESS TAB WHILE PLAYING",13,cyan);
            wrapped_text(context,844,730,game.inspector_hint,48,6,13,muted);
        }
        text(context,32,870,"ARROWS select   ENTER play   CTRL+O open a file   CTRL+L library   F12 screenshot",13,muted);
        text(context,32,898,has_game() ? "Your current game is paused while you browse." : "Five GB games + five GBA games. Included and ready to play.",12,muted);
        return bitmap;
    }
    std::unique_ptr<Gdiplus::Bitmap> render_player(bool include_lcd = true) {
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(bitmap.get());
        auto *context = &graphics;
        context->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        fill(context, 0, 0, canvas_width, canvas_height, {0.035, 0.06, 0.08});
        fill(context, 0, 0, canvas_width, 88, {0.065, 0.105, 0.13});
        draw_logo(context, 24, 22, 40);
        text(context, 78, 25, "MATCHABOY", 26, cyan);
        text(context, 260, 34, gba ? "GAME BOY ADVANCE" : "GAME BOY PLAYER", 13, muted);
        fill(context, 578, 20, 116, 48, {0.10, 0.17, 0.20});
        text(context, 598, 34, "Library", 17, foreground);
        fill(context, 710, 20, 174, 48, {0.10, 0.17, 0.20});
        text(context, 726, 32, controls_visible ? "Hide controls" : "Show controls", 17, foreground);
        fill(context, 900, 20, 158, 48, {0.16, 0.34, 0.32});
        text(context, 919, 32, "Open game...", 18, foreground);
        fill(context, 1076, 20, 164, 48, {0.10, 0.17, 0.20});
        text(context, 1098, 32, "Inspector", 18, muted);
        const auto name = title.size() > 65 ? title.substr(0, 62) + "..." : title;
        text(context, 48, 103, name, 17, foreground);
        if (include_lcd) {
            std::array<BYTE, 240*160*4> pixels{};
            for (unsigned i = 0; i < lcd_width()*lcd_height(); ++i) {
                const auto c = lcd_color(i);
                pixels[i*4] = c.GetB(); pixels[i*4+1] = c.GetG();
                pixels[i*4+2] = c.GetR(); pixels[i*4+3] = 255;
            }
            Gdiplus::Bitmap lcd(lcd_width(), lcd_height(), lcd_width()*4, PixelFormat32bppARGB, pixels.data());
            context->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
            context->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
            context->DrawImage(&lcd, Gdiplus::Rect(static_cast<INT>(lcd_left()), static_cast<INT>(lcd_top()), 800, static_cast<INT>(display_height())), 0, 0, lcd_width(), lcd_height(), Gdiplus::UnitPixel);
        }
        if (controls_visible) {
        text(context, 900, 145, "CONTROLS", 20, cyan);
        text(context, 900, 184, gba ? "Keyboard  >  GBA" : "Keyboard  >  Game Boy", 14, muted);
        constexpr std::array<const char *, 6> keys{"ARROWS", "Z", "X", "ENTER", "SHIFT", "SPACE"};
        constexpr std::array<const char *, 6> labels{"D-pad", "A button", "B button", "Start", "Select", "Pause / resume"};
        for (unsigned i = 0; i < keys.size(); ++i) {
            const auto y = 230 + i*66;
            fill(context, 900, y, 92, 40, {0.10, 0.17, 0.20});
            text(context, 912, y+10, keys[i], 15, foreground);
            text(context, 1014, y+10, labels[i], 16, foreground);
        }
        if (gba) text(context, 900, 635, "Q: L shoulder   W: R shoulder", 14, foreground);
        const auto games = arcade_games();
        if (library_game >= 0 && static_cast<std::size_t>(library_game) < games.size())
            wrapped_text(context,900,662,games[library_game].controls,37,4,13,foreground);
        else {
            text(context, 900, 664, "These keys work for every game.", 14, muted);
            text(context, 900, 691, "A/B actions depend on the game.", 14, muted);
        }
        text(context, 900, 757, "Ctrl+O  Choose another game", 14, muted);
        text(context, 900, 786, "Tab     Toggle Inspector", 14, muted);
        text(context, 900, 815, "F12     Save screenshot", 14, ::muted);
        text(context, 900, 844, "M       Toggle sound", 14, ::muted);
        }
        fill(context, 48, 872, 160, 36, {0.16, 0.34, 0.32});
        text(context, 75, 880, paused ? "Resume" : "Pause", 17, foreground);
        text(context, 232, 882, paused ? "Paused - press Space to resume" : "Playing", 15, paused ? orange : muted);
        return bitmap;
    }
    std::unique_ptr<Gdiplus::Bitmap> render_gba(bool include_lcd) {
        const auto state = gba->inspect(memory_bases[memory_region]+memory_offset);
        auto bitmap = std::make_unique<Gdiplus::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        Gdiplus::Graphics graphics(bitmap.get()); auto *context=&graphics;
        context->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        context->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        context->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        fill(context,0,0,1280,920,{0.035,0.06,0.08}); fill(context,0,0,1280,66,{0.065,0.105,0.13});
        draw_logo(context,24,12,40); text(context,78,16,"MATCHABOY",23,cyan);
        text(context,305,22,"GAME BOY ADVANCE / INSPECTOR",14);
        text(context,890,16,paused ? "PAUSED" : "RUNNING",13,green);
        text(context,890,37,format("Frame %llu",gba->frames()),12,muted);
        constexpr std::array<const char *,4> tabs{"1  Video","2  CPU","3  Memory","4  Audio"};
        for (unsigned i=0;i<4;++i) {
            fill(context,48+i*230,92,218,48,i==inspector_tab ? Color{0.15,0.32,0.31} : Color{0.07,0.11,0.13});
            text(context,68+i*230,106,tabs[i],17,i==inspector_tab ? foreground : muted);
        }
        fill(context,1016,92,216,48,{0.07,0.11,0.13}); text(context,1038,106,"Back to game",17,cyan);
        if (inspector_tab==0) {
            text(context,48,180,"VIDEO / 240 x 160",20,cyan);
            if (include_lcd) {
                std::array<BYTE,240*160*4> pixels{};
                for (unsigned i=0;i<240*160;++i) {
                    const auto c=lcd_color(i); pixels[i*4]=c.GetB(); pixels[i*4+1]=c.GetG(); pixels[i*4+2]=c.GetR(); pixels[i*4+3]=255;
                }
                Gdiplus::Bitmap lcd(240,160,960,PixelFormat32bppARGB,pixels.data());
                context->DrawImage(&lcd,Gdiplus::Rect(48,232,640,427),0,0,240,160,Gdiplus::UnitPixel);
            }
            text(context,736,180,"DISPLAY REGISTERS",20,cyan);
            text(context,736,232,format("Mode %u   Scanline %u",state.dispcnt&7,state.vcount),17);
            text(context,736,270,format("DISPCNT %04X   DISPSTAT %04X",state.dispcnt,state.dispstat),16,muted);
            text(context,736,308,format("VBlank %u   HBlank %u",state.dispstat&1,(state.dispstat>>1)&1),16,muted);
            text(context,736,380,"BACKGROUND PALETTE / 256 COLORS",16,cyan);
            for (unsigned i=0;i<256;++i) {
                const auto color=state.palette[i];
                fill(context,736+(i%16)*28,424+(i/16)*23,26,21,{(color&31)/31.0,((color>>5)&31)/31.0,((color>>10)&31)/31.0});
            }
            text(context,48,705,"Native GBA output; original aspect ratio preserved.",15,muted);
        } else if (inspector_tab==1) {
            text(context,48,180,(state.cpsr&32) ? "ARM7TDMI / THUMB" : "ARM7TDMI / ARM",20,cyan);
            for (unsigned i=0;i<16;++i) text(context,48+(i/8)*245,240+(i%8)*48,format("R%-2u  %08X",i,state.registers[i]),19);
            text(context,48,660,format("CPSR %08X    N%u Z%u C%u V%u",state.cpsr,state.cpsr>>31,(state.cpsr>>30)&1,(state.cpsr>>29)&1,(state.cpsr>>28)&1),16,muted);
            text(context,48,710,"R13 SP   R14 LR   R15 pipeline PC",15,muted);
            text(context,690,180,"RAW OPCODES NEAR R15",20,cyan);
            const unsigned width=(state.cpsr&32) ? 2 : 4;
            for (unsigned i=0;i<16;++i) text(context,690,230+i*34,format("%08X    %0*X",state.code_base+i*width,width*2,state.opcodes[i]),17);
            text(context,690,800,"Memory view, not retired instruction history.",14,muted);
        } else if (inspector_tab==2) {
            constexpr std::array<const char *,6> names{"EWRAM","IWRAM","VRAM","Palette","OAM","ROM"};
            for (unsigned i=0;i<6;++i) {
                fill(context,48+i*197,156,185,40,i==memory_region ? Color{0.15,0.32,0.31} : Color{0.07,0.11,0.13});
                text(context,65+i*197,167,names[i],16);
            }
            text(context,48,216,format("%s / %08X    PgUp/PgDn or wheel: 256-byte page",names[memory_region],state.memory_base),17,cyan);
            text(context,200,258,"00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F",17,muted);
            for (unsigned row=0;row<16;++row) {
                text(context,48,294+row*31,format("%08X",state.memory_base+row*16),17,muted);
                std::string hex,ascii;
                for (unsigned col=0;col<16;++col) { const auto b=state.memory[row*16+col];hex+=format("%02X ",b);ascii+=b>=32&&b<127 ? static_cast<char>(b) : '.'; }
                text(context,200,294+row*31,hex,17);text(context,770,294+row*31,ascii,17,green);
            }
            text(context,48,824,"Read-only inspection; viewing memory does not advance the game.",15,muted);
        } else {
            text(context,48,180,"AUDIO / FINAL STEREO PCM",20,cyan);
            text(context,48,222,"Last 512 output samples at 48 kHz; includes PSG and Direct Sound.",15,muted);
            for (unsigned side=0;side<2;++side) {
                const double y=282+side*230;
                text(context,48,y,side ? "RIGHT" : "LEFT",17,cyan);
                fill(context,200,y,1032,180,{0.07,0.11,0.13});
                Gdiplus::Pen pen(ink(side ? green : cyan),1);
                std::vector<Gdiplus::PointF> points;
                for (unsigned i=0;i<state.audio_frames;++i) points.emplace_back(static_cast<float>(202+i*1028.0/511),static_cast<float>(y+90-state.audio[i*2+side]*84.0/32768));
                if (points.size()>1) context->DrawLines(&pen,points.data(),static_cast<INT>(points.size()));
            }
            if (!state.audio_frames) text(context,220,368,"Play the game to collect output samples.",16,muted);
            text(context,48,768,format("SOUNDCNT L %04X   H %04X   X %04X   BIAS %04X",state.sound_low,state.sound_high,state.sound_enable,state.sound_bias),17);
        }
        text(context,48,888,"TAB game   1-4 panels   SPACE pause   S instruction   F frame   CTRL+O open   F12 capture",12,muted);
        return bitmap;
    }
    std::unique_ptr<Gdiplus::Bitmap> render(bool include_lcd = true) {
        if (!inspector_view) return render_player();
        if (gba) return render_gba(include_lcd);
        inspector->capture(*cpu, *bus);
        const auto state = inspector->snapshot();
        auto rendered = std::make_unique<Gdiplus::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        if (rendered->GetLastStatus() != Gdiplus::Ok) throw std::runtime_error("cannot allocate HUD canvas");
        Gdiplus::Graphics graphics(rendered.get());
        auto *context = &graphics;
        context->SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        context->SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        context->SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        fill(context, 0, 0, canvas_width, canvas_height, {0.035, 0.06, 0.08});
        fill(context, 0, 0, canvas_width, 66, {0.065, 0.105, 0.13});
        draw_logo(context, 24, 12, 40);
        text(context, 78, 16, "MATCHABOY", 23, cyan);
        text(context, 305, 22, title.size() > 55 ? title.substr(0, 52) + "..." : title, 13);
        text(context, 890, 16, paused ? "PAUSED / LIVE HARDWARE STATE" : "RUNNING / LIVE HARDWARE STATE", 12, green);
        text(context, 890, 36, format("FRAME %llu  T %llu", state.frames, state.cycles), 12, muted);
        constexpr std::array<const char *, 4> tabs{"1  Video", "2  CPU", "3  Memory", "4  Audio"};
        for (unsigned i = 0; i < tabs.size(); ++i) {
            fill(context, 48 + i * 230, 92, 218, 48, i == inspector_tab ? Color{0.15,0.32,0.31} : Color{0.07,0.11,0.13});
            text(context, 68 + i * 230, 106, tabs[i], 17, i == inspector_tab ? foreground : muted);
        }
        fill(context, 1016, 92, 216, 48, {0.07,0.11,0.13});
        text(context, 1038, 106, "Back to game", 17, cyan);
        if (inspector_tab == 0) {
        text(context, 48, 180, "VIDEO OUTPUT", 20, cyan);
        if (include_lcd) {
            std::array<BYTE, 160 * 144 * 4> pixels{};
            for (unsigned i = 0; i < 160 * 144; ++i) {
                const auto c = ink((green_palette ? shades : grayscale)[state.lcd[i] & 3]);
                pixels[i*4] = c.GetB(); pixels[i*4+1] = c.GetG();
                pixels[i*4+2] = c.GetR(); pixels[i*4+3] = 255;
            }
            Gdiplus::Bitmap lcd(160, 144, 160*4, PixelFormat32bppARGB, pixels.data());
            context->DrawImage(&lcd, Gdiplus::Rect(48, 232, 640, 576), 0, 0, 160, 144, Gdiplus::UnitPixel);
        }
        text(context, 736, 180, "PIXEL PIPELINE", 20, cyan);
        text(context, 736, 230, format("Scanline %u   Dot %u", state.ly, state.dot), 17);
        text(context, 736, 265, format("Mode %u   STAT %u   Output X %u", state.mode, state.reported_mode, state.output_x), 16);
        text(context, 736, 300, format("VRAM %s   OAM %s", state.vram_locked ? "locked" : "open", state.oam_locked ? "locked" : "open"), 16, muted);
        text(context, 736, 368, "FIFO / next pixel at left", 16, cyan);
        for (unsigned row = 0; row < 2; ++row) {
            text(context, 736, 412 + row * 86, row ? "OBJ" : "BG", 15, muted);
            for (unsigned pixel = 0; pixel < 8; ++pixel) {
                const auto color = row ? state.object_fifo[pixel] : state.background_fifo[pixel];
                const bool valid = row || pixel < state.fifo_depth;
                fill(context, 786 + pixel * 52, 402 + row * 86, 44, 44, {0.10, 0.18, 0.20});
                text(context, 800 + pixel * 52, 413 + row * 86, valid ? format("%u", color) : "-", 16);
                const auto tile = row ? state.object_tile_ids[pixel] : state.background_tile_ids[pixel];
                text(context, 790 + pixel * 52, 450 + row * 86, valid && tile != 0xFFFF ? format("%03X", tile) : "---", 11, muted);
            }
        }
        constexpr std::array<const char *, 4> phases{"Tile ID", "Low byte", "High byte", "Push"};
        text(context, 736, 620, format("Fetch: %s (%u)", phases[std::min(state.fetch_phase, 3U)], state.fetch_ticks), 16);
        text(context, 736, 657, format("Tile $%02X   X %u   Planes %02X/%02X", state.tile, state.tile_x, state.tile_low, state.tile_high), 15, muted);
        text(context, 736, 694, format("%s   Object %s (%u)", state.window ? "Window" : "Background", state.object_fetching ? "fetch" : "idle", state.object_phase), 15, muted);
        text(context, 736, 731, format("Discard %u+%u   BGP %02X  OBP %02X/%02X", state.previsible_discard, state.fine_discard, state.bgp, state.obp0, state.obp1), 14, muted);
        }
        if (inspector_tab == 1) {
        context->TranslateTransform(0, 80);
        text(context, 48, 82, "CPU / RETIRED INSTRUCTIONS", 14, cyan);
        const auto &r = state.registers;
        text(context, 48, 111, format("PC %04X SP %04X A %02X F %02X", state.pc, state.sp, r[0], r[1]), 12);
        text(context, 48, 133, format("BC %02X%02X DE %02X%02X HL %02X%02X", r[2], r[3], r[4], r[5], r[6], r[7]), 12);
        text(context, 48, 155, format("%c%c%c%c IME %u IE %02X IF %02X", r[1]&128?'Z':'-', r[1]&64?'N':'-',
            r[1]&32?'H':'-', r[1]&16?'C':'-', state.ime, state.ie, state.interrupt_flags), 12, orange);
        fill(context, 48, 181, 1184, 590, {0.045, 0.08, 0.10});
        const unsigned end = state.trace_count > trace_scroll ? state.trace_count - trace_scroll : 0;
        const unsigned begin = end > 25 ? end - 25 : 0;
        for (unsigned i = begin; i < end; ++i) {
            const auto &record = state.trace[i];
            text(context, 510, 192 + (i - begin) * 22,
                 format("%04X  %.37s", record.pc, record.text.data()), 15,
                 i + 1 == end ? green : foreground);
        }

        text(context, 68, 214, "RETIRED INSTRUCTION HISTORY", 16, cyan);
        text(context, 68, 255, "Scroll to browse older entries.", 14, muted);
        text(context, 68, 301, "S  Step one instruction", 15);
        text(context, 68, 337, "F  Advance one frame", 15);
        text(context, 68, 373, "D  Advance one clock dot", 15);
        context->ResetTransform();
        }
        if (inspector_tab == 2) {
        auto heat = std::make_unique<std::array<dmg::HeatCell, 65536>>();
        inspector->heatmap(*heat);
        context->TranslateTransform(24, 100);
        text(context, 24, 82, "ADDRESS BUS / 65,536 CELLS", 14, cyan);
        text(context, 24, 106, "BLUE read   RED write   GREEN execute / decay per frame", 11, muted);
        std::array<std::uint8_t, 256 * 256 * 4> pixels{};
        for (unsigned address = 0; address < 65536; ++address) {
            const auto rgb = dmg::Autopsy::heat_rgb((*heat)[address]);
            for (unsigned channel = 0; channel < 3; ++channel) pixels[address * 4 + channel] = rgb[channel];
            pixels[address * 4 + 3] = 255;
        }
        std::vector<BYTE> heat_bgra(pixels.size());
        for (unsigned i = 0; i < 65536; ++i) {
            heat_bgra[i*4] = pixels[i*4+2]; heat_bgra[i*4+1] = pixels[i*4+1];
            heat_bgra[i*4+2] = pixels[i*4]; heat_bgra[i*4+3] = 255;
        }
        Gdiplus::Bitmap heat_image(256, 256, 1024, PixelFormat32bppARGB, heat_bgra.data());
        context->DrawImage(&heat_image, Gdiplus::Rect(24, 130, 512, 512), 0, 0, 256, 256, Gdiplus::UnitPixel);
        text(context, 24, 648, "$0000 TOP-LEFT / $FFFF BOTTOM-RIGHT", 11, muted);

        context->ResetTransform();
        text(context, 640, 214, "MEMORY ACTIVITY", 22, cyan);
        text(context, 640, 260, "Each cell is one address in the 64 KiB bus.", 16);
        text(context, 640, 306, "Blue: reads   Red: writes   Green: execution", 15, muted);
        text(context, 640, 354, "Activity fades as emulated frames advance.", 15, muted);
        text(context, 640, 402, "Pause to inspect a moment in time.", 15, muted);
        }
        if (inspector_tab == 3) {
        text(context, 48, 180, "APU / FOUR REAL DIGITAL CHANNEL OUTPUTS", 14, cyan);
        text(context, 48, 214, "32 T-CYCLES / SAMPLE  |  WINDOW 3.906 ms  |  0..15 DAC INPUT", 11, muted);
        constexpr std::array<const char *, 4> names{"PULSE 1", "PULSE 2", "WAVE", "NOISE"};
        constexpr std::array<Color, 4> wave_colors{cyan, green, orange, Color{0.8,0.55,0.95}};
        for (unsigned channel = 0; channel < 4; ++channel) {
            const double y = 270 + channel * 140;
            text(context, 48, y + 4, names[channel], 12, wave_colors[channel]);
            text(context, 150, y + 4, format("%02u", state.levels[channel]), 12, muted);
            fill(context, 220, y, 1012, 104, {0.07,0.11,0.13});
            Gdiplus::Pen pen(ink(wave_colors[channel]), 1);
            std::vector<Gdiplus::PointF> points;
            for (unsigned sample = 0; sample < state.wave_count; ++sample) {
                const double x = 221 + sample * (1010.0 / (dmg::AutopsyFrame::wave_length - 1));
                const double level_y = y + 96 - state.waveforms[channel][sample] * 88.0 / 15;
                points.emplace_back(static_cast<float>(x), static_cast<float>(level_y));
            }
            if (points.size() > 1) context->DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
        }
        }
        text(context, 48, 888, "TAB game   1-4 panels   CTRL+O open   SPACE pause   S instruction   F frame   D dot   F12 capture", 12, muted);
        return rendered;
    }
    void save(const std::filesystem::path &path, Gdiplus::Bitmap &bitmap, bool gpu) {
        save_png(bitmap, path);
        if (!has_game()) {
            std::ofstream metadata(path.wstring()+L".json");
            metadata << format("{\"platform\":\"library\",\"width\":%u,\"height\":%u,\"gpu_readback\":%s,\"frames\":0,\"paused\":true,\"buttons\":0,\"inspector_view\":false,\"library_view\":true,\"library_selection\":%u,\"library_game\":-1}\n",
                bitmap.GetWidth(), bitmap.GetHeight(), gpu ? "true" : "false", library_selection);
            if (!metadata) throw std::runtime_error("Cannot write library capture metadata.");
            return;
        }
        if (gba) {
            const auto debug = gba->inspect(memory_bases[memory_region]+memory_offset);
            std::ofstream metadata(path.wstring()+L".json");
            metadata << format("{\"platform\":\"gba\",\"width\":%u,\"height\":%u,\"gpu_readback\":%s,\"frames\":%llu,\"paused\":%s,\"buttons\":%u,\"inspector_view\":%s,\"inspector_tab\":%u,\"memory_base\":%u,\"pc\":%u,\"cpsr\":%u,\"dispcnt\":%u,\"memory_first\":%u,\"audio_frames\":%u,\"library_view\":%s,\"library_selection\":%u,\"library_game\":%d}\n",
                bitmap.GetWidth(), bitmap.GetHeight(), gpu ? "true" : "false", gba->frames(), paused ? "true" : "false", buttons, inspector_view ? "true" : "false", inspector_tab, debug.memory_base, debug.registers[15], debug.cpsr, debug.dispcnt, debug.memory[0], debug.audio_frames, library_view ? "true" : "false", library_selection, library_game);
            if (!metadata) throw std::runtime_error("Cannot write capture metadata.");
            return;
        }
        inspector->capture(*cpu, *bus);
        const auto state = inspector->snapshot();
        std::ofstream metadata(path.wstring() + L".json");
        metadata << format("{\"width\":%u,\"height\":%u,\"gpu_readback\":%s,\"frames\":%llu,\"cycles\":%llu,\"ly\":%u,\"dot\":%u,\"mode\":%u,\"fifo_depth\":%u,\"instructions\":%llu,\"paused\":%s,\"buttons\":%u,\"trace_scroll\":%u,\"inspector_view\":%s,\"inspector_tab\":%u,\"library_view\":%s,\"library_selection\":%u,\"library_game\":%d}\n",
            bitmap.GetWidth(), bitmap.GetHeight(), gpu ? "true" : "false", state.frames, state.cycles,
            state.ly, state.dot, state.mode, state.fifo_depth, state.instructions, paused ? "true" : "false", buttons, trace_scroll, inspector_view ? "true" : "false", inspector_tab, library_view ? "true" : "false", library_selection, library_game);
        if (!metadata) throw std::runtime_error("cannot write capture metadata");
        std::cout << "Captured " << utf8(path.wstring()) << '\n';
    }
};

class Window {
  public:
    Runtime &runtime;
    HWND handle{};
    HDC dc{};
    HGLRC gl{};
    GLuint texture{};
    GLuint lcd_texture{};
    unsigned texture_width = 0, texture_height = 0;
    bool chrome_valid = false, cached_paused = false, cached_controls = true, cached_inspector = false;
    bool cached_library = false;
    unsigned cached_library_selection = 0;
    unsigned cached_tab = 0;
    std::chrono::steady_clock::time_point inspector_refresh{};
    std::string cached_title;
    std::filesystem::path capture;
    bool window_test{}, failed{};
    bool capture_requested{};
    WindowsAudio audio;
    GraphicsAdapter graphics_adapter;
    bool audio_suspended = true;
    std::uint64_t generated_frames = 0;
    unsigned generated_peak = 0;
    void sync_audio() {
        const bool suspended = runtime.library_view || !runtime.has_game() || runtime.paused || runtime.sound_muted;
        if (suspended == audio_suspended) return;
        audio.reset();
        std::array<std::int16_t, 8192> discarded{};
        while (runtime.drain_audio(discarded)) {}
        audio_suspended = suspended;
    }
    void pump_audio() {
        std::array<std::int16_t, 8192> samples{};
        while (const auto count = runtime.drain_audio(samples)) {
            generated_frames += count/2;
            for (std::size_t i=0; i<count; ++i) generated_peak = std::max(generated_peak, static_cast<unsigned>(std::abs(static_cast<int>(samples[i]))));
            if (!runtime.library_view && !runtime.sound_muted && !runtime.paused) audio.submit(std::span(samples).first(count));
        }
    }
    void toggle_sound() {
        runtime.sound_muted = !runtime.sound_muted;
        CheckMenuItem(GetMenu(handle), 1006, MF_BYCOMMAND | (runtime.sound_muted ? MF_UNCHECKED : MF_CHECKED));
        sync_audio();
    }
    std::chrono::steady_clock::time_point next_frame = std::chrono::steady_clock::now();
    explicit Window(Runtime &r) : runtime(r) {}
    void toggle_controls() {
        runtime.controls_visible = !runtime.controls_visible;
        CheckMenuItem(GetMenu(handle), 1005, MF_BYCOMMAND | (runtime.controls_visible ? MF_CHECKED : MF_UNCHECKED));
        InvalidateRect(handle, nullptr, FALSE);
    }
    void update_title() {
        SetWindowTextW(handle, runtime.library_view ? L"Matchaboy - Original arcade" : (L"Matchaboy - " + wide(runtime.title)).c_str());
    }
    void show_library() {
        if (runtime.library_view) return;
        runtime.library_was_paused = runtime.paused;
        runtime.library_view = true;
        runtime.paused = true;
        runtime.set_buttons(0);
        sync_audio();
        chrome_valid = false; update_title();
        InvalidateRect(handle, nullptr, FALSE);
    }
    void return_to_game() {
        if (!runtime.library_view || !runtime.has_game()) return;
        runtime.library_view = false;
        runtime.paused = runtime.library_was_paused;
        runtime.set_buttons(0);
        next_frame = std::chrono::steady_clock::now()+frame_period;
        sync_audio(); chrome_valid = false; update_title();
        InvalidateRect(handle, nullptr, FALSE);
    }
    void load_game(const std::filesystem::path &path, int library_game = -1) {
        // Flush before opening the next core, including when replaying the same cartridge.
        // Construct the replacement first so a failed open leaves the old game recoverable.
        runtime.flush_save();
        Runtime replacement(path);
        replacement.sound_muted = runtime.sound_muted;
        replacement.green_palette = runtime.green_palette;
        replacement.controls_visible = runtime.controls_visible;
        replacement.library_selection = runtime.library_selection;
        replacement.library_game = library_game;
        if (library_game >= 0) replacement.title = arcade_games()[library_game].title;
        replacement.run_frame();
        runtime = std::move(replacement);
        audio.reset(); audio_suspended = true;
        if (!window_test) runtime.enable_audio();
        next_frame = std::chrono::steady_clock::now()+frame_period;
        chrome_valid = false; update_title();
    }
    void play_library_game() {
        const auto games = arcade_games();
        if (runtime.library_selection >= games.size()) return;
        try {
            load_game(arcade_rom_path(runtime.library_selection), static_cast<int>(runtime.library_selection));
        } catch (const std::exception &error) {
            MessageBoxW(handle,wide(error.what()).c_str(),L"Unable to play library game",MB_OK | MB_ICONINFORMATION);
        }
        sync_audio(); InvalidateRect(handle,nullptr,FALSE);
    }
    void open_game() {
        const bool was_paused = runtime.paused;
        runtime.paused = true;
        sync_audio();
        runtime.set_buttons(0);
        std::array<wchar_t, 32768> file{};
        OPENFILENAMEW dialog{}; dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = handle;
        dialog.lpstrFilter = L"Game Boy / Advance ROM (*.gb;*.gba)\0*.gb;*.gba\0";
        dialog.lpstrFile = file.data(); dialog.nMaxFile = static_cast<DWORD>(file.size());
        dialog.lpstrTitle = L"Open a game in Matchaboy";
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog)) {
            try {
                load_game(file.data());
            } catch (const std::exception &error) {
                runtime.paused = was_paused;
                MessageBoxW(handle, wide(error.what()).c_str(), L"Unable to open game", MB_OK | MB_ICONINFORMATION);
            }
        } else runtime.paused = was_paused;
        sync_audio();
        next_frame = std::chrono::steady_clock::now() + frame_period;
        InvalidateRect(handle, nullptr, FALSE);
    }
    void click(int x, int y) {
        RECT client{}; GetClientRect(handle, &client);
        const double scale = std::min(client.right / double(canvas_width), client.bottom / double(canvas_height));
        if (scale <= 0) return;
        const double cx = (x-(client.right-canvas_width*scale)/2)/scale;
        const double cy = (y-(client.bottom-canvas_height*scale)/2)/scale;
        if (runtime.library_view) {
            if (cy >= 20 && cy <= 68) {
                if (cx >= 1040 && cx <= 1248) open_game();
                else if (cx >= 834 && cx <= 1024) return_to_game();
            } else if (cx >= 844 && cx <= 1224 && cy >= 334 && cy <= 380) play_library_game();
            else if (cy >= 230 && cy < 828) {
                const unsigned row = static_cast<unsigned>((cy-230)/122);
                if (cy-230-row*122 <= 110) {
                    int choice = cx >= 32 && cx <= 402 ? static_cast<int>(row) : cx >= 426 && cx <= 796 ? static_cast<int>(row+5) : -1;
                    if (choice >= 0 && static_cast<std::size_t>(choice) < arcade_games().size()) runtime.library_selection = static_cast<unsigned>(choice);
                }
            }
            chrome_valid = false; InvalidateRect(handle,nullptr,FALSE); return;
        }
        if (runtime.inspector_view && cy >= 92 && cy <= 140) {
            if (cx >= 48 && cx < 968) runtime.inspector_tab = static_cast<unsigned>((cx - 48) / 230);
            else if (cx >= 1016 && cx <= 1232) runtime.toggle_inspector();
        }
        if (!runtime.inspector_view) {
            if (cx >= 578 && cx <= 694 && cy >= 20 && cy <= 68) { show_library(); return; }
            if (cx >= 710 && cx <= 884 && cy >= 20 && cy <= 68) toggle_controls();
            if (cx >= 900 && cx <= 1058 && cy >= 20 && cy <= 68) open_game();
            else if (cx >= 1076 && cx <= 1240 && cy >= 20 && cy <= 68) runtime.toggle_inspector();
            else if (cx >= 48 && cx <= 208 && cy >= 872 && cy <= 908) runtime.paused = !runtime.paused;
        }
        if (runtime.gba && runtime.inspector_view && runtime.inspector_tab==2 && cy>=156 && cy<=196 && cx>=48 && cx<1230) {
            runtime.memory_region=static_cast<unsigned>((cx-48)/197);runtime.memory_offset=0;chrome_valid=false;
        }
        sync_audio();
        InvalidateRect(handle, nullptr, FALSE);
    }
    ~Window() {
        if (gl) {
            wglMakeCurrent(dc, gl);
            if (texture) glDeleteTextures(1, &texture);
            if (lcd_texture) glDeleteTextures(1, &lcd_texture);
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(gl);
        }
        if (dc && handle) ReleaseDC(handle, dc);
        if (handle && IsWindow(handle)) DestroyWindow(handle);
    }
    static unsigned button(WPARAM key) {
        switch (key) {
            case VK_RIGHT: return 0; case VK_LEFT: return 1; case VK_UP: return 2; case VK_DOWN: return 3;
            case 'Z': return 4; case 'X': return 5; case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return 6;
            case VK_RETURN: return 7; case 'Q': return 8; case 'W': return 9; default: return 10;
        }
    }
    void key(WPARAM key, bool down, bool repeated) {
        if (down && !repeated && key == 'L' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (runtime.library_view) return_to_game(); else show_library();
            return;
        }
        if (runtime.library_view) {
            if (down && !repeated) {
                if (key == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) open_game();
                else if (key == VK_ESCAPE) return_to_game();
                else if (key == VK_RETURN) play_library_game();
                else if (key == VK_F12) capture_requested = true;
                else if (key == 'M') toggle_sound();
                else if (!arcade_games().empty()) {
                    unsigned choice = runtime.library_selection;
                    if (key == VK_UP && choice%5 > 0) --choice;
                    else if (key == VK_DOWN && choice%5 < 4) ++choice;
                    else if (key == VK_LEFT && choice >= 5) choice -= 5;
                    else if (key == VK_RIGHT && choice < 5) choice += 5;
                    if (choice < arcade_games().size()) runtime.library_selection = choice;
                }
                chrome_valid = false; InvalidateRect(handle,nullptr,FALSE);
            }
            return;
        }
        if (down && !repeated) {
            if (key == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) { open_game(); return; }
            if (runtime.inspector_view && key >= '1' && key <= '4') runtime.inspector_tab = static_cast<unsigned>(key - '1');
            else if (key == VK_TAB) runtime.toggle_inspector();
            else if (runtime.gba && runtime.inspector_view && runtime.inspector_tab == 2 && (key == VK_PRIOR || key == VK_NEXT)) { runtime.memory_page(key == VK_NEXT ? 1 : -1); chrome_valid=false; }
            else if (key == 'C') toggle_controls();
            else if (key == 'M') toggle_sound();
            else if (key == VK_SPACE) runtime.paused = !runtime.paused;
            else if (key == 'S' && runtime.inspector_view) { runtime.paused = true; runtime.step(); }
            else if (key == 'F' && runtime.inspector_view) { runtime.paused = true; runtime.run_frame(); }
            else if (key == 'D' && runtime.inspector_view && !runtime.gba) { runtime.paused = true; runtime.bus->tick(1); }
            else if (key == VK_F12) capture_requested = true;
        }
        const unsigned bit = button(key);
        if (bit < (runtime.gba ? 10U : 8U)) {
            if (down) runtime.buttons |= static_cast<std::uint16_t>(1U << bit);
            else runtime.buttons &= static_cast<std::uint16_t>(~(1U << bit));
            runtime.set_buttons(runtime.buttons);
        }
        sync_audio();
        if (down && !repeated && runtime.paused && runtime.inspector_view && (key == 'S' || key == 'F' || key == 'D')) pump_audio();
        InvalidateRect(handle, nullptr, FALSE);
    }
    void paint() {
        if (!wglMakeCurrent(dc, gl)) throw std::runtime_error("OpenGL context unavailable");
        const auto paint_time = std::chrono::steady_clock::now();
        const bool refresh = capture_requested || window_test || !chrome_valid || cached_library != runtime.library_view || cached_library_selection != runtime.library_selection || cached_inspector != runtime.inspector_view || cached_tab != runtime.inspector_tab ||
            (runtime.inspector_view && paint_time >= inspector_refresh) || cached_title != runtime.title || cached_paused != runtime.paused || cached_controls != runtime.controls_visible;
        if (refresh) {
        auto rendered = runtime.library_view ? runtime.render_library() : runtime.inspector_view ? runtime.render(false) : runtime.render_player(false);
        Gdiplus::Rect rect(0, 0, canvas_width, canvas_height);
        Gdiplus::BitmapData data{};
        if (rendered->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok)
            throw std::runtime_error("cannot read rendered HUD");
        std::vector<std::uint8_t> rgba(canvas_width * canvas_height * 4);
        for (unsigned y = 0; y < canvas_height; ++y) {
            const auto *row = static_cast<const BYTE *>(data.Scan0) + static_cast<std::ptrdiff_t>(y) * data.Stride;
            for (unsigned x = 0; x < canvas_width; ++x) {
                const auto offset = (y * canvas_width + x) * 4;
                rgba[offset] = row[x*4+2]; rgba[offset+1] = row[x*4+1];
                rgba[offset+2] = row[x*4]; rgba[offset+3] = 255;
            }
        }
        rendered->UnlockBits(&data);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas_width, canvas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        chrome_valid = true;
        cached_library = runtime.library_view; cached_library_selection = runtime.library_selection;
        cached_inspector = runtime.inspector_view; cached_tab = runtime.inspector_tab;
        inspector_refresh = paint_time + std::chrono::milliseconds(67);
        cached_title = runtime.title; cached_paused = runtime.paused;
        cached_controls = runtime.controls_visible;
        }
        RECT client{}; GetClientRect(handle, &client);
        const int width = client.right, height = client.bottom;
        if (!width || !height) return;
        const double scale = std::min(width / double(canvas_width), height / double(canvas_height));
        const int vw = static_cast<int>(canvas_width * scale), vh = static_cast<int>(canvas_height * scale);
        const int vx = (width-vw)/2, vy = (height-vh)/2;
        glViewport(vx, vy, vw, vh);
        glClearColor(0.035F, 0.06F, 0.08F, 1); glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture);
        glColor3f(1,1,1); glBegin(GL_QUADS);
        glTexCoord2f(0,1); glVertex2f(0,0); glTexCoord2f(1,1); glVertex2f(1,0);
        glTexCoord2f(1,0); glVertex2f(1,1); glTexCoord2f(0,0); glVertex2f(0,1);
        glEnd();
        if (!runtime.library_view && runtime.has_game() && (!runtime.inspector_view || runtime.inspector_tab == 0)) {
            std::array<BYTE, 240*160*4> pixels{};
            for (unsigned i = 0; i < runtime.lcd_width()*runtime.lcd_height(); ++i) {
                const auto c = runtime.lcd_color(i);
                pixels[i*4] = c.GetR(); pixels[i*4+1] = c.GetG();
                pixels[i*4+2] = c.GetB(); pixels[i*4+3] = 255;
            }
            glBindTexture(GL_TEXTURE_2D, lcd_texture);
            if (texture_width != runtime.lcd_width() || texture_height != runtime.lcd_height()) {
                texture_width = runtime.lcd_width(); texture_height = runtime.lcd_height();
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            }
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texture_width, texture_height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            const double lcd_x = runtime.inspector_view ? 48 : runtime.lcd_left();
            const double lcd_y = runtime.inspector_view ? 232 : runtime.lcd_top();
            const double lcd_w = runtime.inspector_view ? 640 : 800;
            const double lcd_h = runtime.inspector_view ? (runtime.gba ? 427 : 576) : runtime.display_height();
            const float left = static_cast<float>(lcd_x)/canvas_width, right = static_cast<float>(lcd_x+lcd_w)/canvas_width;
            const float top = 1-static_cast<float>(lcd_y)/canvas_height, bottom = 1-static_cast<float>(lcd_y+lcd_h)/canvas_height;
            glBegin(GL_QUADS);
            glTexCoord2f(0,1); glVertex2f(left,bottom); glTexCoord2f(1,1); glVertex2f(right,bottom);
            glTexCoord2f(1,0); glVertex2f(right,top); glTexCoord2f(0,0); glVertex2f(left,top);
            glEnd();
        }
        glDisable(GL_TEXTURE_2D);
        if (capture_requested || window_test) {
            std::vector<BYTE> pixels(static_cast<std::size_t>(vw) * vh * 4), bgra(pixels.size());
            glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(vx, vy, vw, vh, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL back-buffer readback failed");
            for (int y = 0; y < vh; ++y) for (int x = 0; x < vw; ++x) {
                const auto src = (static_cast<std::size_t>(vh-1-y)*vw+x)*4;
                const auto dst = (static_cast<std::size_t>(y)*vw+x)*4;
                bgra[dst] = pixels[src+2]; bgra[dst+1] = pixels[src+1]; bgra[dst+2] = pixels[src]; bgra[dst+3] = 255;
            }
            Gdiplus::Bitmap bitmap(vw, vh, vw*4, PixelFormat32bppARGB, bgra.data());
            runtime.save(capture.empty() ? L"autopsy-capture.png" : capture, bitmap, true);
            const auto audio_path = capture.empty() ? std::filesystem::path(L"autopsy-capture.png.audio.json") : std::filesystem::path(capture.wstring()+L".audio.json");
            std::ofstream audio_report(audio_path);
            const auto queued = audio.queued();
            audio_report << format("{\"device_open\":%s,\"muted\":%s,\"paused\":%s,\"queued_buffers\":%u,\"submitted_frames\":%llu,\"completed_frames\":%llu,\"peak\":%u,\"sample_rate\":48000,\"generated_frames\":%llu,\"generated_peak\":%u,\"underruns\":%llu,\"dropped_frames\":%llu,\"max_gap_ms\":%llu,\"underrun_gap_ms\":%llu}\n",
                audio.available() ? "true" : "false", runtime.sound_muted ? "true" : "false", runtime.paused ? "true" : "false", queued,
                audio.submitted_frames, audio.completed_frames, audio.peak, generated_frames, generated_peak, audio.underruns, audio.dropped_frames, audio.max_gap_ms, audio.underrun_gap_ms);
            graphics_adapter.report(capture.empty() ? std::filesystem::path(L"autopsy-capture.png.gpu.json") : std::filesystem::path(capture.wstring()+L".gpu.json"));
            capture_requested = false;
        }
        if (!SwapBuffers(dc)) throw std::runtime_error("OpenGL presentation failed");
        if (window_test) PostMessageW(handle, WM_CLOSE, 0, 0);
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
        auto *self = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
            self->handle = hwnd;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wp, lp);
        try {
            switch (message) {
                case WM_LBUTTONUP: self->click(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); return 0;
                case WM_INITMENU:
                    CheckMenuItem(GetMenu(hwnd), 1011, MF_BYCOMMAND | (self->runtime.paused ? MF_CHECKED : MF_UNCHECKED));
                    for (unsigned id : {1002U, 1011U, 1012U, 1013U, 1014U, 1020U, 1021U, 1022U, 1023U})
                        EnableMenuItem(GetMenu(hwnd), id, MF_BYCOMMAND | (self->runtime.library_view || !self->runtime.has_game() || (self->runtime.gba && id == 1014U) ? MF_GRAYED : MF_ENABLED));
                    return 0;
                case WM_COMMAND:
                    if (LOWORD(wp) == 1050) { self->show_library(); return 0; }
                    if (LOWORD(wp) == 1051) { self->return_to_game(); return 0; }
                    if (LOWORD(wp) == 1052) { if (self->runtime.library_view) self->play_library_game(); return 0; }
                    if ((self->runtime.library_view || !self->runtime.has_game()) &&
                        (LOWORD(wp) == 1002 || (LOWORD(wp) >= 1011 && LOWORD(wp) <= 1014) || (LOWORD(wp) >= 1020 && LOWORD(wp) <= 1023))) return 0;
                    if (LOWORD(wp) >= 1030 && LOWORD(wp) <= 1032) {
                        try {
                            self->graphics_adapter.select(LOWORD(wp) - 1030);
                            CheckMenuRadioItem(GetMenu(hwnd), 1030, 1032, LOWORD(wp), MF_BYCOMMAND);
                            SetWindowTextW(hwnd, (L"Matchaboy - " + wide(self->runtime.title) + L" [restart to apply GPU choice]").c_str());
                        } catch (const std::exception &error) { MessageBoxW(hwnd, wide(error.what()).c_str(), L"Matchaboy GPU settings", MB_OK | MB_ICONERROR); }
                        return 0;
                    }
                    if (LOWORD(wp) == 1010) { PostMessageW(hwnd, WM_CLOSE, 0, 0); return 0; }
                    if (LOWORD(wp) == 1011) self->runtime.paused = !self->runtime.paused;
                    if (LOWORD(wp) >= 1012 && LOWORD(wp) <= 1014 && !(self->runtime.gba && LOWORD(wp)==1014)) {
                        self->runtime.inspector_view = true;
                        self->key(LOWORD(wp) == 1012 ? 'S' : LOWORD(wp) == 1013 ? 'F' : 'D', true, false);
                    }
                    if (LOWORD(wp) == 1015) self->capture_requested = true;
                    if (LOWORD(wp) >= 1020 && LOWORD(wp) <= 1023) {
                        self->runtime.inspector_view = true; self->runtime.inspector_tab = LOWORD(wp) - 1020;
                    }
                    self->chrome_valid = false;
                    if (LOWORD(wp) == 1001) self->open_game();
                    else if (LOWORD(wp) == 1002) self->runtime.toggle_inspector();
                    else if (LOWORD(wp) == 1006) self->toggle_sound();
                    else if (LOWORD(wp) == 1005) self->toggle_controls();
                    else if (LOWORD(wp) == 1003 || LOWORD(wp) == 1004) {
                        self->runtime.green_palette = LOWORD(wp) == 1004;
                        CheckMenuRadioItem(GetMenu(hwnd), 1003, 1004, LOWORD(wp), MF_BYCOMMAND);
                    }
                    self->sync_audio();
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case WM_ERASEBKGND: return 1;
                case WM_PAINT: {
                    PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
                    try { if (self->gl) self->paint(); }
                    catch (...) { EndPaint(hwnd, &ps); throw; }
                    EndPaint(hwnd, &ps); return 0;
                }
                case WM_KEYDOWN: self->key(wp, true, (lp & (1LL << 30)) != 0); return 0;
                case WM_KEYUP: self->key(wp, false, false); return 0;
                case WM_KILLFOCUS:
                    self->runtime.set_buttons(0); return 0;
                case WM_MOUSEWHEEL:
                    if (self->runtime.library_view) return 0;
                    if (self->runtime.gba && self->runtime.inspector_view && self->runtime.inspector_tab==2) {
                        self->runtime.memory_page(GET_WHEEL_DELTA_WPARAM(wp)>0 ? -1 : 1);self->chrome_valid=false;
                        InvalidateRect(hwnd,nullptr,FALSE);return 0;
                    }
                    self->runtime.trace_scroll = static_cast<unsigned>(std::clamp(
                        static_cast<int>(self->runtime.trace_scroll) + GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA * 3, 0, 107));
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
                case WM_CLOSE: self->audio.reset(); self->runtime.flush_save(); ShowWindow(hwnd, SW_HIDE); PostQuitMessage(0); return 0;
            }
        } catch (const std::exception &error) {
            self->failed = true;
            std::cerr << "autopsy: " << error.what() << '\n';
            if (!self->window_test) MessageBoxW(hwnd, wide(error.what()).c_str(), L"Matchaboy", MB_OK | MB_ICONERROR);
            PostQuitMessage(1);
            return 0;
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
    int run(HINSTANCE instance) {
        WNDCLASSW wc{};
        wc.style = CS_OWNDC; wc.lpfnWndProc = procedure; wc.hInstance = instance;
        wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = L"MatchaboyAutopsy";
        if (!RegisterClassW(&wc)) throw std::runtime_error("cannot register native window");
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const double scale = window_test ? 1.0 : std::min({1.0, (work.right-work.left-60)/double(canvas_width), (work.bottom-work.top-80)/double(canvas_height)});
        RECT bounds{0, 0, static_cast<LONG>(canvas_width*scale), static_cast<LONG>(canvas_height*scale)};
        AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, TRUE);
        HMENU menu = CreateMenu(), file_menu = CreatePopupMenu(), emulation_menu = CreatePopupMenu();
        HMENU audio_menu = CreatePopupMenu(), tools_menu = CreatePopupMenu(), panels_menu = CreatePopupMenu();
        AppendMenuW(file_menu, MF_STRING, 1001, L"&Open game...\tCtrl+O");
        AppendMenuW(file_menu, MF_STRING, 1050, L"Game &library\tCtrl+L");
        AppendMenuW(file_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(file_menu, MF_STRING, 1010, L"E&xit");
        AppendMenuW(emulation_menu, MF_STRING, 1011, L"&Pause / resume\tSpace");
        AppendMenuW(emulation_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(emulation_menu, MF_STRING, 1012, L"Step &instruction\tS");
        AppendMenuW(emulation_menu, MF_STRING, 1013, L"Step &frame\tF");
        AppendMenuW(emulation_menu, MF_STRING, 1014, L"Step &dot\tD");
        AppendMenuW(audio_menu, MF_STRING | MF_CHECKED, 1006, L"&Sound on\tM");
        AppendMenuW(audio_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(audio_menu, MF_STRING | MF_CHECKED, 1005, L"Show &controls\tC");
        AppendMenuW(audio_menu, MF_STRING, 1003, L"Palette: &Grayscale");
        AppendMenuW(audio_menu, MF_STRING, 1004, L"Palette: Original &green");
        HMENU gpu_menu = CreatePopupMenu();
        AppendMenuW(gpu_menu, MF_STRING, 1030, L"&Automatic (prefer dedicated)");
        AppendMenuW(gpu_menu, MF_STRING, 1031, (L"&Power saving - " + graphics_adapter.low).c_str());
        AppendMenuW(gpu_menu, MF_STRING, 1032, (L"&High performance - " + graphics_adapter.high).c_str());
        CheckMenuRadioItem(gpu_menu, 1030, 1032, 1030 + graphics_adapter.preference, MF_BYCOMMAND);
        AppendMenuW(gpu_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(gpu_menu, MF_STRING | MF_GRAYED, 1040, L"Active GPU: detecting...");
        AppendMenuW(gpu_menu, MF_STRING | MF_GRAYED, 1041, L"Changes apply after restarting Matchaboy");
        AppendMenuW(audio_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(audio_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(gpu_menu), L"&Graphics processor");
        CheckMenuRadioItem(audio_menu, 1003, 1004, runtime.green_palette ? 1004 : 1003, MF_BYCOMMAND);
        AppendMenuW(tools_menu, MF_STRING, 1002, L"Toggle &Inspector\tTab");
        constexpr std::array<const wchar_t *, 4> panel_names{L"&Video\t1", L"&CPU\t2", L"&Memory\t3", L"&Audio\t4"};
        for (unsigned i = 0; i < 4; ++i) AppendMenuW(panels_menu, MF_STRING, 1020+i, panel_names[i]);
        AppendMenuW(tools_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(panels_menu), L"Inspector &panel");
        AppendMenuW(tools_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(tools_menu, MF_STRING, 1015, L"Save &screenshot\tF12");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"&File");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation_menu), L"&Emulation");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_menu), L"Audio/&Video");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools_menu), L"&Tools");
        const auto title = runtime.library_view ? L"Matchaboy - Original arcade" : L"Matchaboy - " + wide(runtime.title);
        handle = CreateWindowW(wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
            work.left+20, work.top+20, bounds.right-bounds.left, bounds.bottom-bounds.top,
            nullptr, menu, instance, this);
        if (!handle) throw std::runtime_error("cannot create native window");
        dc = GetDC(handle);
        PIXELFORMATDESCRIPTOR descriptor{};
        descriptor.nSize = sizeof(descriptor); descriptor.nVersion = 1;
        descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        descriptor.iPixelType = PFD_TYPE_RGBA; descriptor.cColorBits = 24;
        const int pixel_format = ChoosePixelFormat(dc, &descriptor);
        if (!pixel_format || !SetPixelFormat(dc, pixel_format, &descriptor)) throw std::runtime_error("OpenGL pixel format unavailable");
        gl = wglCreateContext(dc);
        if (!gl || !wglMakeCurrent(dc, gl)) throw std::runtime_error("cannot initialize native OpenGL");
        if (const auto *renderer = glGetString(GL_RENDERER)) graphics_adapter.renderer = reinterpret_cast<const char *>(renderer);
        ModifyMenuW(gpu_menu, 1040, MF_BYCOMMAND | MF_STRING | MF_GRAYED, 1040, (L"Active: " + wide(graphics_adapter.renderer)).c_str());
        // The emulator owns the 59.7275 Hz clock. A second display-refresh
        // wait in SwapBuffers can delay both emulation and PCM submission.
        using SwapInterval = BOOL (WINAPI *)(int);
        const auto swap_interval = reinterpret_cast<SwapInterval>(wglGetProcAddress("wglSwapIntervalEXT"));
        if (swap_interval) swap_interval(0);
        glGenTextures(1, &texture);
        glGenTextures(1, &lcd_texture);
        glBindTexture(GL_TEXTURE_2D, lcd_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 160, 144, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        if (!window_test) {
            if (!audio.open()) {
                ModifyMenuW(audio_menu, 1006, MF_BYCOMMAND | MF_STRING | MF_GRAYED, 1006, L"No audio device available");
            }
            runtime.enable_audio();
        }
        ShowWindow(handle, SW_SHOW); UpdateWindow(handle);
        // WM_TIMER is coarse and resetting its deadline each frame loses time.
        // Keep an absolute hardware-rate schedule and wait without blocking input.
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* high resolution */, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        if (!timer) throw std::runtime_error("cannot create frame timer");
        struct TimerGuard { HANDLE value; ~TimerGuard() { CloseHandle(value); } } timer_guard{timer};
        next_frame = std::chrono::steady_clock::now() + frame_period;
        // Participate in Windows multimedia scheduling so background work
        // cannot routinely starve the thread that supplies the audio device.
        DWORD task_index{};
        HANDLE multimedia_task = AvSetMmThreadCharacteristicsW(L"Games", &task_index);
        struct MultimediaGuard {
            HANDLE value;
            ~MultimediaGuard() { if (value) AvRevertMmThreadCharacteristics(value); }
        } multimedia_guard{multimedia_task};
        MSG msg{};
        for (;;) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return failed ? 1 : static_cast<int>(msg.wParam);
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            sync_audio();
            auto now = std::chrono::steady_clock::now();
            if (runtime.paused || runtime.library_view || !runtime.has_game()) next_frame = now + frame_period;
            else if (now >= next_frame) {
                // Bound recovery after a stalled host or window drag.
                if (now-next_frame > frame_period*8) next_frame = now;
                do {
                    runtime.run_frame(); pump_audio();
                    next_frame += frame_period + std::chrono::microseconds(audio.pacing_adjustment_us());
                }
                while (next_frame <= now);
                InvalidateRect(handle, nullptr, FALSE);
                UpdateWindow(handle);
            }
            const auto remaining = next_frame-std::chrono::steady_clock::now();
            // Already late: handle messages and catch up immediately. Arming
            // another timer here added an extra scheduler delay every frame.
            if (remaining <= std::chrono::steady_clock::duration::zero()) continue;
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<LONGLONG>(1, std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count()/100);
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) throw std::runtime_error("cannot schedule frame");
            if (MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_FAILED)
                throw std::runtime_error("frame wait failed");
        }
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    bool headless = false, window_test = false;
    try {
        SetProcessDPIAware();
        int argc{};
        auto *raw = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!raw) throw std::runtime_error("cannot read command line");
        std::vector<std::wstring> args(raw, raw+argc); LocalFree(raw);
        // An empty invocation is the original arcade. A file argument still opens
        // directly, and all existing ROM capture/test command lines keep working.
        const bool has_rom_path = argc >= 2 && args[1].rfind(L"--",0) != 0;
        bool library_view = !has_rom_path;
        unsigned frames = 120, line = 0, dot = 0;
        bool position = false, paused = false, inspector_view = false;
        std::filesystem::path capture;
        for (int i = has_rom_path ? 2 : 1; i < argc; ++i) {
            const auto &option = args[i];
            if (option == L"--headless") headless = true;
            else if (option == L"--window-test") window_test = true;
            else if (option == L"--paused") paused = true;
            else if (option == L"--inspector") inspector_view = true;
            else if (option == L"--library") library_view = true;
            else {
                if (i+1 == argc) throw std::runtime_error("missing option value");
                const auto value = args[++i];
                if (option == L"--capture") capture = value;
                else if (option == L"--frames" || option == L"--line" || option == L"--dot") {
                    if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos) throw std::runtime_error("invalid numeric option");
                    const auto n = std::stoul(value);
                    if (n > 1000000) throw std::runtime_error("numeric option out of range");
                    if (option == L"--frames") frames = static_cast<unsigned>(n);
                    else if (option == L"--line") { line = static_cast<unsigned>(n); position = true; }
                    else { dot = static_cast<unsigned>(n); position = true; }
                } else throw std::runtime_error("unknown option: " + utf8(option));
            }
        }
        if (line > 153 || dot > 455) throw std::runtime_error("invalid PPU position");
        if ((headless || window_test) && capture.empty()) throw std::runtime_error("capture mode requires --capture");
        if (headless && window_test) throw std::runtime_error("choose headless or native window capture");
        Imaging imaging;
        Runtime runtime;
        if (has_rom_path) runtime = Runtime(args[1]);
        runtime.inspector_view = inspector_view && runtime.has_game();
        for (unsigned frame = 0; runtime.has_game() && !library_view && frame < frames; ++frame) runtime.run_frame();
        if (position) runtime.position(line, dot);
        runtime.library_view = library_view;
        runtime.library_was_paused = paused || headless || window_test;
        runtime.paused = library_view || paused || headless || window_test;
        if (headless || (!capture.empty() && !window_test)) {
            auto bitmap = runtime.library_view ? runtime.render_library() : runtime.render(); runtime.save(capture, *bitmap, false);
        }
        if (headless) return 0;
        Window window(runtime); window.capture = capture; window.window_test = window_test;
        return window.run(instance);
    } catch (const std::exception &error) {
        std::cerr << "autopsy: " << error.what() << '\n';
        if (!headless && !window_test) MessageBoxW(nullptr, wide(error.what()).c_str(), L"Matchaboy", MB_OK | MB_ICONERROR);
        return 1;
    }
}

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
#include "windows_keyboard_settings.hpp"
#include "keyboard_mapping.hpp"
#include "friend_session.hpp"
#include "gameboy_save.hpp"
#include "windows_friend_dialog.hpp"
#include <future>
#include <thread>
#include "arcade_library.hpp"
#include "player_theme.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <array>
#include <bit>
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
using namespace matcha::theme;
matcha::keyboard::Mapping keyboard_mapping=matcha::keyboard::balanced();
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
    // Destroy the link before the consoles whose serial buses it references.
    std::unique_ptr<matcha::FriendSession> friend_session;
    std::filesystem::path rom_path;
    std::string room_code,friend_address;
    bool friend_host=false,opening_friend=false;
    std::uint16_t friend_port=0;
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
    void enable_audio() { if(friend_session)return; if (gba) gba->enable_audio(); else if (bus) bus->apu.set_sample_rate(48000); }
    std::size_t drain_audio(std::span<std::int16_t> samples) {
        if(friend_session)return friend_session->drain_audio(samples);
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
        if(friend_session)return;
        if (!gba) { if (bus) bus->set_buttons(static_cast<std::uint8_t>(value)); return; }
        constexpr std::array<unsigned, 10> mapping{4,5,6,7,0,1,2,3,9,8};
        std::uint16_t mapped = 0;
        for (unsigned i=0; i<mapping.size(); ++i) if (value & (1U<<i)) mapped |= static_cast<std::uint16_t>(1U<<mapping[i]);
        gba->set_buttons(mapped);
    }
    void flush_save() { if(gba)gba->flush_save();else if(bus)flush_gameboy_save(rom_path,*bus); }
    void disconnect_friend() {
        if(friend_session){friend_session->close();friend_session.reset();}
        room_code.clear();friend_address.clear();set_buttons(0);
    }
    std::string connection_label() const {
        if(opening_friend)return "Starting friend play... resolving the host and preparing the link";
        if(!friend_session)return {};
        const auto status=friend_session->status();
        if(status.starts_with("Linked"))return status;
        return std::string(friend_host?"Player 1":"Player 2")+" | Linked frame "+std::to_string(friend_session->frames())+" | "+status;
    }
    void toggle_inspector() { if (has_game() && !library_view) inspector_view = !inspector_view; }

    Runtime() = default;
    Runtime(Runtime &&) noexcept = default;
    Runtime &operator=(Runtime &&other) noexcept {
        if(this!=&other){std::destroy_at(this);std::construct_at(this,std::move(other));}
        return *this;
    }
    ~Runtime() = default;
    explicit Runtime(const std::filesystem::path &path) : rom_path(path), title(utf8(path.filename().wstring())), library_view(false) {
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
        load_gameboy_save(path,*bus);
        bus->autopsy = inspector.get();
        bus->apu.set_sample_rate(0); // Headless warm-up does not queue old audio.
    }
    void step() { if (friend_session || !has_game() || library_view) return; if (gba) { gba->step(); return; } bus->autopsy = inspector.get(); cpu->step(); }
    void run_frame() {
        if (!has_game() || library_view) return;
        if(friend_session){friend_session->advance(buttons);return;}
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
            wrapped_text(context,844,433,matcha::keyboard::arcade_help(game.controls,keyboard_mapping).c_str(),44,6,14,foreground);
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
        text(context, 591, 34, friend_session||opening_friend?"Linked game":"Library", friend_session||opening_friend?14:17,friend_session||opening_friend?muted:foreground);
        fill(context, 710, 20, 174, 48, {0.10, 0.17, 0.20});
        text(context, 726, 32, controls_visible ? "Hide controls" : "Show controls", 17, foreground);
        fill(context, 900, 20, 158, 48, {0.16, 0.34, 0.32});
        text(context, 919, 32, friend_session||opening_friend?"ROM locked":"Open game...",18,friend_session||opening_friend?muted:foreground);
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
            using namespace matcha::keyboard;
            text(context,900,145,"KEYBOARD CONTROLS",20,cyan);
            text(context,900,181,"When the game says A, press "+key_name(keyboard_mapping[4])+".",13,muted);
            text(context,900,217,"GAME BUTTON",12,muted);text(context,1072,217,"PRESS KEY",12,muted);
            constexpr std::array<unsigned,10> order{2,1,3,0,4,5,8,9,7,6};
            unsigned row=0;
            for(const auto bit:order){
                if(!gba&&bit>=8)continue;
                const double y=240+row++*40;const bool held=(buttons&(1U<<bit))!=0;
                text(context,900,y+8,std::string(button_names[bit])+(held?" *":""),15,held?green:foreground);
                text(context,1040,y+8,">",15,muted);
                fill(context,1072,y,168,32,held?Color{0.15,0.32,0.24}:Color{0.10,0.17,0.20});
                text(context,1084,y+8,key_name(keyboard_mapping[bit]),13,held?green:foreground);
            }
            text(context,900,649,"* Key held on this keyboard",12,muted);
            fill(context,900,673,340,34,{0.16,0.34,0.32});text(context,914,682,"Keyboard Settings...  Ctrl+,",14,foreground);
            const auto games=arcade_games();
            if(library_game>=0&&static_cast<std::size_t>(library_game)<games.size())
                wrapped_text(context,900,720,arcade_help(games[library_game].controls,keyboard_mapping).c_str(),43,2,12,foreground);
            else text(context,900,720,"Game actions depend on the game.",13,muted);
            text(context,900,768,friend_session?"Linked play continues in settings.":"Esc         Pause / resume",13,muted);
            text(context,900,792,"Ctrl+Shift+C  Toggle this guide",12,muted);
            text(context,900,816,"Tab Inspector   F12 Screenshot",12,muted);
            text(context,900,840,"Ctrl+Shift+M  Toggle sound",12,muted);
        }
        fill(context,48,872,160,36,{0.16,0.34,0.32});
        if(friend_session||opening_friend){
            text(context,60,880,opening_friend?"Cancel":"Disconnect",16,foreground);
            auto label=connection_label();if(label.size()>108)label=label.substr(0,105)+"...";
            text(context,232,882,label,13,friend_session&&friend_session->finished()?orange:cyan);
        }else{
            text(context,75,880,paused?"Resume":"Pause",17,foreground);
            text(context,232,882,paused?"Paused - press Escape to resume":"Playing",15,paused?orange:muted);
        }
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
        constexpr std::array<const char *,4> tabs{"Ctrl+1 Video","Ctrl+2 CPU","Ctrl+3 Memory","Ctrl+4 Audio"};
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
        text(context,48,888,"TAB game   Ctrl+1-4 panels   ESC pause   Ctrl+S instruction   Ctrl+F frame   CTRL+O open   F12 capture",12,muted);
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
        text(context, 48, 888, "TAB game   Ctrl+1-4 panels   CTRL+O open   ESC pause   Ctrl+S instruction   Ctrl+F frame   Ctrl+D dot   F12 capture", 12, muted);
        return rendered;
    }
    void save(const std::filesystem::path &path,Gdiplus::Bitmap &bitmap,bool gpu) {
        save_contents(path,bitmap,gpu);
        const std::filesystem::path metadata_path(path.wstring()+L".json");
        std::ifstream input(metadata_path);std::string body((std::istreambuf_iterator<char>(input)),{});input.close();
        const auto closing=body.find_last_of('}');
        if(closing==std::string::npos)throw std::runtime_error("Invalid capture metadata.");
        body.resize(closing);std::ofstream output(metadata_path);
        output<<body<<",\"keyboard_mapping\":[";
        for(unsigned i=0;i<keyboard_mapping.size();++i){if(i)output<<',';output<<keyboard_mapping[i];}
        output<<"],\"controls_visible\":"<<(controls_visible?"true":"false")<<",\"opening_friend\":"<<(opening_friend?"true":"false");
        if(friend_session)output<<",\"netplay\":{\"connected\":"<<(friend_session->connected()?"true":"false")
            <<",\"finished\":"<<(friend_session->finished()?"true":"false")<<",\"host\":"<<(friend_host?"true":"false")
            <<",\"frames\":"<<friend_session->frames()<<",\"verified_frames\":"<<friend_session->verified_frames()
            <<",\"port\":"<<friend_session->port()<<",\"status\":"<<std::quoted(friend_session->status())<<'}';
        output<<"}\n";if(!output)throw std::runtime_error("Unable to write capture metadata.");
    }
    void save_contents(const std::filesystem::path &path, Gdiplus::Bitmap &bitmap, bool gpu) {
        save_png(bitmap, path);
        if (!has_game()) {
            std::ofstream metadata(std::filesystem::path(path.wstring()+L".json"));
            metadata << format("{\"platform\":\"library\",\"width\":%u,\"height\":%u,\"gpu_readback\":%s,\"frames\":0,\"paused\":true,\"buttons\":0,\"inspector_view\":false,\"library_view\":true,\"library_selection\":%u,\"library_game\":-1}\n",
                bitmap.GetWidth(), bitmap.GetHeight(), gpu ? "true" : "false", library_selection);
            if (!metadata) throw std::runtime_error("Cannot write library capture metadata.");
            return;
        }
        if (gba) {
            const auto debug = gba->inspect(memory_bases[memory_region]+memory_offset);
            std::ofstream metadata(std::filesystem::path(path.wstring()+L".json"));
            metadata << format("{\"platform\":\"gba\",\"width\":%u,\"height\":%u,\"gpu_readback\":%s,\"frames\":%llu,\"paused\":%s,\"buttons\":%u,\"inspector_view\":%s,\"inspector_tab\":%u,\"memory_base\":%u,\"pc\":%u,\"cpsr\":%u,\"dispcnt\":%u,\"memory_first\":%u,\"audio_frames\":%u,\"library_view\":%s,\"library_selection\":%u,\"library_game\":%d}\n",
                bitmap.GetWidth(), bitmap.GetHeight(), gpu ? "true" : "false", gba->frames(), paused ? "true" : "false", buttons, inspector_view ? "true" : "false", inspector_tab, debug.memory_base, debug.registers[15], debug.cpsr, debug.dispcnt, debug.memory[0], debug.audio_frames, library_view ? "true" : "false", library_selection, library_game);
            if (!metadata) throw std::runtime_error("Cannot write capture metadata.");
            return;
        }
        inspector->capture(*cpu, *bus);
        const auto state = inspector->snapshot();
        std::ofstream metadata(std::filesystem::path(path.wstring()+L".json"));
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
    std::chrono::steady_clock::time_point inspector_refresh{},connection_refresh{};
    std::string cached_title,cached_connection;
    std::filesystem::path capture;
    bool window_test{}, failed{};
    bool capture_requested{};
    std::array<std::uint16_t,1024> held_keys{};
    std::future<Runtime> opening;
    bool opening_cancelled=false,before_connect_paused=false,keyboard_settings_open=false,ticking=false;
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
    void clear_buttons(){held_keys.fill(0);runtime.set_buttons(0);}
    void apply_held_buttons(){std::uint16_t value=0;for(const auto held:held_keys)value|=held;runtime.set_buttons(value);}
    bool linked() const {return runtime.friend_session||runtime.opening_friend;}
    void changed() {chrome_valid=false;sync_audio();InvalidateRect(handle,nullptr,FALSE);}
    void start_friend(matcha::FriendTransportOptions options) {
        if(!runtime.has_game()||linked()||opening.valid())throw std::runtime_error("Open a game before starting a new friend session.");
        runtime.flush_save();Runtime prepared(runtime.rom_path);
        prepared.sound_muted=runtime.sound_muted;prepared.green_palette=runtime.green_palette;
        prepared.controls_visible=runtime.controls_visible;prepared.library_selection=runtime.library_selection;
        prepared.library_game=runtime.library_game;prepared.title=runtime.title;
        prepared.room_code=options.code;prepared.friend_address=options.address;prepared.friend_host=options.host;prepared.friend_port=options.port;
        std::packaged_task<Runtime()> task([options,prepared=std::move(prepared)]() mutable {
            prepared.friend_session=std::make_unique<matcha::FriendSession>(options,prepared.rom_path,prepared.bus.get(),prepared.cpu.get(),prepared.gba.get());
            return std::move(prepared);
        });
        opening=task.get_future();opening_cancelled=false;
        std::thread(std::move(task)).detach();
        runtime.room_code=options.code;runtime.friend_address=options.address;runtime.friend_host=options.host;runtime.friend_port=options.port;
        before_connect_paused=runtime.paused;runtime.paused=true;runtime.opening_friend=true;changed();
    }
    void disconnect_friend() {
        if(runtime.opening_friend){opening_cancelled=true;runtime.opening_friend=false;runtime.paused=before_connect_paused;clear_buttons();changed();return;}
        runtime.disconnect_friend();clear_buttons();runtime.flush_save();runtime.paused=false;runtime.enable_audio();audio.reset();audio_suspended=true;changed();
    }
    void poll_opening() {
        if(!opening.valid()||opening.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return;
        if(opening_cancelled){try{auto discarded=opening.get();(void)discarded;}catch(const std::exception &){}opening_cancelled=false;changed();return;}
        try{runtime=opening.get();clear_buttons();audio.reset();audio_suspended=true;next_frame=std::chrono::steady_clock::now()+frame_period;changed();}
        catch(const std::exception &error){runtime.opening_friend=false;runtime.paused=before_connect_paused;changed();MessageBoxW(handle,wide(error.what()).c_str(),L"Unable to connect",MB_OK|MB_ICONINFORMATION);}
    }
    void settings() {
        if(keyboard_settings_open||runtime.opening_friend)return;
        const bool was_paused=runtime.paused;keyboard_settings_open=true;
        clear_buttons();if(!runtime.friend_session)runtime.paused=true;changed();
        try{
            auto draft=keyboard_mapping;
            if(matcha::keyboard::show_windows_settings(handle,draft)){
                matcha::keyboard::save_windows_mapping(draft);keyboard_mapping=draft;
            }
        }catch(const std::exception &error){MessageBoxW(handle,wide(error.what()).c_str(),L"Keyboard settings",MB_OK|MB_ICONERROR);}
        clear_buttons();runtime.paused=was_paused;keyboard_settings_open=false;
        next_frame=std::chrono::steady_clock::now()+frame_period;changed();SetFocus(handle);
    }
    void friend_dialog(bool hosting) {
        if(!runtime.has_game()||linked()||opening.valid())return;
        const bool was_paused=runtime.paused;runtime.paused=true;clear_buttons();changed();
        try{
            matcha::FriendTransportOptions options;options.host=hosting;
            options.address=hosting?"0.0.0.0":runtime.friend_address;options.code=hosting?matcha::FriendSession::make_room_code():runtime.room_code;
            matcha::windows::FriendDialog dialog(handle,options);
            const auto selected=dialog.show();runtime.paused=was_paused;
            if(selected)start_friend(*selected);
        }catch(const std::exception &error){runtime.paused=was_paused;MessageBoxW(handle,wide(error.what()).c_str(),L"Friend play",MB_OK|MB_ICONERROR);}
        next_frame=std::chrono::steady_clock::now()+frame_period;changed();SetFocus(handle);
    }
    void connection_details() {
        if(!runtime.friend_session)return;
        clear_buttons();changed();
        const auto details=runtime.connection_label()+"\r\n\r\n"+
            (runtime.friend_host?std::string("Hosting on UDP port "):std::string("Host: ")+runtime.friend_address+":"+std::to_string(runtime.friend_port)+" | Local UDP port ")+
            std::to_string(runtime.friend_session->port())+
            "\r\nOn the same Wi-Fi, your friend joins using the host's LAN IP. Internet play needs a VPN address or host UDP forwarding. There is no automatic relay or router setup. Room codes and game data travel unencrypted.\r\n\r\nIf the connection drops, copy diagnostics before disconnecting.";
        matcha::windows::FriendDialog dialog(handle,details,runtime.room_code,[this]{return runtime.friend_session?runtime.friend_session->diagnostics():"Session ended.";});
        dialog.show();clear_buttons();changed();SetFocus(handle);
    }
    void step_debug(unsigned command) {
        if(linked()||runtime.library_view||!runtime.has_game()||(command==1014&&runtime.gba))return;
        runtime.inspector_view=true;runtime.paused=true;
        if(command==1012)runtime.step();else if(command==1013)runtime.run_frame();else runtime.bus->tick(1);
        sync_audio();pump_audio();changed();
    }
    void tick() {
        if(ticking)return;
        ticking=true;struct TickGuard {bool &value;~TickGuard(){value=false;}} guard{ticking};
        poll_opening();
        if(runtime.friend_session)runtime.friend_session->service();
        sync_audio();const auto now=std::chrono::steady_clock::now();
        if((runtime.paused&&!runtime.friend_session)||runtime.library_view||!runtime.has_game())next_frame=now+frame_period;
        else if(now>=next_frame){
            if(now-next_frame>frame_period*8)next_frame=now;
            do{
                runtime.run_frame();pump_audio();
                next_frame+=frame_period+std::chrono::microseconds(runtime.friend_session?0:audio.pacing_adjustment_us());
            }while(next_frame<=now);
            InvalidateRect(handle,nullptr,FALSE);
        }
        // Refresh terminal connection messages even when no frame can advance.
        const auto label=runtime.connection_label();
        if(label!=cached_connection&&now>=connection_refresh){cached_connection=label;connection_refresh=now+std::chrono::milliseconds(100);chrome_valid=false;InvalidateRect(handle,nullptr,FALSE);}
    }

    void toggle_controls() {
        runtime.controls_visible = !runtime.controls_visible;
        CheckMenuItem(GetMenu(handle), 1005, MF_BYCOMMAND | (runtime.controls_visible ? MF_CHECKED : MF_UNCHECKED));
        InvalidateRect(handle, nullptr, FALSE);
    }
    void update_title() {
        SetWindowTextW(handle, runtime.library_view ? L"Matchaboy - Original arcade" : (L"Matchaboy - " + wide(runtime.title)).c_str());
    }
    void show_library() {
        if (runtime.library_view || linked()) return;
        runtime.library_was_paused = runtime.paused;
        runtime.library_view = true;
        runtime.paused = true;
        clear_buttons();
        sync_audio();
        chrome_valid = false; update_title();
        InvalidateRect(handle, nullptr, FALSE);
    }
    void return_to_game() {
        if (!runtime.library_view || !runtime.has_game()) return;
        runtime.library_view = false;
        runtime.paused = runtime.library_was_paused;
        clear_buttons();
        next_frame = std::chrono::steady_clock::now()+frame_period;
        sync_audio(); chrome_valid = false; update_title();
        InvalidateRect(handle, nullptr, FALSE);
    }
    void load_game(const std::filesystem::path &path, int library_game = -1) {
        if(linked())throw std::runtime_error("Disconnect friend play before opening another game.");
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
        runtime = std::move(replacement);clear_buttons();
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
        if(linked())return;
        const bool was_paused = runtime.paused;
        runtime.paused = true;
        sync_audio();
        clear_buttons();
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
            else if (cx >= 48 && cx <= 208 && cy >= 872 && cy <= 908) {if(linked())disconnect_friend();else runtime.paused = !runtime.paused;}
            else if(runtime.controls_visible&&cx>=900&&cx<=1240&&cy>=673&&cy<=707)settings();
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
        if (handle && IsWindow(handle)) {KillTimer(handle,1);DestroyWindow(handle);}
    }
    void key(WPARAM key,LPARAM data,bool down,bool repeated) {
        const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        const bool modified=control||(GetKeyState(VK_MENU)&0x8000)||(GetKeyState(VK_LWIN)&0x8000)||(GetKeyState(VK_RWIN)&0x8000);
        const auto physical=matcha::keyboard::from_windows_key(static_cast<unsigned>((data>>16)&255),(data&(1LL<<24))!=0,static_cast<unsigned>(key));
        const auto bit=matcha::keyboard::key_bit(keyboard_mapping,physical);
        const auto scan=static_cast<unsigned>((data>>16)&255);
        const auto token=scan?(scan|((data&(1LL<<24))?256U:0U)):512U+(static_cast<unsigned>(key)&255U);
        // Track physical aliases separately: releasing keypad Enter or one
        // Shift must not release another keyboard key still holding the button.
        if(!down){
            if(held_keys[token]){held_keys[token]=0;apply_held_buttons();chrome_valid=false;InvalidateRect(handle,nullptr,FALSE);}
            return;
        }
        if(control){
            if(repeated)return;
            if(key==VK_OEM_COMMA)settings();
            else if(key=='O')open_game();else if(key=='L'){if(runtime.library_view)return_to_game();else show_library();}
            else if(shift&&key=='C')toggle_controls();else if(shift&&key=='M')toggle_sound();
            else if(key=='P'){runtime.green_palette=!runtime.green_palette;CheckMenuRadioItem(GetMenu(handle),1003,1004,runtime.green_palette?1004:1003,MF_BYCOMMAND);}
            else if(!runtime.library_view&&key>='1'&&key<='4'){runtime.inspector_view=true;runtime.inspector_tab=static_cast<unsigned>(key-'1');}
            else if(key=='S'||key=='F'||key=='D')step_debug(key=='S'?1012:key=='F'?1013:1014);
            changed();return;
        }
        if(modified)return;
        if(runtime.library_view){
            if(repeated)return;
            if(key==VK_ESCAPE)return_to_game();else if(key==VK_RETURN)play_library_game();else if(key==VK_F12)capture_requested=true;
            else if(!arcade_games().empty()){
                unsigned choice=runtime.library_selection;
                if(key==VK_UP&&choice%5>0)--choice;else if(key==VK_DOWN&&choice%5<4)++choice;
                else if(key==VK_LEFT&&choice>=5)choice-=5;else if(key==VK_RIGHT&&choice<5)choice+=5;
                if(choice<arcade_games().size())runtime.library_selection=choice;
            }
            changed();return;
        }
        // Gameplay assignments take precedence over every unmodified app letter.
        if(bit<(runtime.gba?10U:8U)){
            held_keys[token]=static_cast<std::uint16_t>(1U<<bit);apply_held_buttons();chrome_valid=false;InvalidateRect(handle,nullptr,FALSE);return;
        }
        if(!repeated){
            if(key==VK_ESCAPE&&!linked())runtime.paused=!runtime.paused;
            else if(key==VK_TAB)runtime.toggle_inspector();else if(key==VK_F12)capture_requested=true;
            else if(runtime.gba&&runtime.inspector_view&&runtime.inspector_tab==2&&(key==VK_PRIOR||key==VK_NEXT))runtime.memory_page(key==VK_NEXT?1:-1);
        }
        changed();
    }
    void paint() {
        if (!wglMakeCurrent(dc, gl)) throw std::runtime_error("OpenGL context unavailable");
        const auto paint_time = std::chrono::steady_clock::now();
        const bool refresh = capture_requested || window_test || !chrome_valid || cached_library != runtime.library_view || cached_library_selection != runtime.library_selection || cached_inspector != runtime.inspector_view || cached_tab != runtime.inspector_tab ||
            (runtime.inspector_view && paint_time >= inspector_refresh) || cached_title != runtime.title || cached_paused != runtime.paused || cached_controls != runtime.controls_visible;
        if (refresh) {
        auto rendered = runtime.library_view ? runtime.render_library() : runtime.inspector_view ? runtime.render(false) : runtime.render_player(false);
        if(linked()&&runtime.inspector_view){
            Gdiplus::Graphics footer(rendered.get());fill(&footer,0,864,1280,56,{0.035,0.06,0.08});
            text(&footer,48,882,runtime.connection_label(),12,runtime.friend_session&&runtime.friend_session->finished()?orange:cyan);
        }
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
                case WM_INITMENU: {
                    CheckMenuItem(GetMenu(hwnd),1011,MF_BYCOMMAND|(self->runtime.paused?MF_CHECKED:MF_UNCHECKED));
                    for(unsigned id:{1002U,1011U,1012U,1013U,1014U,1020U,1021U,1022U,1023U}){
                        const bool disabled=self->runtime.library_view||!self->runtime.has_game()||(self->runtime.gba&&id==1014U)||
                            (self->linked()&&id>=1011U&&id<=1014U);
                        EnableMenuItem(GetMenu(hwnd),id,MF_BYCOMMAND|(disabled?MF_GRAYED:MF_ENABLED));
                    }
                    for(unsigned id:{1001U,1050U,1051U,1052U,1500U,1501U})
                        EnableMenuItem(GetMenu(hwnd),id,MF_BYCOMMAND|((self->linked()||((id==1500U||id==1501U)&&(!self->runtime.has_game()||self->opening.valid())))?MF_GRAYED:MF_ENABLED));
                    EnableMenuItem(GetMenu(hwnd),1502,MF_BYCOMMAND|(self->runtime.friend_session?MF_ENABLED:MF_GRAYED));
                    EnableMenuItem(GetMenu(hwnd),1503,MF_BYCOMMAND|(self->linked()?MF_ENABLED:MF_GRAYED));
                    EnableMenuItem(GetMenu(hwnd),1060,MF_BYCOMMAND|(self->runtime.opening_friend?MF_GRAYED:MF_ENABLED));
                    ModifyMenuW(GetMenu(hwnd),1503,MF_BYCOMMAND|MF_STRING|(self->linked()?MF_ENABLED:MF_GRAYED),1503,self->runtime.opening_friend?L"Cancel preparation":L"Disconnect");
                    return 0;
                }
                case WM_COMMAND: {
                    const auto id=LOWORD(wp);
                    if(self->linked()&&(id==1001||id==1050||id==1051||id==1052||(id>=1011&&id<=1014)||id==1500||id==1501))return 0;
                    if(id==1060){self->settings();return 0;}
                    if(id==1500||id==1501){self->friend_dialog(id==1500);return 0;}
                    if(id==1502){self->connection_details();return 0;}
                    if(id==1503){self->disconnect_friend();return 0;}
                    if(id==1050){self->show_library();return 0;}
                    if(id==1051){self->return_to_game();return 0;}
                    if(id==1052){if(self->runtime.library_view)self->play_library_game();return 0;}
                    if((self->runtime.library_view||!self->runtime.has_game())&&
                        (id==1002||(id>=1011&&id<=1014)||(id>=1020&&id<=1023)))return 0;
                    if(id>=1030&&id<=1032){
                        try{
                            self->graphics_adapter.select(id-1030);CheckMenuRadioItem(GetMenu(hwnd),1030,1032,id,MF_BYCOMMAND);
                            SetWindowTextW(hwnd,(L"Matchaboy - "+wide(self->runtime.title)+L" [restart to apply GPU choice]").c_str());
                        }catch(const std::exception &error){MessageBoxW(hwnd,wide(error.what()).c_str(),L"Matchaboy GPU settings",MB_OK|MB_ICONERROR);}
                        return 0;
                    }
                    if(id==1010){PostMessageW(hwnd,WM_CLOSE,0,0);return 0;}
                    if(id==1011)self->runtime.paused=!self->runtime.paused;
                    if(id>=1012&&id<=1014)self->step_debug(id);
                    if(id==1015)self->capture_requested=true;
                    if(id>=1020&&id<=1023){self->runtime.inspector_view=true;self->runtime.inspector_tab=id-1020;}
                    if(id==1001)self->open_game();else if(id==1002)self->runtime.toggle_inspector();
                    else if(id==1006)self->toggle_sound();else if(id==1005)self->toggle_controls();
                    else if(id==1003||id==1004){self->runtime.green_palette=id==1004;CheckMenuRadioItem(GetMenu(hwnd),1003,1004,id,MF_BYCOMMAND);}
                    self->changed();return 0;
                }
                case WM_TIMER: if(wp==1&&self->gl&&!self->window_test){self->tick();return 0;}break;
                case WM_ERASEBKGND: return 1;
                case WM_PAINT: {
                    PAINTSTRUCT ps{}; BeginPaint(hwnd, &ps);
                    try { if (self->gl) self->paint(); }
                    catch (...) { EndPaint(hwnd, &ps); throw; }
                    EndPaint(hwnd, &ps); return 0;
                }
                case WM_KEYDOWN: self->key(wp, lp, true, (lp & (1LL << 30)) != 0); return 0;
                case WM_KEYUP: self->key(wp, lp, false, false); return 0;
                case WM_SYSKEYUP: self->key(wp,lp,false,false); break;
                case WM_KILLFOCUS:
                    self->clear_buttons();self->chrome_valid=false;InvalidateRect(hwnd,nullptr,FALSE);return 0;
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
                case WM_CLOSE: KillTimer(hwnd,1);self->audio.reset();self->runtime.disconnect_friend();self->runtime.flush_save(); ShowWindow(hwnd, SW_HIDE); PostQuitMessage(0); return 0;
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
        AppendMenuW(emulation_menu, MF_STRING, 1011, L"&Pause / resume\tEsc");
        AppendMenuW(emulation_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(emulation_menu, MF_STRING, 1012, L"Step &instruction\tCtrl+S");
        AppendMenuW(emulation_menu, MF_STRING, 1013, L"Step &frame\tCtrl+F");
        AppendMenuW(emulation_menu, MF_STRING, 1014, L"Step &dot\tCtrl+D");
        AppendMenuW(audio_menu, MF_STRING | MF_CHECKED, 1006, L"&Sound on\tCtrl+Shift+M");
        AppendMenuW(audio_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(audio_menu, MF_STRING | MF_CHECKED, 1005, L"Show &controls\tCtrl+Shift+C");
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
        AppendMenuW(tools_menu,MF_STRING,1060,L"&Keyboard Settings...\tCtrl+,");
        AppendMenuW(tools_menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(tools_menu, MF_STRING, 1002, L"Toggle &Inspector\tTab");
        constexpr std::array<const wchar_t *, 4> panel_names{L"&Video\tCtrl+1", L"&CPU\tCtrl+2", L"&Memory\tCtrl+3", L"&Audio\tCtrl+4"};
        for (unsigned i = 0; i < 4; ++i) AppendMenuW(panels_menu, MF_STRING, 1020+i, panel_names[i]);
        AppendMenuW(tools_menu, MF_POPUP, reinterpret_cast<UINT_PTR>(panels_menu), L"Inspector &panel");
        AppendMenuW(tools_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(tools_menu, MF_STRING, 1015, L"Save &screenshot\tF12");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file_menu), L"&File");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(emulation_menu), L"&Emulation");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(audio_menu), L"Audio/&Video");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(tools_menu), L"&Tools");
        HMENU friend_menu=CreatePopupMenu();
        AppendMenuW(friend_menu,MF_STRING,1500,L"&Host game...");AppendMenuW(friend_menu,MF_STRING,1501,L"&Join game...");
        AppendMenuW(friend_menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(friend_menu,MF_STRING,1502,L"Connection &Details...");AppendMenuW(friend_menu,MF_STRING,1503,L"&Disconnect");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(friend_menu),L"&Netplay");
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
        const auto swap_interval = std::bit_cast<SwapInterval>(wglGetProcAddress("wglSwapIntervalEXT"));
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
        // Native modal dialogs and menu/resize loops dispatch this timer even
        // when they temporarily own the application's message loop.
        if(!SetTimer(handle,1,10,nullptr))throw std::runtime_error("Cannot create link-service timer.");
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
            tick();
            UpdateWindow(handle);
            const auto remaining=std::min(next_frame-std::chrono::steady_clock::now(),
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::milliseconds(4)));
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
        std::filesystem::path rom_path,capture,net_stop_file;
        std::string game_id,net_join,net_code;unsigned net_port=0;
        unsigned frames=120,line=0,dot=0,tab=0,buttons=0,input_frames=0,memory_region=0;
        bool library_view=false,position=false,paused=false,inspector_view=false,net_host=false,green=false,hide_controls=false;
        const auto number=[](const std::wstring &value,unsigned limit,bool hex=false){
            if(value.empty()||value.front()==L'-')throw std::runtime_error("Invalid numeric option.");
            std::size_t consumed=0;const auto result=std::stoul(value,&consumed,hex?0:10);
            if(consumed!=value.size()||result>limit)throw std::runtime_error("Numeric option out of range.");
            return static_cast<unsigned>(result);
        };
        for(int i=1;i<argc;++i){
            const auto &option=args[i];
            if(option==L"--headless")headless=true;else if(option==L"--window-test")window_test=true;
            else if(option==L"--paused")paused=true;else if(option==L"--inspector")inspector_view=true;
            else if(option==L"--library")library_view=true;else if(option==L"--green")green=true;
            else if(option==L"--hide-controls")hide_controls=true;
            else if(option.rfind(L"--",0)==0){
                if(i+1==argc)throw std::runtime_error("Missing option value.");
                const auto value=args[++i];
                if(option==L"--capture")capture=value;else if(option==L"--game")game_id=utf8(value);
                else if(option==L"--net-host"){net_host=true;net_port=number(value,65535);}
                else if(option==L"--net-join")net_join=utf8(value);else if(option==L"--net-code")net_code=utf8(value);
                else if(option==L"--net-stop-file")net_stop_file=value;
                else if(option==L"--frames")frames=number(value,1000000);
                else if(option==L"--input-frames")input_frames=number(value,1000000);
                else if(option==L"--buttons")buttons=number(value,1023,true);
                else if(option==L"--tab")tab=number(value,3);else if(option==L"--memory-region")memory_region=number(value,5);
                else if(option==L"--line"){line=number(value,153);position=true;}
                else if(option==L"--dot"){dot=number(value,455);position=true;}
                else throw std::runtime_error("Unknown option: "+utf8(option));
            }else if(rom_path.empty())rom_path=option;else throw std::runtime_error("Choose one ROM at a time.");
        }
        if(!rom_path.empty()&&!game_id.empty())throw std::runtime_error("Choose either a ROM path or --game, not both.");
        const bool networking=net_host||!net_join.empty();
        if(net_host&&!net_join.empty())throw std::runtime_error("Choose --net-host or --net-join.");
        if(!networking&&!net_code.empty())throw std::runtime_error("--net-code requires friend play.");
        if(!net_stop_file.empty()&&(!networking||!headless))throw std::runtime_error("--net-stop-file requires headless friend play.");
        if(networking&&(paused||library_view||position||input_frames))throw std::runtime_error("Friend play cannot pause, browse the library, position the PPU, or use --input-frames.");
        if((headless||window_test)&&capture.empty())throw std::runtime_error("Capture mode requires --capture.");
        if(headless&&window_test)throw std::runtime_error("Choose headless or native window capture.");
        Imaging imaging;keyboard_mapping=matcha::keyboard::load_windows_mapping();
        Runtime runtime;
        if(!game_id.empty()){
            const auto games=arcade_games();const auto found=std::find_if(games.begin(),games.end(),[&](const auto &game){return game_id==game.id;});
            if(found==games.end())throw std::runtime_error("Unknown arcade game: "+game_id);
            const auto index=static_cast<unsigned>(found-games.begin());runtime=Runtime(arcade_rom_path(index));
            runtime.library_game=static_cast<int>(index);runtime.library_selection=index;runtime.title=found->title;
        }else if(!rom_path.empty())runtime=Runtime(rom_path);
        library_view=library_view||!runtime.has_game();
        runtime.inspector_view=(inspector_view||position)&&runtime.has_game();runtime.inspector_tab=tab;runtime.memory_region=memory_region;
        runtime.green_palette=green;runtime.controls_visible=!hide_controls;
        if(networking){
            if(!runtime.has_game())throw std::runtime_error("Open a ROM before starting friend play.");
            matcha::FriendTransportOptions options;options.host=net_host;options.code=net_code;
            if(net_host){if(!net_port)throw std::runtime_error("Host UDP port must be between 1 and 65535.");options.port=static_cast<std::uint16_t>(net_port);}
            else{
                const auto separator=net_join.rfind(':');if(separator==std::string::npos)throw std::runtime_error("Use --net-join HOST:PORT.");
                options.address=net_join.substr(0,separator);const auto port=number(wide(net_join.substr(separator+1)),65535);
                if(!port)throw std::runtime_error("Friend UDP port must be between 1 and 65535.");
                options.port=static_cast<std::uint16_t>(port);
            }
            runtime.room_code=options.code;runtime.friend_address=options.address;runtime.friend_host=net_host;runtime.friend_port=options.port;
            runtime.friend_session=std::make_unique<matcha::FriendSession>(options,runtime.rom_path,runtime.bus.get(),runtime.cpu.get(),runtime.gba.get());
            runtime.set_buttons(static_cast<std::uint16_t>(buttons));
            if(headless||window_test){
                const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
                while(runtime.friend_session->frames()<frames&&!runtime.friend_session->finished()&&std::chrono::steady_clock::now()<deadline){runtime.run_frame();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
                if(runtime.friend_session->frames()<frames)throw std::runtime_error("Friend capture did not complete: "+runtime.friend_session->status());
                std::cout<<"Friend capture ready: frame "<<runtime.friend_session->frames()<<std::endl;
                const auto grace=std::chrono::steady_clock::now()+(net_stop_file.empty()?std::chrono::milliseconds(200):std::chrono::milliseconds(10000));
                do{
                    runtime.friend_session->service();if(!net_stop_file.empty()&&std::filesystem::exists(net_stop_file))break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }while(std::chrono::steady_clock::now()<grace);
            }
        }else{
            if(input_frames==0)runtime.set_buttons(static_cast<std::uint16_t>(buttons));
            for(unsigned frame=0;runtime.has_game()&&!library_view&&frame<frames;++frame)runtime.run_frame();
            if(input_frames){runtime.set_buttons(static_cast<std::uint16_t>(buttons));for(unsigned i=0;i<input_frames;++i)runtime.run_frame();}
        }
        if(position)runtime.position(line,dot);
        runtime.library_view=library_view;runtime.library_was_paused=paused||headless||window_test;
        runtime.paused=!networking&&(library_view||paused||headless||window_test);
        if (headless || (!capture.empty() && !window_test)) {
            auto bitmap = runtime.library_view ? runtime.render_library() : runtime.render(); runtime.save(capture, *bitmap, false);
        }
        if (headless) {runtime.disconnect_friend();runtime.flush_save();return 0;}
        Window window(runtime); window.capture = capture; window.window_test = window_test;
        return window.run(instance);
    } catch (const std::exception &error) {
        std::cerr << "autopsy: " << error.what() << '\n';
        if (!headless && !window_test) MessageBoxW(nullptr, wide(error.what()).c_str(), L"Matchaboy", MB_OK | MB_ICONERROR);
        return 1;
    }
}

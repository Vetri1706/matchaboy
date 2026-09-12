#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#import <OpenGL/gl.h>
#include "dmg/autopsy.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "gba_core.hpp"
#include "arcade_library.hpp"
#include "macos_audio.hpp"
#include "player_theme.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using BYTE = std::uint8_t;
using INT = int;
constexpr unsigned canvas_width = 1280, canvas_height = 920;
constexpr auto frame_period = std::chrono::nanoseconds(16742706);
constexpr int PixelFormat32bppARGB = 0;
using namespace matcha::theme;
// CoreGraphics owns the backing pixels; these small drawing types keep the
// Windows and Mac layouts in the same canvas coordinates without an external GUI library.
namespace MacGraphics {
constexpr int Ok = 0, TextRenderingHintAntiAliasGridFit = 0, InterpolationModeNearestNeighbor = 0;
constexpr int PixelOffsetModeHalf = 0, UnitPixel = 0;
struct Color {
    BYTE a, r, g, b;
    BYTE GetR() const { return r; } BYTE GetG() const { return g; } BYTE GetB() const { return b; }
};
struct PointF { float x, y; PointF(float px, float py):x(px),y(py){} };
struct Rect { int x,y,w,h; Rect(int a,int b,int c,int d):x(a),y(b),w(c),h(d){} };
struct Pen { Color color; double width; Pen(Color c,double w):color(c),width(w){} };
class Bitmap {
    unsigned width_, height_;
    CGColorSpaceRef colors_{};
public:
    std::vector<BYTE> pixels;
    CGContextRef context{};
    Bitmap(unsigned width,unsigned height,int):width_(width),height_(height),pixels(width*height*4) {
        colors_=CGColorSpaceCreateDeviceRGB();
        context=CGBitmapContextCreate(pixels.data(),width,height,8,width*4,colors_,kCGImageAlphaPremultipliedLast);
        if (!context) throw std::runtime_error("Cannot allocate the Mac dashboard canvas.");
        CGContextTranslateCTM(context,0,height);CGContextScaleCTM(context,1,-1);
    }
    Bitmap(unsigned w,unsigned h,unsigned stride,int format,const BYTE *bgra):Bitmap(w,h,format) {
        for(unsigned y=0;y<h;++y) for(unsigned x=0;x<w;++x) {
            const auto *p=bgra+y*stride+x*4;auto *q=pixels.data()+(y*w+x)*4;
            q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];
        }
    }
    Bitmap(const Bitmap&)=delete;Bitmap& operator=(const Bitmap&)=delete;
    ~Bitmap(){if(context)CGContextRelease(context);if(colors_)CGColorSpaceRelease(colors_);}
    unsigned GetWidth() const{return width_;}unsigned GetHeight() const{return height_;}
    int GetLastStatus()const{return Ok;}
    CGImageRef image()const{return CGBitmapContextCreateImage(context);}
};
class Graphics {
    CGAffineTransform base_;
public:
    CGContextRef context;
    explicit Graphics(Bitmap *b):base_(CGContextGetCTM(b->context)),context(b->context){}
    void SetTextRenderingHint(int){}void SetPixelOffsetMode(int){}
    void SetInterpolationMode(int){CGContextSetInterpolationQuality(context,kCGInterpolationNone);}
    void TranslateTransform(double x,double y){CGContextTranslateCTM(context,x,y);}
    void ResetTransform(){CGContextConcatCTM(context,CGAffineTransformInvert(CGContextGetCTM(context)));CGContextConcatCTM(context,base_);}
    void DrawImage(Bitmap *bitmap,Rect r,int,int,unsigned,unsigned,int){
        auto source=bitmap->image();CGContextSaveGState(context);
        CGContextTranslateCTM(context,r.x,r.y+r.h);CGContextScaleCTM(context,1,-1);
        CGContextDrawImage(context,CGRectMake(0,0,r.w,r.h),source);CGContextRestoreGState(context);CGImageRelease(source);
    }
    void DrawLines(Pen *pen,const PointF *points,int count){
        const auto c=pen->color;CGContextSetRGBStrokeColor(context,c.r/255.0,c.g/255.0,c.b/255.0,1);
        CGContextSetLineWidth(context,pen->width);CGContextBeginPath(context);
        for(int i=0;i<count;++i){if(i)CGContextAddLineToPoint(context,points[i].x,points[i].y);else CGContextMoveToPoint(context,points[i].x,points[i].y);}
        CGContextStrokePath(context);
    }
};
}
MacGraphics::Color ink(Color c){return {255,static_cast<BYTE>(c.r*255),static_cast<BYTE>(c.g*255),static_cast<BYTE>(c.b*255)};}
void fill(MacGraphics::Graphics *g,double x,double y,double w,double h,Color c){
    CGContextSetRGBFillColor(g->context,c.r,c.g,c.b,1);CGContextFillRect(g->context,CGRectMake(x,y,w,h));
}
struct FontCache {
    std::map<double,CTFontRef> fonts;
    ~FontCache(){for(auto [size,font]:fonts){(void)size;CFRelease(font);}}
    CTFontRef get(double size){auto &font=fonts[size];if(!font)font=CTFontCreateWithName(CFSTR("Menlo"),size,nullptr);return font;}
} fonts;
void text(MacGraphics::Graphics *graphics,double x,double y,const std::string &value,double size=13,Color color=foreground){
    auto context=graphics->context;
    CFStringRef string=CFStringCreateWithBytes(nullptr,reinterpret_cast<const UInt8*>(value.data()),value.size(),kCFStringEncodingUTF8,false);
    if(!string)return;
    CGColorRef ink_color=CGColorCreateGenericRGB(color.r,color.g,color.b,1);
    const void *keys[]{kCTFontAttributeName,kCTForegroundColorAttributeName};const void *values[]{fonts.get(size),ink_color};
    auto attributes=CFDictionaryCreate(nullptr,keys,values,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    auto attributed=CFAttributedStringCreate(nullptr,string,attributes);auto line=CTLineCreateWithAttributedString(attributed);
    CGContextSaveGState(context);CGContextTranslateCTM(context,x,y+size);CGContextScaleCTM(context,1,-1);
    CGContextSetTextMatrix(context,CGAffineTransformIdentity);CGContextSetTextPosition(context,0,0);CTLineDraw(line,context);CGContextRestoreGState(context);
    CFRelease(line);CFRelease(attributed);CFRelease(attributes);CGColorRelease(ink_color);CFRelease(string);
}
template<typename... Args> std::string format(const char *pattern,Args... args){
    std::array<char,2048> buffer{};std::snprintf(buffer.data(),buffer.size(),pattern,args...);return buffer.data();
}
std::string clipped_title(const std::string &value,std::size_t limit){
    if(value.size()<=limit)return value;
    std::size_t end=limit-3;
    while(end && (static_cast<unsigned char>(value[end])&0xC0)==0x80)--end;
    return value.substr(0,end)+"...";
}
unsigned numeric_option(const std::string &text,unsigned maximum,const std::string &option,bool hexadecimal=false){
    std::size_t offset=0;unsigned base=10,value=0;
    if(hexadecimal && text.size()>2 && text[0]=='0' && (text[1]=='x'||text[1]=='X')){base=16;offset=2;}
    if(offset==text.size())throw std::runtime_error("Missing numeric value for "+option);
    for(;offset<text.size();++offset){
        const auto c=text[offset];unsigned digit=base;
        if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
        else if(base==16&&c>='a'&&c<='f')digit=10+static_cast<unsigned>(c-'a');
        else if(base==16&&c>='A'&&c<='F')digit=10+static_cast<unsigned>(c-'A');
        if(digit>=base || digit>maximum || value>(maximum-digit)/base)
            throw std::runtime_error("Invalid value for "+option+" (maximum "+std::to_string(maximum)+").");
        value=value*base+digit;
    }
    return value;
}
void save_png_image(CGImageRef bitmap,const std::filesystem::path &path){
    const auto name=path.string();
    auto url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(name.data()),name.size(),false);
    auto destination=CGImageDestinationCreateWithURL(url,CFSTR("public.png"),1,nullptr);
    if(!destination){CFRelease(url);throw std::runtime_error("Cannot create screenshot: "+name);}
    CGImageDestinationAddImage(destination,bitmap,nullptr);const bool ok=CGImageDestinationFinalize(destination);
    CFRelease(destination);CFRelease(url);if(!ok)throw std::runtime_error("Cannot write screenshot: "+name);
}
void save_png(MacGraphics::Bitmap &bitmap,const std::filesystem::path &path){auto image=bitmap.image();save_png_image(image,path);CGImageRelease(image);}
void wrapped_text(MacGraphics::Graphics *context, double x, double y, const char *value,
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
void draw_logo(MacGraphics::Graphics *context, double x, double y, double size) {
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
    MacGraphics::Color lcd_color(unsigned i) const {
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
    explicit Runtime(const std::filesystem::path &path) : title(path.filename().string()), library_view(false) {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (extension == ".gba") { gba = std::make_unique<GbaCore>(path); return; }
        if (extension != ".gb") throw std::runtime_error("Choose a Game Boy (.gb) or Game Boy Advance (.gba) ROM.");
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open ROM: " + path.string());
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
    std::unique_ptr<MacGraphics::Bitmap> render_library() {
        auto bitmap = std::make_unique<MacGraphics::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        MacGraphics::Graphics graphics(bitmap.get()); auto *context = &graphics;
        context->SetTextRenderingHint(MacGraphics::TextRenderingHintAntiAliasGridFit);
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
        text(context,32,870,"ARROWS select   ENTER play   CMD+O open a file   CMD+L library   F12 screenshot",13,muted);
        text(context,32,898,has_game() ? "Your current game is paused while you browse." : "Five GB games + five GBA games. Included and ready to play.",12,muted);
        return bitmap;
    }
    std::unique_ptr<MacGraphics::Bitmap> render_player(bool include_lcd = true) {
        auto bitmap = std::make_unique<MacGraphics::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        MacGraphics::Graphics graphics(bitmap.get());
        auto *context = &graphics;
        context->SetTextRenderingHint(MacGraphics::TextRenderingHintAntiAliasGridFit);
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
        const auto name = clipped_title(title,65);
        text(context, 48, 103, name, 17, foreground);
        if (include_lcd) {
            std::array<BYTE, 240*160*4> pixels{};
            for (unsigned i = 0; i < lcd_width()*lcd_height(); ++i) {
                const auto c = lcd_color(i);
                pixels[i*4] = c.GetB(); pixels[i*4+1] = c.GetG();
                pixels[i*4+2] = c.GetR(); pixels[i*4+3] = 255;
            }
            MacGraphics::Bitmap lcd(lcd_width(), lcd_height(), lcd_width()*4, PixelFormat32bppARGB, pixels.data());
            context->SetInterpolationMode(MacGraphics::InterpolationModeNearestNeighbor);
            context->SetPixelOffsetMode(MacGraphics::PixelOffsetModeHalf);
            context->DrawImage(&lcd, MacGraphics::Rect(static_cast<INT>(lcd_left()), static_cast<INT>(lcd_top()), 800, static_cast<INT>(display_height())), 0, 0, lcd_width(), lcd_height(), MacGraphics::UnitPixel);
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
        text(context, 900, 757, "Cmd+O  Choose another game", 14, muted);
        text(context, 900, 786, "Tab     Toggle Inspector", 14, muted);
        text(context, 900, 815, "F12     Save screenshot", 14, ::muted);
        text(context, 900, 844, "M       Toggle sound", 14, ::muted);
        }
        fill(context, 48, 872, 160, 36, {0.16, 0.34, 0.32});
        text(context, 75, 880, paused ? "Resume" : "Pause", 17, foreground);
        text(context, 232, 882, paused ? "Paused - press Space to resume" : "Playing", 15, paused ? orange : muted);
        return bitmap;
    }
    std::unique_ptr<MacGraphics::Bitmap> render_gba(bool include_lcd) {
        const auto state = gba->inspect(memory_bases[memory_region]+memory_offset);
        auto bitmap = std::make_unique<MacGraphics::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        MacGraphics::Graphics graphics(bitmap.get()); auto *context=&graphics;
        context->SetTextRenderingHint(MacGraphics::TextRenderingHintAntiAliasGridFit);
        context->SetInterpolationMode(MacGraphics::InterpolationModeNearestNeighbor);
        context->SetPixelOffsetMode(MacGraphics::PixelOffsetModeHalf);
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
                MacGraphics::Bitmap lcd(240,160,960,PixelFormat32bppARGB,pixels.data());
                context->DrawImage(&lcd,MacGraphics::Rect(48,232,640,427),0,0,240,160,MacGraphics::UnitPixel);
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
                MacGraphics::Pen pen(ink(side ? green : cyan),1);
                std::vector<MacGraphics::PointF> points;
                for (unsigned i=0;i<state.audio_frames;++i) points.emplace_back(static_cast<float>(202+i*1028.0/511),static_cast<float>(y+90-state.audio[i*2+side]*84.0/32768));
                if (points.size()>1) context->DrawLines(&pen,points.data(),static_cast<INT>(points.size()));
            }
            if (!state.audio_frames) text(context,220,368,"Play the game to collect output samples.",16,muted);
            text(context,48,768,format("SOUNDCNT L %04X   H %04X   X %04X   BIAS %04X",state.sound_low,state.sound_high,state.sound_enable,state.sound_bias),17);
        }
        text(context,48,888,"TAB game   1-4 panels   SPACE pause   S instruction   F frame   CMD+O open   F12 capture",12,muted);
        return bitmap;
    }
    std::unique_ptr<MacGraphics::Bitmap> render(bool include_lcd = true) {
        if (library_view) return render_library();
        if (!inspector_view) return render_player(include_lcd);
        if (gba) return render_gba(include_lcd);
        inspector->capture(*cpu, *bus);
        const auto state = inspector->snapshot();
        auto rendered = std::make_unique<MacGraphics::Bitmap>(canvas_width, canvas_height, PixelFormat32bppARGB);
        if (rendered->GetLastStatus() != MacGraphics::Ok) throw std::runtime_error("cannot allocate HUD canvas");
        MacGraphics::Graphics graphics(rendered.get());
        auto *context = &graphics;
        context->SetInterpolationMode(MacGraphics::InterpolationModeNearestNeighbor);
        context->SetPixelOffsetMode(MacGraphics::PixelOffsetModeHalf);
        context->SetTextRenderingHint(MacGraphics::TextRenderingHintAntiAliasGridFit);
        fill(context, 0, 0, canvas_width, canvas_height, {0.035, 0.06, 0.08});
        fill(context, 0, 0, canvas_width, 66, {0.065, 0.105, 0.13});
        draw_logo(context, 24, 12, 40);
        text(context, 78, 16, "MATCHABOY", 23, cyan);
        text(context, 305, 22, clipped_title(title,55), 13);
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
            MacGraphics::Bitmap lcd(160, 144, 160*4, PixelFormat32bppARGB, pixels.data());
            context->DrawImage(&lcd, MacGraphics::Rect(48, 232, 640, 576), 0, 0, 160, 144, MacGraphics::UnitPixel);
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
        MacGraphics::Bitmap heat_image(256, 256, 1024, PixelFormat32bppARGB, heat_bgra.data());
        context->DrawImage(&heat_image, MacGraphics::Rect(24, 130, 512, 512), 0, 0, 256, 256, MacGraphics::UnitPixel);
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
            MacGraphics::Pen pen(ink(wave_colors[channel]), 1);
            std::vector<MacGraphics::PointF> points;
            for (unsigned sample = 0; sample < state.wave_count; ++sample) {
                const double x = 221 + sample * (1010.0 / (dmg::AutopsyFrame::wave_length - 1));
                const double level_y = y + 96 - state.waveforms[channel][sample] * 88.0 / 15;
                points.emplace_back(static_cast<float>(x), static_cast<float>(level_y));
            }
            if (points.size() > 1) context->DrawLines(&pen, points.data(), static_cast<INT>(points.size()));
        }
        }
        text(context, 48, 888, "TAB game   1-4 panels   CMD+O open   SPACE pause   S instruction   F frame   D dot   F12 capture", 12, muted);
        return rendered;
    }
    void save(const std::filesystem::path &path, MacGraphics::Bitmap &bitmap, bool gpu) {
        save_png(bitmap,path);
        save_metadata(path,bitmap.GetWidth(),bitmap.GetHeight(),gpu);
    }
    void save_metadata(const std::filesystem::path &path,unsigned width,unsigned height,bool gpu) {
        std::ofstream out(path.string()+".json");
        out << "{\"platform\":\"" << (library_view ? "library" : gba ? "gba" : "gb") << "\",\"width\":" << width
            << ",\"height\":" << height << ",\"gpu_readback\":" << (gpu ? "true" : "false")
            << ",\"library_view\":" << (library_view ? "true" : "false") << ",\"library_selection\":" << library_selection
            << ",\"library_game\":" << library_game << ",\"inspector_view\":" << (inspector_view ? "true" : "false")
            << ",\"inspector_tab\":" << inspector_tab << ",\"paused\":" << (paused ? "true" : "false") << ",\"buttons\":" << buttons;
        if (gba) {
            const auto debug=gba->inspect(memory_bases[memory_region]+memory_offset);
            out << ",\"frames\":" << gba->frames() << ",\"pc\":" << debug.registers[15] << ",\"cpsr\":" << debug.cpsr
                << ",\"memory_base\":" << debug.memory_base << ",\"dispcnt\":" << debug.dispcnt << ",\"audio_frames\":" << debug.audio_frames;
        } else if(bus) {
            inspector->capture(*cpu,*bus);const auto state=inspector->snapshot();
            out << ",\"frames\":" << state.frames << ",\"cycles\":" << state.cycles << ",\"pc\":" << state.pc
                << ",\"ly\":" << static_cast<unsigned>(state.ly) << ",\"dot\":" << state.dot << ",\"mode\":" << state.mode
                << ",\"fifo_depth\":" << state.fifo_depth << ",\"instructions\":" << state.instructions << ",\"trace_scroll\":" << trace_scroll;
        } else out << ",\"frames\":0";
        if(has_game() && !library_view && (!inspector_view || inspector_tab==0)) {
            const double sx=width/static_cast<double>(canvas_width),sy=height/static_cast<double>(canvas_height);
            out << ",\"lcd_rect\":[" << (inspector_view ? 48 : lcd_left())*sx << ',' << (inspector_view ? 232 : lcd_top())*sy
                << ',' << (inspector_view ? 640 : 800)*sx << ',' << (inspector_view ? (gba ? 427 : 576) : display_height())*sy << ']';
            out << ",\"lcd_native_width\":" << lcd_width() << ",\"lcd_native_height\":" << lcd_height();
            std::ofstream lcd(path.string()+".lcd.ppm",std::ios::binary);lcd << "P6\n" << lcd_width() << ' ' << lcd_height() << "\n255\n";
            for(unsigned i=0;i<lcd_width()*lcd_height();++i){const auto c=lcd_color(i);const char rgb[]{static_cast<char>(c.GetR()),static_cast<char>(c.GetG()),static_cast<char>(c.GetB())};lcd.write(rgb,3);}
            if(!lcd)throw std::runtime_error("Cannot write LCD reference image.");
        }
        out << "}\n";if(!out)throw std::runtime_error("Cannot write capture metadata.");
        std::cout << "Captured " << path.string() << '\n';
    }
};
Runtime *active_runtime{};
class Presentation {
public:
    Runtime &runtime;
    MacAudio audio;
    bool audio_suspended=true, dirty=true, controls_dirty=true, window_test=false, failed=false;
    std::filesystem::path capture;
    bool capture_requested=false;
    std::uint64_t generated_frames=0;
    unsigned generated_peak=0;
    std::chrono::steady_clock::time_point next_frame=std::chrono::steady_clock::now(), refresh{};
    std::string renderer;
    explicit Presentation(Runtime &r):runtime(r){}
    void sync_audio(){
        const bool suspended=runtime.library_view || !runtime.has_game() || runtime.paused || runtime.sound_muted;
        if(suspended==audio_suspended)return;
        audio.reset();std::array<std::int16_t,8192> discarded{};while(runtime.drain_audio(discarded)){}
        audio_suspended=suspended;
    }
    void pump_audio(){
        std::array<std::int16_t,8192> samples{};
        while(const auto count=runtime.drain_audio(samples)){
            generated_frames+=count/2;
            for(std::size_t i=0;i<count;++i)generated_peak=std::max(generated_peak,static_cast<unsigned>(std::abs(static_cast<int>(samples[i]))));
            if(!audio_suspended)audio.submit(std::span(samples).first(count));
        }
    }
    void changed(){dirty=true;controls_dirty=true;sync_audio();}
    void load_game(const std::filesystem::path &path,int game=-1){
        runtime.flush_save();
        Runtime replacement(path); // Keep the old game alive if validation/open fails.
        replacement.sound_muted=runtime.sound_muted;replacement.green_palette=runtime.green_palette;
        replacement.controls_visible=runtime.controls_visible;replacement.library_selection=runtime.library_selection;
        replacement.library_game=game;if(game>=0)replacement.title=arcade_games()[game].title;
        replacement.run_frame();runtime=std::move(replacement);audio.reset();audio_suspended=true;
        if(!window_test)runtime.enable_audio();next_frame=std::chrono::steady_clock::now()+frame_period;changed();
    }
    void library(){
        if(runtime.library_view)return;
        runtime.library_was_paused=runtime.paused;runtime.library_view=true;runtime.paused=true;runtime.set_buttons(0);changed();
    }
    void return_game(){
        if(!runtime.library_view || !runtime.has_game())return;
        runtime.library_view=false;runtime.paused=runtime.library_was_paused;runtime.set_buttons(0);
        next_frame=std::chrono::steady_clock::now()+frame_period;changed();
    }
    void audio_report(const std::filesystem::path &path){
        const auto s=audio.stats();std::ofstream out(path.string()+".audio.json");
        out << "{\"device_open\":" << (s.available?"true":"false") << ",\"muted\":" << (runtime.sound_muted?"true":"false")
            << ",\"paused\":" << (runtime.paused?"true":"false") << ",\"queued_buffers\":" << s.queued_buffers
            << ",\"submitted_frames\":" << s.submitted_frames << ",\"completed_frames\":" << s.completed_frames
            << ",\"peak\":" << s.peak << ",\"sample_rate\":48000,\"generated_frames\":" << generated_frames
            << ",\"generated_peak\":" << generated_peak << ",\"underruns\":" << s.underruns
            << ",\"dropped_frames\":" << s.dropped_frames << ",\"max_gap_ms\":" << s.max_gap_ms << ",\"device_error\":" << s.error << "}\n";
    }
};
Presentation *presentation{};
NSString *native(const std::string &s){return [NSString stringWithUTF8String:s.c_str()];}
void show_error(const std::string &message,const std::string &title="Unable to open game"){
    NSAlert *alert=[[NSAlert alloc]init];[alert setMessageText:native(title)];
    [alert setInformativeText:native(message)];[alert addButtonWithTitle:@"OK"];[alert runModal];[alert release];
}
unsigned joypad_bit(unsigned key){
    switch(key){case 124:return 0;case 123:return 1;case 126:return 2;case 125:return 3;case 6:return 4;case 7:return 5;
        case 56:case 60:return 6;case 36:case 76:return 7;case 12:return 8;case 13:return 9;default:return 10;}
}
}

// Real AppKit controls expose semantic actions to VoiceOver. Draw only their
// transient interaction affordance; the shared canvas supplies their normal look.
@interface MatchaButton : NSButton {
    NSTrackingArea *tracking_;
    BOOL hover_,pressed_;
}
@end
@implementation MatchaButton
- (BOOL)isOpaque{return NO;}
- (void)updateTrackingAreas{
    [super updateTrackingAreas];
    if(tracking_){[self removeTrackingArea:tracking_];[tracking_ release];}
    tracking_=[[NSTrackingArea alloc]initWithRect:NSZeroRect options:NSTrackingMouseEnteredAndExited|NSTrackingActiveInKeyWindow|NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:tracking_];
}
- (void)dealloc{if(tracking_){[self removeTrackingArea:tracking_];[tracking_ release];}[super dealloc];}
- (void)mouseEntered:(NSEvent *)event{(void)event;hover_=YES;[self setNeedsDisplay:YES];}
- (void)mouseExited:(NSEvent *)event{(void)event;hover_=NO;[self setNeedsDisplay:YES];}
- (BOOL)becomeFirstResponder{const BOOL accepted=[super becomeFirstResponder];[self setNeedsDisplay:YES];return accepted;}
- (BOOL)resignFirstResponder{const BOOL accepted=[super resignFirstResponder];[self setNeedsDisplay:YES];return accepted;}
- (void)mouseDown:(NSEvent *)event{
    pressed_=YES;[self setNeedsDisplay:YES];[self displayIfNeeded];
    [super mouseDown:event];pressed_=NO;[self setNeedsDisplay:YES];
}
- (void)drawRect:(NSRect)dirty{
    (void)dirty;const BOOL focused=[[self window]firstResponder]==self;
    if(!hover_&&!pressed_&&!focused)return;
    const double opacity=pressed_?1.0:focused?0.95:0.55;
    [[NSColor colorWithCalibratedRed:cyan.r green:cyan.g blue:cyan.b alpha:opacity]setStroke];
    NSBezierPath *outline=[NSBezierPath bezierPathWithRect:NSInsetRect([self bounds],1.5,1.5)];
    [outline setLineWidth:pressed_||focused?3:2];[outline stroke];
}
@end

@interface AutopsyView : NSOpenGLView <NSMenuItemValidation> {
    GLuint texture_, lcd_texture_;
    unsigned lcd_width_, lcd_height_;
    NSMutableArray<NSButton *> *controls_;
}
- (void)advance:(NSTimer *)timer;
- (void)performCommand:(id)sender;
- (void)rebuildControls;
- (void)focusLost;
- (void)openGame;
- (void)openPath:(NSString *)path;
@end
@implementation AutopsyView
- (BOOL)acceptsFirstResponder{return YES;}
- (void)prepareOpenGL{
    [super prepareOpenGL];GLint swap=1;[[self openGLContext]setValues:&swap forParameter:NSOpenGLCPSwapInterval];
    glGenTextures(1,&texture_);glGenTextures(1,&lcd_texture_);
    glBindTexture(GL_TEXTURE_2D,lcd_texture_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    const auto *renderer=glGetString(GL_RENDERER);if(renderer)presentation->renderer=reinterpret_cast<const char *>(renderer);
    [self setAccessibilityLabel:@"Matchaboy game display"];
    [self setAccessibilityHelp:@"Use the native Game and View menus for all controls. Arrow keys move, Z is A, X is B, Return is Start."];
    [self registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
}
- (void)dealloc{
    [[self openGLContext]makeCurrentContext];if(texture_)glDeleteTextures(1,&texture_);if(lcd_texture_)glDeleteTextures(1,&lcd_texture_);
    [controls_ release];[super dealloc];
}
- (void)setFrameSize:(NSSize)size{[super setFrameSize:size];if(presentation){presentation->controls_dirty=true;[self setNeedsDisplay:YES];}}
- (void)drawRect:(NSRect)dirtyRect{
    (void)dirtyRect;
    try{
        auto &host=*presentation;auto &r=host.runtime;[[self openGLContext]makeCurrentContext];
        const auto now=std::chrono::steady_clock::now();
        if(host.dirty || host.capture_requested || host.window_test || (r.inspector_view && !r.library_view && now>=host.refresh)){
            auto bitmap=r.render(false);glBindTexture(GL_TEXTURE_2D,texture_);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,canvas_width,canvas_height,0,GL_RGBA,GL_UNSIGNED_BYTE,bitmap->pixels.data());
            host.dirty=false;host.refresh=now+std::chrono::milliseconds(67);
        }
        const auto backing=[self convertRectToBacking:[self bounds]];
        const double scale=std::min(backing.size.width/canvas_width,backing.size.height/canvas_height);
        const int vw=static_cast<int>(canvas_width*scale),vh=static_cast<int>(canvas_height*scale);
        const int vx=static_cast<int>((backing.size.width-vw)/2),vy=static_cast<int>((backing.size.height-vh)/2);
        glViewport(vx,vy,vw,vh);glClearColor(0.035F,0.06F,0.08F,1);glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);glLoadIdentity();glOrtho(0,1,0,1,-1,1);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
        glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,texture_);glColor3f(1,1,1);glBegin(GL_QUADS);
        glTexCoord2f(0,1);glVertex2f(0,0);glTexCoord2f(1,1);glVertex2f(1,0);glTexCoord2f(1,0);glVertex2f(1,1);glTexCoord2f(0,0);glVertex2f(0,1);glEnd();
        if(!r.library_view && r.has_game() && (!r.inspector_view || r.inspector_tab==0)){
            std::array<BYTE,240*160*4> pixels{};
            for(unsigned i=0;i<r.lcd_width()*r.lcd_height();++i){const auto c=r.lcd_color(i);pixels[i*4]=c.GetR();pixels[i*4+1]=c.GetG();pixels[i*4+2]=c.GetB();pixels[i*4+3]=255;}
            glBindTexture(GL_TEXTURE_2D,lcd_texture_);
            if(lcd_width_!=r.lcd_width() || lcd_height_!=r.lcd_height()){
                lcd_width_=r.lcd_width();lcd_height_=r.lcd_height();glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,lcd_width_,lcd_height_,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            }
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,lcd_width_,lcd_height_,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            const double x=r.inspector_view?48:r.lcd_left(),y=r.inspector_view?232:r.lcd_top();
            const double w=r.inspector_view?640:800,h=r.inspector_view?(r.gba?427:576):r.display_height();
            const float left=static_cast<float>(x/canvas_width),right=static_cast<float>((x+w)/canvas_width);
            const float top=static_cast<float>(1-y/canvas_height),bottom=static_cast<float>(1-(y+h)/canvas_height);
            glBegin(GL_QUADS);glTexCoord2f(0,1);glVertex2f(left,bottom);glTexCoord2f(1,1);glVertex2f(right,bottom);
            glTexCoord2f(1,0);glVertex2f(right,top);glTexCoord2f(0,0);glVertex2f(left,top);glEnd();
        }
        glDisable(GL_TEXTURE_2D);
        if(host.capture_requested || host.window_test){
            try {
            if(host.capture.empty()){
                auto directory=[[NSFileManager defaultManager]URLsForDirectory:NSPicturesDirectory inDomains:NSUserDomainMask].firstObject;
                const auto stamp=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                host.capture=std::filesystem::path([[directory path]UTF8String])/format("Matchaboy-%lld.png",static_cast<long long>(stamp));
            }
            std::vector<BYTE> pixels(static_cast<std::size_t>(vw)*vh*4),bgra(pixels.size());
            glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);glReadPixels(vx,vy,vw,vh,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("OpenGL back-buffer readback failed.");
            for(int y=0;y<vh;++y)for(int x=0;x<vw;++x){const auto s=(static_cast<std::size_t>(vh-1-y)*vw+x)*4,d=(static_cast<std::size_t>(y)*vw+x)*4;
                bgra[d]=pixels[s+2];bgra[d+1]=pixels[s+1];bgra[d+2]=pixels[s];bgra[d+3]=255;}
            MacGraphics::Bitmap bitmap(vw,vh,vw*4,PixelFormat32bppARGB,bgra.data());r.save(host.capture,bitmap,true);host.audio_report(host.capture);
            host.capture_requested=false;
            if(!host.window_test){[[NSWorkspace sharedWorkspace]activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:native(host.capture.string())]]];host.capture.clear();}
            } catch(const std::exception &error) {
                host.capture_requested=false;host.capture.clear();
                if(host.window_test)throw;
                r.paused=true;host.changed();show_error(error.what(),"Unable to save screenshot");
            }
        }
        [[self openGLContext]flushBuffer];
        if(host.controls_dirty)[self rebuildControls];
        if(host.window_test)[NSApp terminate:nil];
    }catch(const std::exception &e){presentation->failed=true;std::cerr<<"Matchaboy: "<<e.what()<<'\n';[NSApp stop:nil];}
}
- (void)advance:(NSTimer *)timer{
    (void)timer;auto &host=*presentation;auto &r=host.runtime;
    try{
        if(!r.library_view && r.has_game() && !r.paused){
            auto now=std::chrono::steady_clock::now();unsigned ran=0;
            while(now>=host.next_frame && ran<2){
                r.run_frame();host.pump_audio();++ran;
                host.next_frame+=frame_period+std::chrono::microseconds(host.audio.pacing_adjustment_us());
                now=std::chrono::steady_clock::now();
            }
            if(now-host.next_frame>frame_period*3)host.next_frame=now+frame_period;
            if(ran)[self setNeedsDisplay:YES];
        }else host.next_frame=std::chrono::steady_clock::now()+frame_period;
        if(host.dirty)[self setNeedsDisplay:YES];
    }catch(const std::exception &e){r.paused=true;host.changed();show_error(e.what());}
}
- (void)focusLost{
    auto &r=presentation->runtime;r.set_buttons(0);
    if(r.has_game() && !r.library_view)r.paused=true;
    presentation->changed();[self setNeedsDisplay:YES];
}
- (void)openPath:(NSString *)path{
    const bool paused=presentation->runtime.paused;
    try{presentation->load_game([path fileSystemRepresentation]);}
    catch(const std::exception &e){presentation->runtime.paused=paused;show_error(e.what());}
    presentation->changed();
    [[self window]setTitle:native(presentation->runtime.library_view?"Matchaboy — Original Arcade":"Matchaboy — "+presentation->runtime.title)];
    [self setNeedsDisplay:YES];
}
- (void)openGame{
    auto &host=*presentation;const bool paused=host.runtime.paused;
    host.runtime.paused=true;host.runtime.set_buttons(0);host.sync_audio();
    NSOpenPanel *panel=[NSOpenPanel openPanel];[panel setTitle:@"Open a game in Matchaboy"];
    [panel setCanChooseDirectories:NO];[panel setAllowsMultipleSelection:NO];[panel setAllowedFileTypes:@[@"gb",@"gba"]];
    const auto response=[panel runModal];
    host.runtime.paused=paused;
    if(response==NSModalResponseOK)[self openPath:[[panel URL]path]];
    host.next_frame=std::chrono::steady_clock::now()+frame_period;host.changed();[self setNeedsDisplay:YES];
}
- (void)performCommand:(id)sender{
    auto &host=*presentation;auto &r=host.runtime;const NSInteger command=[sender tag];
    try{
        if(command>=200 && command<210){r.library_selection=static_cast<unsigned>(command-200);}
        else if(command>=300 && command<304 && r.has_game() && !r.library_view){r.inspector_view=true;r.inspector_tab=static_cast<unsigned>(command-300);}
        else if(command>=400 && command<406 && r.gba){r.memory_region=static_cast<unsigned>(command-400);r.memory_offset=0;}
        else switch(command){
            case 1:[self openGame];break;
            case 2:if(r.library_view)host.return_game();else host.library();break;
            case 3:host.return_game();break;
            case 4:if(r.library_selection<arcade_games().size())host.load_game(arcade_rom_path(r.library_selection),static_cast<int>(r.library_selection));break;
            case 5:r.toggle_inspector();break;
            case 6:r.controls_visible=!r.controls_visible;break;
            case 7:if(r.has_game()&&!r.library_view)r.paused=!r.paused;break;
            case 8:
                if(!host.audio.available()){
                    if(host.audio.open()){r.sound_muted=false;r.enable_audio();}
                    else{r.sound_muted=true;show_error("No output device is available. Connect or select an audio output in System Settings, then press M to retry.","Audio output unavailable");}
                }else r.sound_muted=!r.sound_muted;
                break;
            case 9:r.green_palette=!r.green_palette;break;
            case 10:host.capture_requested=true;break;
            case 11:if(r.has_game()&&!r.library_view){r.paused=true;r.step();}break;
            case 12:if(r.has_game()&&!r.library_view){r.paused=true;r.run_frame();}break;
            case 13:if(r.bus&&!r.library_view){r.paused=true;r.bus->tick(1);}break;
            default:break;
        }
    }catch(const std::exception &e){show_error(e.what());}
    host.changed();host.pump_audio();
    [[self window]setTitle:native(r.library_view?"Matchaboy — Original Arcade":"Matchaboy — "+r.title)];
    [[self window]makeFirstResponder:self];[self setNeedsDisplay:YES];
}
- (BOOL)validateMenuItem:(NSMenuItem *)item{
    const auto &r=presentation->runtime;const NSInteger tag=[item tag];
    if(tag==6)[item setState:r.controls_visible?NSControlStateValueOn:NSControlStateValueOff];
    if(tag==8){
        [item setTitle:presentation->audio.available()?@"Sound":@"Sound — No Output Device (Retry)"];
        [item setState:presentation->audio.available()&&!r.sound_muted?NSControlStateValueOn:NSControlStateValueOff];
    }
    if(tag==9)[item setState:r.green_palette?NSControlStateValueOn:NSControlStateValueOff];
    if(tag==7)[item setTitle:r.paused?@"Resume":@"Pause"];
    if(tag>=300&&tag<304)[item setState:r.inspector_view&&r.inspector_tab==static_cast<unsigned>(tag-300)?NSControlStateValueOn:NSControlStateValueOff];
    if(tag==3)return r.has_game()&&r.library_view;
    if(tag==4)return r.library_view;
    if(tag==5||tag==7||tag==11||tag==12||(tag>=300&&tag<304))return r.has_game()&&!r.library_view;
    if(tag==13)return r.bus&&!r.library_view;
    if(tag==9)return !r.gba;
    return YES;
}
- (void)addControl:(NSString *)label tag:(NSInteger)tag x:(double)x y:(double)y width:(double)width height:(double)height{
    const auto bounds=[self bounds];const double scale=std::min(bounds.size.width/canvas_width,bounds.size.height/canvas_height);
    const double ox=(bounds.size.width-canvas_width*scale)/2,oy=(bounds.size.height-canvas_height*scale)/2;
    NSButton *button=[[MatchaButton alloc]initWithFrame:NSMakeRect(ox+x*scale,oy+(canvas_height-y-height)*scale,width*scale,height*scale)];
    [button setTitle:label];[button setAccessibilityLabel:label];[button setTransparent:NO];[button setBordered:NO];[button setFocusRingType:NSFocusRingTypeNone];
    [button setTag:tag];[button setTarget:self];[button setAction:@selector(performCommand:)];[button setToolTip:label];
    [self addSubview:button];[controls_ addObject:button];[button release];
}
- (void)rebuildControls{
    if(!controls_)controls_=[[NSMutableArray alloc]init];
    for(NSButton *button in controls_)[button removeFromSuperview];[controls_ removeAllObjects];
    auto &r=presentation->runtime;
    if(r.library_view){
        [self addControl:@"Open your game…" tag:1 x:1040 y:20 width:208 height:48];
        if(r.has_game())[self addControl:@"Return to game" tag:3 x:834 y:20 width:190 height:48];
        for(unsigned i=0;i<arcade_games().size();++i){
            const std::string label=std::string(arcade_games()[i].title)+", "+arcade_games()[i].system+(i==r.library_selection?", selected":"");
            [self addControl:native(label) tag:200+i x:i<5?32:426 y:230+(i%5)*122 width:370 height:110];
        }
        [self addControl:@"Play selected game" tag:4 x:844 y:334 width:380 height:46];
    }else if(r.inspector_view){
        constexpr std::array<const char *,4> names{"Video","CPU","Memory","Audio"};
        for(unsigned i=0;i<4;++i)[self addControl:native(names[i]) tag:300+i x:48+i*230 y:92 width:218 height:48];
        [self addControl:@"Back to game" tag:5 x:1016 y:92 width:216 height:48];
        if(r.gba&&r.inspector_tab==2){
            constexpr std::array<const char *,6> names{"EWRAM","IWRAM","VRAM","Palette","OAM","ROM"};
            for(unsigned i=0;i<6;++i)[self addControl:native(names[i]) tag:400+i x:48+i*197 y:156 width:185 height:40];
        }
    }else{
        [self addControl:@"Library" tag:2 x:578 y:20 width:116 height:48];
        [self addControl:r.controls_visible?@"Hide controls":@"Show controls" tag:6 x:710 y:20 width:174 height:48];
        [self addControl:@"Open game…" tag:1 x:900 y:20 width:158 height:48];
        [self addControl:@"Inspector" tag:5 x:1076 y:20 width:164 height:48];
        [self addControl:r.paused?@"Resume":@"Pause" tag:7 x:48 y:872 width:160 height:36];
    }
    presentation->controls_dirty=false;
}
- (void)keyDown:(NSEvent *)event{
    auto &r=presentation->runtime;const auto key=[event keyCode];const auto character=[[event charactersIgnoringModifiers]lowercaseString];
    if([event modifierFlags]&NSEventModifierFlagCommand){[super keyDown:event];return;}
    if(![event isARepeat]){
        NSInteger command=0;
        if(key==111)command=10;
        else if([character isEqualToString:@"m"])command=8;
        else if(r.library_view){
            if(key==36||key==76)command=4;
            else if(key==53)command=3;
            else if(key>=123&&key<=126){
                unsigned selection=r.library_selection;
                if(key==126&&selection%5>0)--selection;else if(key==125&&selection%5<4)++selection;
                else if(key==123&&selection>=5)selection-=5;else if(key==124&&selection<5)selection+=5;
                if(selection<arcade_games().size())r.library_selection=selection;
                presentation->changed();[self setNeedsDisplay:YES];return;
            }
        }else{
            if(key==48)command=5;
            else if(key==49)command=7;
            else if([character isEqualToString:@"c"])command=6;
            else if([character isEqualToString:@"p"])command=9;
            else if(r.inspector_view){
                if([character isEqualToString:@"s"])command=11;else if([character isEqualToString:@"f"])command=12;
                else if([character isEqualToString:@"d"]&&!r.gba)command=13;
                else if([character length]==1&&[character characterAtIndex:0]>='1'&&[character characterAtIndex:0]<='4')command=300+[character characterAtIndex:0]-'1';
                else if(r.gba&&r.inspector_tab==2&&(key==116||key==121)){r.memory_page(key==121?1:-1);presentation->dirty=true;[self setNeedsDisplay:YES];return;}
            }
        }
        if(command){NSMenuItem *item=[[[NSMenuItem alloc]init]autorelease];[item setTag:command];[self performCommand:item];return;}
    }
    if(r.library_view)return;
    const unsigned bit=joypad_bit(key);if(bit<(r.gba?10U:8U))r.set_buttons(r.buttons|static_cast<std::uint16_t>(1U<<bit));
}
- (void)keyUp:(NSEvent *)event{
    auto &r=presentation->runtime;const unsigned bit=joypad_bit([event keyCode]);
    if(bit<10)r.set_buttons(r.buttons&static_cast<std::uint16_t>(~(1U<<bit)));
}
- (void)flagsChanged:(NSEvent *)event{
    auto &r=presentation->runtime;if(r.library_view)return;
    r.set_buttons(([event modifierFlags]&NSEventModifierFlagShift)?r.buttons|0x40:r.buttons&0xFFBF);
}
- (void)scrollWheel:(NSEvent *)event{
    auto &r=presentation->runtime;if(!r.inspector_view||r.library_view)return;
    if(r.gba&&r.inspector_tab==2)r.memory_page([event scrollingDeltaY]>0?-1:1);
    else r.trace_scroll=static_cast<unsigned>(std::clamp(static_cast<int>(r.trace_scroll)+static_cast<int>([event scrollingDeltaY]),0,107));
    presentation->dirty=true;[self setNeedsDisplay:YES];
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender{
    return [[[sender draggingPasteboard]readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}]count]?NSDragOperationCopy:NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender{
    NSArray<NSURL *> *urls=[[sender draggingPasteboard]readObjectsForClasses:@[[NSURL class]] options:@{NSPasteboardURLReadingFileURLsOnlyKey:@YES}];
    if(![urls count])return NO;[self openPath:[urls[0]path]];return YES;
}
@end

@interface AutopsyDelegate:NSObject<NSApplicationDelegate,NSWindowDelegate>
@property(assign) AutopsyView *view;
@end
@implementation AutopsyDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender{(void)sender;return YES;}
- (void)applicationDidResignActive:(NSNotification *)notification{(void)notification;[self.view focusLost];}
- (void)windowDidResignKey:(NSNotification *)notification{(void)notification;[self.view focusLost];}
- (BOOL)application:(NSApplication *)app openFile:(NSString *)filename{(void)app;[self.view openPath:filename];return YES;}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender{
    (void)sender;
    try{presentation->runtime.flush_save();return NSTerminateNow;}
    catch(const std::exception &e){
        if(presentation->window_test){std::cerr<<e.what()<<'\n';presentation->failed=true;return NSTerminateNow;}
        NSAlert *alert=[[[NSAlert alloc]init]autorelease];[alert setMessageText:@"Unable to save progress"];
        [alert setInformativeText:native(e.what())];[alert addButtonWithTitle:@"Keep playing"];[alert addButtonWithTitle:@"Quit without saving"];
        return [alert runModal]==NSAlertSecondButtonReturn?NSTerminateNow:NSTerminateCancel;
    }
}
@end

namespace {
void menu_command(NSMenu *menu,NSString *title,NSString *key,NSInteger tag,AutopsyView *view,NSEventModifierFlags modifiers=NSEventModifierFlagCommand){
    NSMenuItem *item=[[[NSMenuItem alloc]initWithTitle:title action:@selector(performCommand:) keyEquivalent:key]autorelease];
    [item setTag:tag];[item setTarget:view];[item setKeyEquivalentModifierMask:modifiers];[menu addItem:item];
}
void create_menus(AutopsyView *view){
    NSMenu *bar=[[[NSMenu alloc]init]autorelease];
    auto add=[&](NSString *title){NSMenuItem *item=[[[NSMenuItem alloc]initWithTitle:title action:nil keyEquivalent:@""]autorelease];
        NSMenu *submenu=[[[NSMenu alloc]initWithTitle:title]autorelease];[item setSubmenu:submenu];[bar addItem:item];return submenu;};
    auto app=add(@"Matchaboy");
    [app addItemWithTitle:@"About Matchaboy" action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
    [app addItem:[NSMenuItem separatorItem]];[app addItemWithTitle:@"Hide Matchaboy" action:@selector(hide:) keyEquivalent:@"h"];
    [app addItemWithTitle:@"Quit Matchaboy" action:@selector(terminate:) keyEquivalent:@"q"];
    auto game=add(@"Game");menu_command(game,@"Open Game…",@"o",1,view);menu_command(game,@"Library",@"l",2,view);
    menu_command(game,@"Return to Game",@"",3,view);menu_command(game,@"Play Selected Game",@"",4,view);
    [game addItem:[NSMenuItem separatorItem]];menu_command(game,@"Pause",@" ",7,view,0);
    menu_command(game,@"Sound",@"m",8,view,0);menu_command(game,@"Save Screenshot",@"s",10,view,NSEventModifierFlagCommand|NSEventModifierFlagShift);
    auto screen=add(@"View");menu_command(screen,@"Inspector",@"\t",5,view,0);menu_command(screen,@"Show Controls",@"c",6,view,0);
    menu_command(screen,@"Game Boy Green Palette",@"p",9,view,0);[screen addItem:[NSMenuItem separatorItem]];
    const std::array<NSString *,4> titles{@"Video",@"CPU",@"Memory",@"Audio"};
    for(unsigned i=0;i<4;++i)menu_command(screen,titles[i],[NSString stringWithFormat:@"%u",i+1],300+i,view,NSEventModifierFlagCommand);
    auto debug=add(@"Debug");menu_command(debug,@"Step Instruction",@"",11,view);menu_command(debug,@"Advance Frame",@"",12,view);menu_command(debug,@"Advance Dot",@"",13,view);
    auto window=add(@"Window");[window addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
    [window addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
    [window addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];[NSApp setWindowsMenu:window];[NSApp setMainMenu:bar];
}
}

int main(int argc,char **argv){
    @autoreleasepool{
        try{
            std::string rom,capture,game_id;unsigned frames=120,line=0,dot=0,tab=0,buttons=0,input_frames=0,memory_region=0;
            bool headless=false,window_test=false,paused=false,position=false,inspector=false,library=false,green=false,hide_controls=false;
            for(int i=1;i<argc;++i){
                const std::string option=argv[i];
                if(option=="--headless")headless=true;else if(option=="--window-test")window_test=true;
                else if(option=="--paused")paused=true;else if(option=="--inspector")inspector=true;
                else if(option=="--library")library=true;else if(option=="--green")green=true;else if(option=="--hide-controls")hide_controls=true;
                else if(option=="--help"){
                    std::cout<<"Matchaboy [ROM.gb|ROM.gba] [--game ID] [--library] [--inspector --tab 0..3]\n"
                        "  --frames N --buttons MASK --input-frames N --green --hide-controls --paused\n"
                        "  --headless|--window-test --capture FILE.png [--line LY --dot DOT]\n";return 0;
                }else if(option.rfind("-psn_",0)==0)continue;
                else if(option.rfind("--",0)==0){
                    if(i+1==argc)throw std::runtime_error("Missing value for "+option);const std::string value=argv[++i];
                    if(option=="--capture")capture=value;else if(option=="--game")game_id=value;
                    else if(option=="--frames")frames=numeric_option(value,1000000,option);
                    else if(option=="--input-frames")input_frames=numeric_option(value,1000000,option);
                    else if(option=="--buttons")buttons=numeric_option(value,1023,option,true);
                    else if(option=="--tab")tab=numeric_option(value,3,option);
                    else if(option=="--memory-region")memory_region=numeric_option(value,5,option);
                    else if(option=="--line"){line=numeric_option(value,153,option);position=true;}
                    else if(option=="--dot"){dot=numeric_option(value,455,option);position=true;}
                    else throw std::runtime_error("Unknown option: "+option);
                }else if(rom.empty())rom=option;else throw std::runtime_error("Choose one ROM at a time.");
            }
            if(!rom.empty()&&!game_id.empty())throw std::runtime_error("Choose either a ROM path or --game, not both.");
            if((headless||window_test)&&capture.empty())throw std::runtime_error("Capture mode requires --capture FILE.png.");
            if(headless&&window_test)throw std::runtime_error("Choose --headless or --window-test.");
            Runtime runtime;
            if(!game_id.empty()){
                const auto games=arcade_games();auto found=std::find_if(games.begin(),games.end(),[&](const ArcadeGame &g){return game_id==g.id;});
                if(found==games.end())throw std::runtime_error("Unknown arcade game: "+game_id);
                const auto index=static_cast<unsigned>(found-games.begin());runtime=Runtime(arcade_rom_path(index));runtime.library_game=static_cast<int>(index);
                runtime.library_selection=index;runtime.title=found->title;
            }else if(!rom.empty())runtime=Runtime(rom);
            runtime.inspector_view=inspector||position;runtime.inspector_tab=tab;runtime.memory_region=memory_region;
            runtime.green_palette=green;runtime.controls_visible=!hide_controls;
            if(input_frames==0)runtime.set_buttons(static_cast<std::uint16_t>(buttons));
            if(runtime.has_game())for(unsigned i=0;i<frames;++i)runtime.run_frame();
            if(input_frames){runtime.set_buttons(static_cast<std::uint16_t>(buttons));for(unsigned i=0;i<input_frames;++i)runtime.run_frame();}
            if(position)runtime.position(line,dot);
            runtime.paused=paused||headless||window_test;
            if(library){runtime.library_was_paused=paused;runtime.library_view=true;runtime.paused=true;runtime.set_buttons(0);}
            if(!runtime.has_game())runtime.paused=true;
            active_runtime=&runtime;Presentation host(runtime);presentation=&host;host.window_test=window_test;host.capture=capture;
            if(headless){auto bitmap=runtime.render();runtime.save(capture,*bitmap,false);runtime.flush_save();return 0;}
            if(!capture.empty()&&!window_test){auto bitmap=runtime.render();runtime.save(capture,*bitmap,false);host.capture.clear();}
            [NSApplication sharedApplication];[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            auto *delegate=[[AutopsyDelegate alloc]init];[NSApp setDelegate:delegate];
            NSOpenGLPixelFormatAttribute attributes[]{NSOpenGLPFADoubleBuffer,NSOpenGLPFAColorSize,24,NSOpenGLPFAAccelerated,0};
            auto *pixel_format=[[NSOpenGLPixelFormat alloc]initWithAttributes:attributes];if(!pixel_format)throw std::runtime_error("Native OpenGL is unavailable.");
            const auto available=[[NSScreen mainScreen]visibleFrame];
            const double scale=std::min({1.0,(available.size.width-64)/canvas_width,(available.size.height-64)/canvas_height});
            const NSRect bounds=NSMakeRect(0,0,canvas_width*scale,canvas_height*scale);
            auto *window=[[NSWindow alloc]initWithContentRect:bounds styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
            auto *view=[[AutopsyView alloc]initWithFrame:bounds pixelFormat:pixel_format];[pixel_format release];
            [view setWantsBestResolutionOpenGLSurface:YES];delegate.view=view;[window setDelegate:delegate];
            [window setTitle:native(runtime.library_view?"Matchaboy — Original Arcade":"Matchaboy — "+runtime.title)];
            [window setContentMinSize:NSMakeSize(800,575)];[window setContentAspectRatio:NSMakeSize(canvas_width,canvas_height)];
            [window setContentView:view];[window makeFirstResponder:view];create_menus(view);[window center];[window makeKeyAndOrderFront:nil];
            if(!window_test){host.audio.open();runtime.enable_audio();host.sync_audio();}
            [NSApp activateIgnoringOtherApps:YES];
            NSTimer *timer=[NSTimer timerWithTimeInterval:1.0/240 target:view selector:@selector(advance:) userInfo:nil repeats:YES];
            [[NSRunLoop mainRunLoop]addTimer:timer forMode:NSRunLoopCommonModes];[NSApp run];
            [timer invalidate];runtime.flush_save();return host.failed?1:0;
        }catch(const std::exception &e){std::cerr<<"Matchaboy: "<<e.what()<<'\n';return 1;}
    }
}
#pragma clang diagnostic pop

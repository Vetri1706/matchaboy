#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#import <OpenGL/gl.h>
#include "dmg/autopsy.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr unsigned canvas_width = 1280, canvas_height = 920;
struct Color { double r, g, b; };
constexpr Color foreground{0.85, 0.90, 0.94}, muted{0.48, 0.60, 0.67};
constexpr Color cyan{0.25, 0.85, 0.94}, green{0.45, 0.90, 0.60}, orange{1.0, 0.65, 0.32};
constexpr std::array<Color, 4> shades{{{0.80,0.87,0.61},{0.53,0.65,0.40},{0.29,0.43,0.31},{0.10,0.23,0.22}}};
void fill(CGContextRef context, double x, double y, double w, double h, Color color) {
    CGContextSetRGBFillColor(context, color.r, color.g, color.b, 1);
    CGContextFillRect(context, CGRectMake(x, y, w, h));
}
void text(CGContextRef context, double x, double y, const std::string &value,
          double size = 13, Color color = foreground) {
    CFStringRef string = CFStringCreateWithBytes(nullptr,
        reinterpret_cast<const UInt8 *>(value.data()), value.size(), kCFStringEncodingUTF8, false);
    CTFontRef font = CTFontCreateWithName(CFSTR("Menlo"), size, nullptr);
    CGColorRef ink = CGColorCreateGenericRGB(color.r, color.g, color.b, 1);
    const void *keys[]{kCTFontAttributeName, kCTForegroundColorAttributeName};
    const void *values[]{font, ink};
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 2,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(nullptr, string, attributes);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, x, y + size);
    CGContextScaleCTM(context, 1, -1);
    CGContextSetTextPosition(context, 0, 0);
    CTLineDraw(line, context);
    CGContextRestoreGState(context);
    CFRelease(line); CFRelease(attributed); CFRelease(attributes);
    CGColorRelease(ink); CFRelease(font); CFRelease(string);
}
template<typename... Args> std::string format(const char *pattern, Args... args) {
    std::array<char, 512> buffer{};
    std::snprintf(buffer.data(), buffer.size(), pattern, args...);
    return buffer.data();
}
void image(CGContextRef context, CGImageRef bitmap, CGRect rect) {
    CGContextSaveGState(context);
    CGContextTranslateCTM(context, rect.origin.x, rect.origin.y + rect.size.height);
    CGContextScaleCTM(context, 1, -1);
    CGContextDrawImage(context, CGRectMake(0, 0, rect.size.width, rect.size.height), bitmap);
    CGContextRestoreGState(context);
}
class Runtime {
  public:
    std::unique_ptr<dmg::Autopsy> inspector = std::make_unique<dmg::Autopsy>();
    std::unique_ptr<dmg::Bus> bus;
    std::unique_ptr<dmg::Cpu> cpu;
    std::string title;
    bool paused = false;
    std::string window_capture;
    unsigned trace_scroll = 0;
    explicit Runtime(const std::string &path) : title(std::filesystem::path(path).filename().string()) {
        std::ifstream stream(path, std::ios::binary);
        if (!stream) throw std::runtime_error("cannot open ROM: " + path);
        std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(stream)), {});
        bus = std::make_unique<dmg::Bus>(std::move(rom));
        cpu = std::make_unique<dmg::Cpu>(*bus);
        bus->autopsy = inspector.get();
        bus->apu.set_sample_rate(0); // HUD samples the real digital outputs directly.
    }
    void step() { cpu->step(); }
    void run_frame() {
        const auto target = bus->ppu.frames() + 1;
        const auto deadline = bus->cycles() + 70224 * 2;
        while (bus->ppu.frames() < target && bus->cycles() < deadline) step();
    }
    void position(unsigned line, unsigned dot) {
        const auto deadline = bus->cycles() + 70224 * 2;
        do {
            step();
            if (bus->ppu.read(0xFF44) == line && bus->ppu.dot() >= dot && bus->ppu.dot() < dot + 24) return;
        } while (bus->cycles() < deadline);
        throw std::runtime_error("requested PPU position not reached (LCD may be disabled)");
    }
    CGImageRef render() {
        inspector->capture(*cpu, *bus);
        const auto state = inspector->snapshot();
        auto heat = std::make_unique<std::array<dmg::HeatCell, 65536>>();
        inspector->heatmap(*heat);
        CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(nullptr, canvas_width, canvas_height, 8,
            canvas_width * 4, colors, kCGImageAlphaPremultipliedLast);
        if (!context) { CGColorSpaceRelease(colors); throw std::runtime_error("cannot allocate HUD canvas"); }
        CGContextTranslateCTM(context, 0, canvas_height);
        CGContextScaleCTM(context, 1, -1);
        CGContextSetInterpolationQuality(context, kCGInterpolationNone);
        fill(context, 0, 0, canvas_width, canvas_height, {0.035, 0.06, 0.08});
        fill(context, 0, 0, canvas_width, 66, {0.065, 0.105, 0.13});
        text(context, 24, 14, "MATCHABOY", 23, cyan);
        text(context, 305, 22, title, 14);
        text(context, 890, 16, paused ? "PAUSED / LIVE HARDWARE STATE" : "RUNNING / LIVE HARDWARE STATE", 12, green);
        text(context, 890, 36, format("FRAME %llu  T %llu", state.frames, state.cycles), 12, muted);
        text(context, 24, 82, "ADDRESS BUS / 65,536 CELLS", 14, cyan);
        text(context, 24, 106, "BLUE read   RED write   GREEN execute / decay per frame", 11, muted);
        std::array<std::uint8_t, 256 * 256 * 4> pixels{};
        for (unsigned address = 0; address < 65536; ++address) {
            const auto rgb = dmg::Autopsy::heat_rgb((*heat)[address]);
            for (unsigned channel = 0; channel < 3; ++channel) pixels[address * 4 + channel] = rgb[channel];
            pixels[address * 4 + 3] = 255;
        }
        CGContextRef heat_context = CGBitmapContextCreate(pixels.data(), 256, 256, 8, 1024,
            colors, kCGImageAlphaPremultipliedLast);
        CGImageRef heat_image = CGBitmapContextCreateImage(heat_context);
        image(context, heat_image, CGRectMake(24, 130, 512, 512));
        CGImageRelease(heat_image); CGContextRelease(heat_context);
        text(context, 24, 648, "$0000 TOP-LEFT / $FFFF BOTTOM-RIGHT", 11, muted);
        text(context, 566, 82, "LCD / FINAL PALETTE OUTPUT", 14, cyan);
        for (unsigned y = 0; y < 144; ++y) for (unsigned x = 0; x < 160; ++x)
            fill(context, 566 + x * 2, 110 + y * 2, 2, 2, shades[state.lcd[y * 160 + x] & 3]);
        text(context, 566, 410, format("LY %03u DOT %03u MODE %u / STAT %u", state.ly, state.dot, state.mode, state.reported_mode), 12);
        text(context, 566, 430, format("X %03u  VRAM %s  OAM %s", state.output_x,
            state.vram_locked ? "LOCK" : "OPEN", state.oam_locked ? "LOCK" : "OPEN"), 12, muted);
        text(context, 566, 457, "PIXEL FIFO / NEXT OUTPUT AT LEFT", 13, cyan);
        for (unsigned row = 0; row < 2; ++row) {
            text(context, 566, 487 + row * 56, row == 0 ? "BG" : "OBJ", 12, row == 0 ? green : orange);
            for (unsigned pixel = 0; pixel < 8; ++pixel) {
                const auto color = row == 0 ? state.background_fifo[pixel] : state.object_fifo[pixel];
                const auto palette = row == 0 ? state.bgp : (state.object_flags[pixel] & 0x10) ? state.obp1 : state.obp0;
                const auto shade = (palette >> (color * 2)) & 3;
                const bool valid = row != 0 || pixel < state.fifo_depth;
                fill(context, 602 + pixel * 35, 480 + row * 56, 31, 30,
                     valid ? shades[shade] : Color{0.14, 0.18, 0.20});
                text(context, 611 + pixel * 35, 486 + row * 56,
                     valid ? format("%u", color) : "-", 12, shade < 2 && valid ? shades[3] : foreground);
                const auto tile = row == 0 ? state.background_tile_ids[pixel] : state.object_tile_ids[pixel];
                text(context, 604 + pixel * 35, 513 + row * 56,
                     valid && tile != 0xFFFF ? format("%03X", tile) : "---", 9, muted);
                if (row != 0)
                    text(context, 621 + pixel * 35, 541, (state.object_flags[pixel] & 0x10) ? "1" : "0", 7,
                         shade < 2 ? shades[3] : foreground);
            }
        }
        constexpr std::array<const char *, 4> phases{"TILE ID", "LOW", "HIGH", "PUSH"};
        text(context, 566, 600, format("FETCH %s:%u TILE $%02X X %u", phases[std::min(state.fetch_phase, 3U)],
            state.fetch_ticks, state.tile, state.tile_x), 12);
        text(context, 566, 621, format("PLANES %02X/%02X %s OBJ %s:%u", state.tile_low, state.tile_high,
            state.window ? "WIN" : "BG ", state.object_fetching ? "FETCH" : "IDLE", state.object_phase), 12, muted);
        text(context, 566, 642, format("DISCARD %u+%u BGP %02X OBP %02X/%02X", state.previsible_discard,
            state.fine_discard, state.bgp, state.obp0, state.obp1), 12, muted);
        text(context, 918, 82, "CPU / RETIRED INSTRUCTIONS", 14, cyan);
        const auto &r = state.registers;
        text(context, 918, 111, format("PC %04X SP %04X A %02X F %02X", state.pc, state.sp, r[0], r[1]), 12);
        text(context, 918, 133, format("BC %02X%02X DE %02X%02X HL %02X%02X", r[2], r[3], r[4], r[5], r[6], r[7]), 12);
        text(context, 918, 155, format("%c%c%c%c IME %u IE %02X IF %02X", r[1]&128?'Z':'-', r[1]&64?'N':'-',
            r[1]&32?'H':'-', r[1]&16?'C':'-', state.ime, state.ie, state.interrupt_flags), 12, orange);
        fill(context, 918, 181, 338, 460, {0.045, 0.08, 0.10});
        const unsigned end = state.trace_count > trace_scroll ? state.trace_count - trace_scroll : 0;
        const unsigned begin = end > 21 ? end - 21 : 0;
        for (unsigned i = begin; i < end; ++i) {
            const auto &record = state.trace[i];
            text(context, 928, 190 + (i - begin) * 21,
                 format("%04X  %.37s", record.pc, record.text.data()), 11,
                 i + 1 == end ? green : foreground);
        }
        text(context, 24, 684, "APU / FOUR REAL DIGITAL CHANNEL OUTPUTS", 14, cyan);
        text(context, 630, 688, "32 T-CYCLES / SAMPLE  |  WINDOW 3.906 ms  |  0..15 DAC INPUT", 11, muted);
        constexpr std::array<const char *, 4> names{"PULSE 1", "PULSE 2", "WAVE", "NOISE"};
        constexpr std::array<Color, 4> wave_colors{cyan, green, orange, Color{0.8,0.55,0.95}};
        for (unsigned channel = 0; channel < 4; ++channel) {
            const double y = 718 + channel * 39;
            text(context, 24, y + 4, names[channel], 12, wave_colors[channel]);
            text(context, 98, y + 4, format("%02u", state.levels[channel]), 12, muted);
            fill(context, 133, y, 1123, 31, {0.07,0.11,0.13});
            CGContextSetRGBStrokeColor(context, wave_colors[channel].r, wave_colors[channel].g, wave_colors[channel].b, 1);
            CGContextSetLineWidth(context, 1);
            CGContextBeginPath(context);
            for (unsigned sample = 0; sample < state.wave_count; ++sample) {
                const double x = 134 + sample * (1121.0 / (dmg::AutopsyFrame::wave_length - 1));
                const double level_y = y + 28 - state.waveforms[channel][sample] * 25.0 / 15;
                if (sample == 0) CGContextMoveToPoint(context, x, level_y);
                else CGContextAddLineToPoint(context, x, level_y);
            }
            CGContextStrokePath(context);
        }
        text(context, 24, 891, "SPACE pause   S instruction   F frame   D dot / paused   scroll trace   arrows + Z X Enter Shift joypad", 11, muted);
        CGImageRef rendered = CGBitmapContextCreateImage(context);
        CGContextRelease(context); CGColorSpaceRelease(colors);
        return rendered;
    }
    void save(const std::string &path, CGImageRef source = nullptr) {
        CGImageRef rendered = source ? CGImageRetain(source) : render();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr,
            reinterpret_cast<const UInt8 *>(path.data()), path.size(), false);
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, CFSTR("public.png"), 1, nullptr);
        if (!destination) { CGImageRelease(rendered); CFRelease(url); throw std::runtime_error("cannot create capture: " + path); }
        CGImageDestinationAddImage(destination, rendered, nullptr);
        const bool success = CGImageDestinationFinalize(destination);
        const auto width = CGImageGetWidth(rendered), height = CGImageGetHeight(rendered);
        CFRelease(destination); CFRelease(url); CGImageRelease(rendered);
        if (!success) throw std::runtime_error("cannot write capture: " + path);
        const auto state = inspector->snapshot();
        std::ofstream metadata(path + ".json");
        metadata << format("{\"width\":%zu,\"height\":%zu,\"gpu_readback\":%s,\"frames\":%llu,\"cycles\":%llu,\"ly\":%u,\"dot\":%u,\"mode\":%u,\"fifo_depth\":%u}\n",
            width, height, source ? "true" : "false", state.frames, state.cycles, state.ly, state.dot, state.mode, state.fifo_depth);
        if (!metadata) throw std::runtime_error("cannot write capture metadata");
        std::cout << format("Captured %s: frame=%llu cycles=%llu LY=%u dot=%u mode=%u fifo=%u\n",
            path.c_str(), state.frames, state.cycles, state.ly, state.dot, state.mode, state.fifo_depth);
    }
};
Runtime *active_runtime{};
}

@interface AutopsyView : NSOpenGLView {
    GLuint texture_;
    std::uint8_t buttons_;
}
- (void)advance:(NSTimer *)timer;
@end
@implementation AutopsyView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)prepareOpenGL {
    [super prepareOpenGL];
    GLint swap = 1;
    [[self openGLContext] setValues:&swap forParameter:NSOpenGLCPSwapInterval];
    glGenTextures(1, &texture_);
}
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect;
    [[self openGLContext] makeCurrentContext];
    CGImageRef rendered = active_runtime->render();
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    std::vector<std::uint8_t> pixels(canvas_width * canvas_height * 4);
    CGContextRef context = CGBitmapContextCreate(pixels.data(), canvas_width, canvas_height, 8,
        canvas_width * 4, colors, kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(context, CGRectMake(0, 0, canvas_width, canvas_height), rendered);
    const NSRect bounds = [self convertRectToBacking:[self bounds]];
    glViewport(0, 0, static_cast<GLsizei>(bounds.size.width), static_cast<GLsizei>(bounds.size.height));
    glClearColor(0.035F, 0.06F, 0.08F, 1); glClear(GL_COLOR_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 1, 0, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, canvas_width, canvas_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glColor3f(1,1,1); glBegin(GL_QUADS);
    // CGBitmapContext's first row is the top of the HUD; OpenGL's lower
    // vertices therefore sample the last texture row.
    glTexCoord2f(0,1); glVertex2f(0,0); glTexCoord2f(1,1); glVertex2f(1,0);
    glTexCoord2f(1,0); glVertex2f(1,1); glTexCoord2f(0,0); glVertex2f(0,1);
    glEnd(); glDisable(GL_TEXTURE_2D);
    if (!active_runtime->window_capture.empty()) {
        const auto width = static_cast<std::size_t>(bounds.size.width);
        const auto height = static_cast<std::size_t>(bounds.size.height);
        std::vector<std::uint8_t> readback(width * height * 4), top_down(readback.size());
        glReadBuffer(GL_BACK); glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height), GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("OpenGL back-buffer readback failed");
        for (std::size_t y = 0; y < height; ++y)
            std::copy_n(readback.data() + (height - 1 - y) * width * 4, width * 4, top_down.data() + y * width * 4);
        CGContextRef read_context = CGBitmapContextCreate(top_down.data(), width, height, 8, width * 4,
            colors, kCGImageAlphaPremultipliedLast);
        CGImageRef read_image = CGBitmapContextCreateImage(read_context);
        active_runtime->save(active_runtime->window_capture, read_image);
        CGImageRelease(read_image); CGContextRelease(read_context);
        active_runtime->window_capture.clear();
        [NSApp terminate:nil];
    }
    [[self openGLContext] flushBuffer];
    CGContextRelease(context); CGColorSpaceRelease(colors); CGImageRelease(rendered);
}
- (void)advance:(NSTimer *)timer {
    (void)timer;
    if (!active_runtime->paused) active_runtime->run_frame();
    [self setNeedsDisplay:YES];
}
- (void)scrollWheel:(NSEvent *)event {
    const int next = static_cast<int>(active_runtime->trace_scroll) + static_cast<int>([event scrollingDeltaY]);
    active_runtime->trace_scroll = static_cast<unsigned>(std::clamp(next, 0, 107));
    [self setNeedsDisplay:YES];
}
- (void)keyDown:(NSEvent *)event {
    NSString *characters = [[event charactersIgnoringModifiers] lowercaseString];
    if ([characters isEqualToString:@" "]) active_runtime->paused = !active_runtime->paused;
    else if ([characters isEqualToString:@"s"]) { active_runtime->paused = true; active_runtime->step(); }
    else if ([characters isEqualToString:@"f"]) { active_runtime->paused = true; active_runtime->run_frame(); }
    else if ([characters isEqualToString:@"d"]) { active_runtime->paused = true; active_runtime->bus->tick(1); }
    else {
        const unsigned key = [event keyCode];
        const unsigned bit = key == 124 ? 0 : key == 123 ? 1 : key == 126 ? 2 : key == 125 ? 3 :
            key == 6 ? 4 : key == 7 ? 5 : key == 56 ? 6 : key == 36 ? 7 : 8;
        if (bit < 8) buttons_ |= static_cast<std::uint8_t>(1U << bit);
        active_runtime->bus->set_buttons(buttons_);
    }
    [self setNeedsDisplay:YES];
}
- (void)keyUp:(NSEvent *)event {
    const unsigned key = [event keyCode];
    const unsigned bit = key == 124 ? 0 : key == 123 ? 1 : key == 126 ? 2 : key == 125 ? 3 :
        key == 6 ? 4 : key == 7 ? 5 : key == 56 ? 6 : key == 36 ? 7 : 8;
    if (bit < 8) buttons_ &= static_cast<std::uint8_t>(~(1U << bit));
    active_runtime->bus->set_buttons(buttons_);
}
- (void)flagsChanged:(NSEvent *)event {
    if ([event modifierFlags] & NSEventModifierFlagShift) buttons_ |= 0x40;
    else buttons_ &= 0xBF;
    active_runtime->bus->set_buttons(buttons_);
}
@end
@interface AutopsyDelegate : NSObject <NSApplicationDelegate>
@end
@implementation AutopsyDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { (void)sender; return YES; }
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        try {
            if (argc < 2) throw std::runtime_error("usage: autopsy ROM [--frames N] [--headless --capture PATH] [--line N --dot N] [--paused]");
            unsigned frames = 120, line = 0, dot = 0;
            bool headless = false, position = false, paused = false, window_test = false;
            std::string capture;
            for (int argument = 2; argument < argc; ++argument) {
                const std::string option = argv[argument];
                if (option == "--headless") headless = true;
                else if (option == "--window-test") window_test = true;
                else if (option == "--paused") paused = true;
                else if (argument + 1 == argc) throw std::runtime_error("missing value for " + option);
                else if (option == "--frames") frames = static_cast<unsigned>(std::stoul(argv[++argument]));
                else if (option == "--line") { line = static_cast<unsigned>(std::stoul(argv[++argument])); position = true; }
                else if (option == "--dot") { dot = static_cast<unsigned>(std::stoul(argv[++argument])); position = true; }
                else if (option == "--capture") capture = argv[++argument];
                else throw std::runtime_error("unknown option " + option);
            }
            if (line > 153 || dot > 455) throw std::runtime_error("invalid PPU position");
            Runtime runtime(argv[1]); active_runtime = &runtime;
            for (unsigned frame = 0; frame < frames; ++frame) runtime.run_frame();
            if (position) runtime.position(line, dot);
            runtime.paused = paused || headless || window_test;
            if (window_test) {
                if (capture.empty() || headless) throw std::runtime_error("--window-test requires --capture and a native window");
                runtime.window_capture = capture;
            } else if (!capture.empty()) runtime.save(capture);
            if (headless) {
                if (capture.empty()) throw std::runtime_error("--headless requires --capture");
                return 0;
            }
            [NSApplication sharedApplication];
            [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
            AutopsyDelegate *delegate = [[AutopsyDelegate alloc] init];
            [NSApp setDelegate:delegate];
            NSOpenGLPixelFormatAttribute attributes[]{NSOpenGLPFADoubleBuffer, NSOpenGLPFAColorSize, 24,
                NSOpenGLPFAAccelerated, 0};
            NSOpenGLPixelFormat *pixel_format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attributes];
            if (!pixel_format) throw std::runtime_error("native OpenGL pixel format unavailable");
            NSRect bounds = NSMakeRect(0, 0, canvas_width, canvas_height);
            NSWindow *window = [[NSWindow alloc] initWithContentRect:bounds
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered defer:NO];
            AutopsyView *view = [[AutopsyView alloc] initWithFrame:bounds pixelFormat:pixel_format];
            [view setWantsBestResolutionOpenGLSurface:YES];
            [window setTitle:@"Matchaboy — DMG hardware inspector"];
            [window setContentView:view]; [window makeFirstResponder:view];
            [window setContentAspectRatio:NSMakeSize(canvas_width, canvas_height)];
            [window center]; [window makeKeyAndOrderFront:nil];
            [NSApp activateIgnoringOtherApps:YES];
            [NSTimer scheduledTimerWithTimeInterval:1.0 / 59.7275005696 target:view selector:@selector(advance:) userInfo:nil repeats:YES];
            [NSApp run];
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "autopsy: " << error.what() << '\n'; return 1;
        }
    }
}
#pragma clang diagnostic pop

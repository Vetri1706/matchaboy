#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

// No mGBA headers/types leak into the DMG engine or native window.
class GbaCore {
public:
    explicit GbaCore(const std::filesystem::path &rom);
    ~GbaCore();
    void run_frame();
    void set_buttons(std::uint16_t buttons);
    void flush_save();
    std::uint64_t frames() const;
    std::span<const std::uint32_t> pixels() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

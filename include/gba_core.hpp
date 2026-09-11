#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <span>

struct GbaInspection {
    std::array<std::uint32_t, 16> registers{}, opcodes{};
    std::uint32_t cpsr{}, code_base{}, memory_base{};
    std::array<std::uint8_t, 256> memory{};
    std::array<std::uint16_t, 256> palette{};
    std::uint16_t dispcnt{}, dispstat{}, vcount{}, sound_low{}, sound_high{}, sound_enable{}, sound_bias{};
    std::array<std::int16_t, 1024> audio{};
    unsigned audio_frames{};
};
// No mGBA headers/types leak into the DMG engine or native window.
class GbaCore {
public:
    explicit GbaCore(const std::filesystem::path &rom);
    ~GbaCore();
    void run_frame();
    void step();
    GbaInspection inspect(std::uint32_t memory_base) const;
    void set_buttons(std::uint16_t buttons);
    void flush_save();
    std::uint64_t frames() const;
    std::span<const std::uint32_t> pixels() const;
    void enable_audio();
    std::size_t drain_audio(std::span<std::int16_t> destination);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

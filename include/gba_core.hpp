#pragma once
#include <cstdint>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

class GbaLinkedPair;

struct GbaInspection {
    std::array<std::uint32_t, 16> registers{}, opcodes{};
    std::uint32_t cpsr{}, code_base{}, memory_base{};
    std::array<std::uint8_t, 256> memory{};
    std::array<std::uint16_t, 256> palette{};
    std::uint16_t dispcnt{}, dispstat{}, vcount{}, sound_low{}, sound_high{}, sound_enable{}, sound_bias{};
    std::uint16_t siocnt{}, rcnt{}, serial_send{}, interrupt_flags{};
    std::array<std::uint16_t, 4> serial_multi{};
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
    bool fresh() const;
    std::span<const std::uint32_t> pixels() const;
    void enable_audio();
    std::size_t drain_audio(std::span<std::int16_t> destination);
    std::vector<std::uint8_t> export_save() const;
    // Replaces in-memory cartridge save and resets the CPU. Never writes the
    // source save file; must be called before attaching a linked pair.
    void import_save(std::span<const std::uint8_t> bytes);
    std::uint64_t digest() const;
private:
    friend class GbaLinkedPair;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// Both referenced consoles must outlive the pair. No background threads: each
// call cooperatively runs the real CPUs and the mGBA serial lockstep driver.
class GbaLinkedPair {
public:
    GbaLinkedPair(GbaCore &player0, GbaCore &player1);
    ~GbaLinkedPair();
    GbaLinkedPair(const GbaLinkedPair &) = delete;
    GbaLinkedPair &operator=(const GbaLinkedPair &) = delete;
    void run_frame();
    std::uint64_t frames() const;
    std::uint64_t digest() const;
private:
    friend class GbaCore;
    struct Impl;
    std::unique_ptr<Impl> impl;
};

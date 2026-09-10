#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace dmg {
class Cpu;
class Bus;
inline constexpr std::size_t SnapshotSerialCapacity = 65536;
inline constexpr std::size_t SnapshotCapacity = 262144;
inline constexpr std::uint32_t SnapshotVersion = 2;

enum class SnapshotResult {
    Ok,
    InvalidSnapshot,
    VersionMismatch,
    RomMismatch,
    MachineMismatch,
    SerialOverflow,
    SerialCapacity,
    CapacityExceeded,
    HistoryUnavailable
};

// Only size bytes participate in equality, hashing, or restore. The remaining
// preallocated bytes are scratch space, not machine state or object padding.
struct MachineSnapshot {
    std::array<std::uint8_t, SnapshotCapacity> data{};
    std::uint32_t size{};
    std::uint64_t checksum{};
};

[[nodiscard]] std::array<std::uint8_t, 32>
snapshot_rom_identity(std::span<const std::uint8_t> rom) noexcept;
// Call between completed CPU instructions and outside all bus callbacks.
// Devices may be at any dot, DMA/serial bit, timer reload, or FIFO phase.
// CPU execution callbacks are rejected: C++ instruction locals are not state.
[[nodiscard]] SnapshotResult save_snapshot(const Cpu &, const Bus &, MachineSnapshot &) noexcept;
[[nodiscard]] SnapshotResult load_snapshot(Cpu &, Bus &, const MachineSnapshot &) noexcept;
[[nodiscard]] bool snapshot_equal(const MachineSnapshot &, const MachineSnapshot &) noexcept;
[[nodiscard]] std::uint64_t snapshot_hash(const MachineSnapshot &) noexcept;
[[nodiscard]] const char *snapshot_result_name(SnapshotResult) noexcept;

class SnapshotRing {
  public:
    static constexpr std::size_t Capacity = 60;
    // This is the sole ring allocation. Capture, rewind and lookup allocate none.
    SnapshotRing();
    SnapshotRing(const SnapshotRing &) = delete;
    SnapshotRing &operator=(const SnapshotRing &) = delete;
    [[nodiscard]] SnapshotResult capture(const Cpu &, const Bus &) noexcept;
    // Zero restores the latest captured frame; one restores its predecessor.
    // A successful rewind discards newer history, so replay creates a branch.
    [[nodiscard]] SnapshotResult rewind(Cpu &, Bus &, std::size_t frames_back) noexcept;
    [[nodiscard]] const MachineSnapshot *at(std::size_t frames_back) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return count_; }
    void clear() noexcept { next_ = count_ = 0; }

  private:
    std::unique_ptr<MachineSnapshot[]> frames_;
    std::size_t next_{}, count_{};
};
} // namespace dmg

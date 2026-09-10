#pragma once
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/snapshot.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <type_traits>
#include <vector>

namespace dmg {
// Exactly one producer and one consumer per queue. Each try operation has a
// fixed number of lock-free atomic accesses: no CAS/retry loop, allocation,
// mutex or operating-system call. Waiting belongs to the caller, not the queue.
template<class T, std::size_t Capacity> class SpscTaskQueue {
    static_assert(Capacity > 1 && (Capacity & (Capacity - 1)) == 0);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::is_trivially_copyable_v<T> && std::is_nothrow_copy_assignable_v<T>);
    std::array<T, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> enqueue_{};
    alignas(64) std::atomic<std::size_t> dequeue_{};
  public:
    bool push(T value) noexcept {
        const auto position = enqueue_.load(std::memory_order_relaxed);
        // Unsigned distance remains valid across counter wrap because the
        // producer can never advance more than Capacity beyond the consumer.
        if (position - dequeue_.load(std::memory_order_acquire) == Capacity)
            return false;
        slots_[position & (Capacity - 1)] = value;
        enqueue_.store(position + 1, std::memory_order_release);
        return true;
    }
    bool pop(T &value) noexcept {
        const auto position = dequeue_.load(std::memory_order_relaxed);
        if (position == enqueue_.load(std::memory_order_acquire))
            return false;
        value = slots_[position & (Capacity - 1)];
        dequeue_.store(position + 1, std::memory_order_release);
        return true;
    }
};

struct RewardWatch {
    std::uint16_t address{};
    std::uint8_t width{1};
    float scale{1};
    bool delta{true}, signed_value{};
};

class HeadlessGym {
  public:
    static constexpr std::uint64_t CyclesPerFrame = 70224;
    static constexpr std::size_t MaxEnvironments = 256, MaxWatches = 32;
    HeadlessGym(std::vector<std::uint8_t> rom, std::size_t count, std::size_t workers = 0);
    ~HeadlessGym();
    HeadlessGym(const HeadlessGym &) = delete;
    HeadlessGym &operator=(const HeadlessGym &) = delete;
    void step(std::span<const std::uint8_t> actions, std::span<float> rewards,
              std::span<std::uint8_t> dones);
    void reset(std::size_t index);
    void reset_all();
    [[nodiscard]] std::size_t size() const noexcept { return environments_.size(); }
    [[nodiscard]] std::size_t workers() const noexcept { return workers_.size(); }
    [[nodiscard]] Bus &bus(std::size_t index);
    [[nodiscard]] const Bus &bus(std::size_t index) const;
    [[nodiscard]] const Cpu &cpu(std::size_t index) const;
    [[nodiscard]] std::uint64_t frames(std::size_t index) const;
    [[nodiscard]] bool terminated(std::size_t index) const;
    [[nodiscard]] bool truncated(std::size_t index) const;
    void add_watch(RewardWatch watch);
    void clear_watches();
    void set_terminal(std::uint16_t address, std::uint8_t mask, std::uint8_t value);
    void clear_terminal() noexcept { terminal_enabled_ = false; }
    void set_max_steps(std::uint64_t frames) noexcept { max_steps_ = frames; }
  private:
    struct Environment {
        Bus bus;
        Cpu cpu;
        MachineSnapshot initial;
        std::array<std::int64_t, MaxWatches> watched{};
        std::uint64_t target{}, frames{};
        float reward{};
        std::uint8_t action{};
        bool terminated{}, truncated{};
        explicit Environment(const std::vector<std::uint8_t> &rom);
    };
    std::vector<std::unique_ptr<Environment>> environments_;
    std::vector<std::thread> workers_;
    std::vector<std::unique_ptr<SpscTaskQueue<std::size_t, MaxEnvironments>>> jobs_;
    std::atomic<std::uint64_t> wake_{};
    std::atomic<std::size_t> pending_{};
    std::atomic<bool> stopping_{};
    std::atomic<bool> failed_{};
    std::array<RewardWatch, MaxWatches> watches_{};
    std::size_t watch_count_{};
    std::uint64_t max_steps_{};
    std::uint16_t terminal_address_{};
    std::uint8_t terminal_mask_{}, terminal_value_{};
    bool terminal_enabled_{};
    void worker_loop(std::size_t worker_index) noexcept;
    void execute(std::size_t index) noexcept;
    [[nodiscard]] std::int64_t watched_value(const Bus &, const RewardWatch &) const;
};
} // namespace dmg

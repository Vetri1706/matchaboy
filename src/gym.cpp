#include "dmg/gym.hpp"
#include "dmg/gym.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace dmg {
HeadlessGym::Environment::Environment(const std::vector<std::uint8_t> &rom) : bus(rom), cpu(bus) {
    bus.capture_serial_output = false;
    bus.apu.set_sample_rate(0);
    if (save_snapshot(cpu, bus, initial) != SnapshotResult::Ok)
        throw std::runtime_error("cannot capture initial Gym machine state");
}
HeadlessGym::HeadlessGym(std::vector<std::uint8_t> rom, std::size_t count, std::size_t workers) {
    if (count == 0 || count > MaxEnvironments)
        throw std::invalid_argument("environment count must be 1..256");
    environments_.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
        environments_.push_back(std::make_unique<Environment>(rom));
    workers = std::min(count, workers == 0 ? std::max(1U, std::thread::hardware_concurrency()) : workers);
    // Allocate every queue before starting its sole consumer. The serialized
    // API caller is the sole producer for each fixed round-robin assignment.
    jobs_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i)
        jobs_.push_back(std::make_unique<SpscTaskQueue<std::size_t, MaxEnvironments>>());
    workers_.reserve(workers);
    try {
        for (std::size_t i = 0; i < workers; ++i)
            workers_.emplace_back([this, i] { worker_loop(i); });
    } catch (...) {
        stopping_.store(true, std::memory_order_release);
        wake_.fetch_add(1, std::memory_order_release);
        wake_.notify_all();
        for (auto &worker : workers_)
            worker.join();
        throw;
    }
}
HeadlessGym::~HeadlessGym() {
    stopping_.store(true, std::memory_order_release);
    wake_.fetch_add(1, std::memory_order_release);
    wake_.notify_all();
    for (auto &worker : workers_)
        worker.join();
}
void HeadlessGym::worker_loop(std::size_t worker_index) noexcept {
    auto &queue = *jobs_[worker_index];
    while (!stopping_.load(std::memory_order_acquire)) {
        const auto epoch = wake_.load(std::memory_order_acquire);
        std::size_t index = 0;
        if (queue.pop(index)) {
            execute(index);
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                pending_.notify_one();
        } else {
            if (stopping_.load(std::memory_order_acquire))
                return;
            wake_.wait(epoch, std::memory_order_acquire);
        }
    }
}
void HeadlessGym::step(std::span<const std::uint8_t> actions, std::span<float> rewards,
                       std::span<std::uint8_t> dones) {
    if (actions.size() != size() || rewards.size() != size() || dones.size() != size())
        throw std::invalid_argument("actions, rewards and dones must match environment count");
    if (pending_.load(std::memory_order_acquire) != 0)
        throw std::logic_error("concurrent calls on a Gym handle are unsupported");
    failed_.store(false, std::memory_order_relaxed);
    for (std::size_t i = 0; i < size(); ++i)
        environments_[i]->action = actions[i];
    pending_.store(size(), std::memory_order_release);
    for (std::size_t i = 0; i < size(); ++i)
        if (!jobs_[i % jobs_.size()]->push(i))
            throw std::logic_error("bounded task queue capacity invariant failed");
    wake_.fetch_add(1, std::memory_order_release);
    wake_.notify_all();
    for (auto count = pending_.load(std::memory_order_acquire); count != 0;
         count = pending_.load(std::memory_order_acquire))
        pending_.wait(count, std::memory_order_acquire);
    for (std::size_t i = 0; i < size(); ++i) {
        rewards[i] = environments_[i]->reward;
        dones[i] = static_cast<std::uint8_t>(environments_[i]->terminated || environments_[i]->truncated);
    }
    if (failed_.load(std::memory_order_acquire))
        throw std::runtime_error("worker could not execute the machine; reset affected environment");
}
std::int64_t HeadlessGym::watched_value(const Bus &bus, const RewardWatch &watch) const {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < watch.width; ++i)
        value |= static_cast<std::uint32_t>(bus.peek(static_cast<std::uint16_t>(watch.address + i))) << (i * 8U);
    if (watch.signed_value && (value & (std::uint32_t{1} << (watch.width * 8U - 1U))) != 0)
        return static_cast<std::int64_t>(value) - (std::int64_t{1} << (watch.width * 8U));
    return value;
}
void HeadlessGym::execute(std::size_t index) noexcept {
    auto &env = *environments_[index];
    env.reward = 0;
    if (env.terminated || env.truncated)
        return;
    try {
        env.bus.set_buttons(env.action);
        if (env.target > std::numeric_limits<std::uint64_t>::max() - CyclesPerFrame)
            throw std::overflow_error("emulated cycle horizon exhausted");
        env.target += CyclesPerFrame;
        while (env.bus.cycles() < env.target) {
            const auto before = env.bus.cycles();
            env.cpu.step();
            if (env.cpu.locked) {
                env.terminated = true;
                break;
            }
            if (env.bus.cycles() == before) {
                // STOP gates the oscillator. Return control to the policy so
                // another action can wake it; do not invent executed cycles.
                env.target = before;
                break;
            }
        }
        ++env.frames;
        for (std::size_t i = 0; i < watch_count_; ++i) {
            const auto value = watched_value(env.bus, watches_[i]);
            const auto quantity = watches_[i].delta ? value - env.watched[i] : value;
            env.reward += watches_[i].scale * static_cast<float>(quantity);
            env.watched[i] = value;
        }
        if (terminal_enabled_ && (env.bus.peek(terminal_address_) & terminal_mask_) == terminal_value_)
            env.terminated = true;
        env.truncated = max_steps_ != 0 && env.frames >= max_steps_;
    } catch (...) {
        env.terminated = true;
        failed_.store(true, std::memory_order_release);
    }
}
Bus &HeadlessGym::bus(std::size_t index) { return environments_.at(index)->bus; }
const Bus &HeadlessGym::bus(std::size_t index) const { return environments_.at(index)->bus; }
const Cpu &HeadlessGym::cpu(std::size_t index) const { return environments_.at(index)->cpu; }
std::uint64_t HeadlessGym::frames(std::size_t index) const { return environments_.at(index)->frames; }
bool HeadlessGym::terminated(std::size_t index) const { return environments_.at(index)->terminated; }
bool HeadlessGym::truncated(std::size_t index) const { return environments_.at(index)->truncated; }
void HeadlessGym::reset(std::size_t index) {
    auto &env = *environments_.at(index);
    const auto result = load_snapshot(env.cpu, env.bus, env.initial);
    if (result != SnapshotResult::Ok)
        throw std::runtime_error(snapshot_result_name(result));
    env.target = env.frames = 0;
    env.reward = 0;
    env.action = 0;
    env.terminated = env.truncated = false;
    for (std::size_t i = 0; i < watch_count_; ++i)
        env.watched[i] = watched_value(env.bus, watches_[i]);
}
void HeadlessGym::reset_all() {
    for (std::size_t i = 0; i < size(); ++i)
        reset(i);
}
void HeadlessGym::add_watch(RewardWatch watch) {
    if ((watch.width != 1 && watch.width != 2 && watch.width != 4) ||
        static_cast<unsigned>(watch.address) + watch.width > 65536U || !std::isfinite(watch.scale))
        throw std::invalid_argument("invalid RAM watch width, address or scale");
    if (watch_count_ == MaxWatches)
        throw std::length_error("maximum 32 reward watches");
    watches_[watch_count_] = watch;
    for (auto &env : environments_)
        env->watched[watch_count_] = watched_value(env->bus, watch);
    ++watch_count_;
}
void HeadlessGym::clear_watches() { watch_count_ = 0; }
void HeadlessGym::set_terminal(std::uint16_t address, std::uint8_t mask, std::uint8_t value) {
    if ((value & mask) != value)
        throw std::invalid_argument("terminal value contains bits outside its mask");
    terminal_address_ = address;
    terminal_mask_ = mask;
    terminal_value_ = value;
    terminal_enabled_ = true;
}
} // namespace dmg

namespace {
thread_local std::array<char, 512> api_error{};
void clear_error() { api_error[0] = '\0'; }
void error(const char *message) noexcept {
    const auto count = std::min(std::strlen(message), api_error.size() - 1);
    std::memcpy(api_error.data(), message, count);
    api_error[count] = '\0';
}
dmg::HeadlessGym &gym(void *handle) {
    if (handle == nullptr)
        throw std::invalid_argument("null Gym handle");
    return *static_cast<dmg::HeadlessGym *>(handle);
}
std::size_t instance(void *handle, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= gym(handle).size())
        throw std::out_of_range("invalid environment index");
    return static_cast<std::size_t>(index);
}
template<class Function> int checked(Function function) noexcept {
    clear_error();
    try { function(); return 0; }
    catch (const std::exception &exception) { error(exception.what()); }
    catch (...) { error("unknown native exception"); }
    return -1;
}
}
extern "C" {
void *gym_create(const char *rom_path, int num_instances) {
    clear_error();
    try {
        if (rom_path == nullptr || num_instances <= 0)
            throw std::invalid_argument("ROM path and positive environment count required");
        std::ifstream file(rom_path, std::ios::binary);
        if (!file)
            throw std::runtime_error("cannot open ROM");
        std::vector<std::uint8_t> rom{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        if (file.bad())
            throw std::runtime_error("cannot read ROM");
        return new dmg::HeadlessGym(std::move(rom), static_cast<std::size_t>(num_instances));
    } catch (const std::exception &exception) { error(exception.what()); }
    catch (...) { error("unknown native exception"); }
    return nullptr;
}
void gym_destroy(void *handle) { delete static_cast<dmg::HeadlessGym *>(handle); }
const char *gym_last_error() { return api_error.data(); }
int gym_count(void *handle) {
    int result = -1;
    checked([&] { result = static_cast<int>(gym(handle).size()); });
    return result;
}
int gym_step_checked(void *handle, const std::uint8_t *actions, std::size_t count,
                     float *rewards, std::uint8_t *dones) {
    return checked([&] {
        if (actions == nullptr || rewards == nullptr || dones == nullptr)
            throw std::invalid_argument("null batch buffer");
        gym(handle).step({actions, count}, {rewards, count}, {dones, count});
    });
}
void gym_step(void *handle, const std::uint8_t *actions, float *rewards, std::uint8_t *dones) {
    if (handle == nullptr) { error("null Gym handle"); return; }
    static_cast<void>(gym_step_checked(handle, actions, gym(handle).size(), rewards, dones));
}
int gym_reset(void *handle, int index) {
    return checked([&] {
        if (index == -1) gym(handle).reset_all();
        else gym(handle).reset(instance(handle, index));
    });
}
void gym_get_observations(void *handle, std::uint8_t *destination) {
    checked([&] {
        if (destination == nullptr) throw std::invalid_argument("null observation buffer");
        for (std::size_t i = 0; i < gym(handle).size(); ++i) {
            const auto &frame = gym(handle).bus(i).ppu.framebuffer;
            std::memcpy(destination + i * frame.size(), frame.data(), frame.size());
        }
    });
}
void gym_get_ram(void *handle, int index, std::uint8_t **pointer) {
    if (pointer != nullptr) *pointer = nullptr;
    checked([&] {
        if (pointer == nullptr) throw std::invalid_argument("null RAM pointer output");
        *pointer = gym(handle).bus(instance(handle, index)).wram_data();
    });
}
int gym_ram_size(void *handle) {
    int size = -1;
    checked([&] { static_cast<void>(gym(handle)); size = dmg::Bus::WramSize; });
    return size;
}
int gym_observation_ptr(void *handle, int index, const std::uint8_t **pixels, std::size_t *length) {
    if (pixels != nullptr) *pixels = nullptr;
    if (length != nullptr) *length = 0;
    return checked([&] {
        if (pixels == nullptr || length == nullptr) throw std::invalid_argument("null observation output");
        const auto &frame = gym(handle).bus(instance(handle, index)).ppu.framebuffer;
        *pixels = frame.data(); *length = frame.size();
    });
}
std::uint64_t gym_cycles(void *handle, int index) {
    std::uint64_t value = 0;
    checked([&] { value = gym(handle).bus(instance(handle, index)).cycles(); });
    return value;
}
std::uint64_t gym_frames(void *handle, int index) {
    std::uint64_t value = 0;
    checked([&] { value = gym(handle).frames(instance(handle, index)); });
    return value;
}
int gym_peek(void *handle, int index, std::uint16_t address, std::uint8_t *value) {
    return checked([&] {
        if (value == nullptr) throw std::invalid_argument("null byte output");
        *value = gym(handle).bus(instance(handle, index)).peek(address);
    });
}
int gym_add_watch(void *handle, std::uint16_t address, std::uint8_t width, float scale,
                  std::uint8_t delta, std::uint8_t signed_value) {
    return checked([&] {
        if (delta > 1 || signed_value > 1) throw std::invalid_argument("watch flags must be zero or one");
        gym(handle).add_watch({address, width, scale, delta != 0, signed_value != 0});
    });
}
int gym_clear_watches(void *handle) { return checked([&] { gym(handle).clear_watches(); }); }
int gym_set_terminal(void *handle, std::uint16_t address, std::uint8_t mask, std::uint8_t value) {
    return checked([&] { gym(handle).set_terminal(address, mask, value); });
}
int gym_clear_terminal(void *handle) { return checked([&] { gym(handle).clear_terminal(); }); }
int gym_set_max_steps(void *handle, std::uint64_t frames) {
    return checked([&] { gym(handle).set_max_steps(frames); });
}
int gym_episode_flags(void *handle, int index, std::uint8_t *terminated, std::uint8_t *truncated) {
    return checked([&] {
        if (terminated == nullptr || truncated == nullptr) throw std::invalid_argument("null episode output");
        const auto selected = instance(handle, index);
        *terminated = gym(handle).terminated(selected); *truncated = gym(handle).truncated(selected);
    });
}
}

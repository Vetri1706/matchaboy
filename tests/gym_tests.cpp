#include "dmg/gym.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> program() {
    std::vector<std::uint8_t> rom(32768);
    rom[0x100] = 0xC3; rom[0x101] = 0x50; rom[0x102] = 1;
    // Real ROM program reads the d-pad and updates WRAM continuously.
    const std::array<std::uint8_t, 19> bytes{
        0xF3, 0x31, 0xFE, 0xFF, 0x3E, 0x20, 0xE0, 0x00,
        0xF0, 0x00, 0x2F, 0xE6, 0x0F, 0xEA, 0x00, 0xC0, 0xC3, 0x58, 0x01};
    std::copy(bytes.begin(), bytes.end(), rom.begin() + 0x150);
    return rom;
}
void queue_concurrency() {
    constexpr std::size_t producers = 4, each = 20000, total = producers * each;
    std::array<dmg::SpscTaskQueue<std::size_t,256>,producers> queues;
    auto seen = std::make_unique<std::atomic<unsigned>[]>(total);
    std::vector<std::thread> threads;
    for (std::size_t p = 0; p < producers; ++p)
        threads.emplace_back([&, p] {
            for (std::size_t i = 0; i < each; ++i)
                while (!queues[p].push(p * each + i)) std::this_thread::yield();
        });
    for (std::size_t c = 0; c < producers; ++c)
        threads.emplace_back([&,c] {
            std::size_t consumed=0;
            while (consumed < each) {
                std::size_t value = 0;
                if (queues[c].pop(value)) {
                    if (value != c * each + consumed) std::terminate();
                    seen[value].fetch_add(1, std::memory_order_relaxed);
                    ++consumed;
                } else std::this_thread::yield();
            }
        });
    for (auto &thread : threads) thread.join();
    for (std::size_t i = 0; i < total; ++i)
        require(seen[i].load() == 1, "SPSC task lost or delivered twice");
}
void queue_bounds_and_wrap() {
    dmg::SpscTaskQueue<std::size_t,8> queue;
    std::size_t value=999;
    require(!queue.pop(value) && value==999,"empty pop leaves its output unchanged");
    for (std::size_t round=0;round<1000;++round) {
        for (std::size_t i=0;i<8;++i)
            require(queue.push(round*12+i),"queue accepts its full declared capacity");
        require(!queue.push(999),"full push does not overwrite queued work");
        for (std::size_t i=0;i<4;++i)
            require(queue.pop(value) && value==round*12+i,"partial drain preserves FIFO order");
        for (std::size_t i=8;i<12;++i)
            require(queue.push(round*12+i),"released slots are reused across ring wrap");
        for (std::size_t i=4;i<12;++i)
            require(queue.pop(value) && value==round*12+i,"wrapped payloads preserve FIFO order");
        require(!queue.pop(value),"wrapped queue drains completely");
    }
}
void deterministic_batch() {
    constexpr std::size_t count = 16;
    dmg::HeadlessGym single(program(), count, 1), parallel(program(), count, 8);
    single.add_watch({0xC000, 1, 2.0F, false, false});
    parallel.add_watch({0xC000, 1, 2.0F, false, false});
    std::array<std::uint8_t, count> actions{}, done1{}, done2{};
    std::array<float, count> reward1{}, reward2{};
    auto before = std::make_unique<dmg::MachineSnapshot>();
    auto a = std::make_unique<dmg::MachineSnapshot>(), b = std::make_unique<dmg::MachineSnapshot>();
    require(dmg::save_snapshot(parallel.cpu(0), parallel.bus(0), *before) == dmg::SnapshotResult::Ok,
            "initial batch snapshot");
    const auto *pointer = parallel.bus(0).wram_data();
    for (unsigned frame = 0; frame < 20; ++frame) {
        for (std::size_t i = 0; i < count; ++i) actions[i] = static_cast<std::uint8_t>((frame + i) & 15U);
        single.step(actions, reward1, done1); parallel.step(actions, reward2, done2);
        for (std::size_t i = 0; i < count; ++i) {
            require(reward1[i] == actions[i] * 2.0F && reward2[i] == reward1[i], "real WRAM reward extraction");
            require(done1[i] == 0 && done2[i] == 0, "active ROM remains running");
            require(single.bus(i).cycles() >= (frame + 1) * dmg::HeadlessGym::CyclesPerFrame,
                    "every step executes its full cycle horizon");
            require(dmg::save_snapshot(single.cpu(i), single.bus(i), *a) == dmg::SnapshotResult::Ok &&
                    dmg::save_snapshot(parallel.cpu(i), parallel.bus(i), *b) == dmg::SnapshotResult::Ok &&
                    dmg::snapshot_equal(*a, *b), "worker scheduling changed deterministic machine state");
        }
    }
    parallel.reset(0);
    require(pointer == parallel.bus(0).wram_data(), "borrowed RAM address survives reset");
    require(dmg::save_snapshot(parallel.cpu(0), parallel.bus(0), *a) == dmg::SnapshotResult::Ok &&
            dmg::snapshot_equal(*before, *a), "reset restores complete initial state");
    parallel.set_max_steps(1);
    parallel.step(actions, reward2, done2);
    require(parallel.truncated(0) && !parallel.terminated(0), "time limit is truncation, not termination");
    const auto stopped_cycles = parallel.bus(0).cycles();
    parallel.step(actions, reward2, done2);
    require(parallel.bus(0).cycles() == stopped_cycles, "done environment does not silently auto-reset");
    parallel.set_max_steps(0); parallel.reset_all();
    parallel.set_terminal(0xC000, 15, actions[0]);
    parallel.step(actions, reward2, done2);
    require(parallel.terminated(0) && !parallel.truncated(0), "RAM terminal predicate");
}
}
int main() {
    try {
        queue_bounds_and_wrap(); queue_concurrency(); deterministic_batch();
        std::cout << "PASS 80000 concurrent SPSC jobs across four producer/consumer pairs, full/empty/wrap; 16 independent machines x20 frames, worker-order replay, rewards, reset and termination\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Gym test failure: " << error.what() << '\n';
        return 1;
    }
}

#pragma once
#include <cstdint>

namespace dmg {
class Bus;
// External transport state is not machine state. Callbacks run inside a CPU
// instruction: enqueue events here, and restore snapshots only between steps.
struct SerialEndpoint {
    void *context{};
    void (*on_start)(void *, Bus &, std::uint64_t cycle, std::uint8_t outgoing,
                     std::uint8_t control){};
    void (*on_tick)(void *, Bus &, std::uint64_t cycle){};
    bool (*exchange_bit)(void *, Bus &, std::uint64_t cycle, bool outgoing){};
};
} // namespace dmg

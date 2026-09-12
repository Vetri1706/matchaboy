#pragma once
#include "friend_transport.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <span>

namespace dmg { class Bus; class Cpu; }
class GbaCore;
namespace matcha {
// The display owns the local console; a session owns the remote console and
// connects the pair. Neither console runs speculatively on missing input.
class FriendSession {
public:
    FriendSession(const FriendTransportOptions &, const std::filesystem::path &rom,
                  dmg::Bus *local_bus, dmg::Cpu *local_cpu, GbaCore *local_gba);
    ~FriendSession();
    FriendSession(const FriendSession &) = delete;
    FriendSession &operator=(const FriendSession &) = delete;
    // Call from the UI thread at least 60 times/second, even while waiting.
    // Buttons use Matchaboy's Right Left Up Down A B Select Start L R order.
    // Returns true only when one confirmed linked frame actually advances.
    bool advance(std::uint16_t buttons);
    void service();
    std::string status() const;
    // Read-only troubleshooting text; excludes room codes, addresses and saves.
    std::string diagnostics() const;
    bool connected() const;
    bool finished() const;
    std::uint64_t frames() const;
    std::uint64_t verified_frames() const;
    std::uint16_t port() const;
    std::size_t drain_audio(std::span<std::int16_t>);
    void close();
    static std::string make_room_code();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace matcha

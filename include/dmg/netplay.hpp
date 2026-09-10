#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dmg::netplay {
inline constexpr std::uint16_t protocol_version = 1;
inline constexpr std::size_t max_payload = 1200;
inline constexpr std::size_t send_window = 32;

enum class PacketKind : std::uint8_t { Hello = 1, Inputs = 2, Serial = 3, Hash = 4, Finish = 5, Ack = 6 };
struct Packet {
    PacketKind kind{PacketKind::Inputs};
    std::uint64_t session{}, frame{}, cycle{};
    std::uint32_t sequence{}, ack{};
    std::uint64_t ack_bits{};
    std::uint16_t size{};
    std::array<std::uint8_t, max_payload> payload{};
};
struct Datagram {
    std::array<std::uint8_t, max_payload + 56> bytes{};
    std::size_t size{};
};
[[nodiscard]] bool encode(const Packet &, Datagram &) noexcept;
[[nodiscard]] bool decode(const std::uint8_t *, std::size_t, Packet &) noexcept;
struct Statistics {
    std::uint64_t sent{}, received{}, retransmitted{}, duplicates{}, malformed{}, wrong_session{},
        wrong_sender{}, backpressure{}, socket_errors{};
};

// One real nonblocking UDP socket. Session identifiers isolate stale datagrams;
// they authenticate neither a remote machine nor an untrusted network.
class Channel {
  public:
    Channel(std::uint16_t bind_port, const std::string &peer_ipv4, std::uint16_t peer_port,
            std::uint64_t session, std::uint32_t retransmit_ms = 30,
            const std::string &bind_ipv4 = "127.0.0.1");
    ~Channel();
    Channel(const Channel &) = delete;
    Channel &operator=(const Channel &) = delete;
    [[nodiscard]] std::uint16_t port() const noexcept;
    [[nodiscard]] bool send(PacketKind, std::uint64_t frame, std::uint64_t cycle,
                            const std::uint8_t *, std::size_t);
    // Delivers distinct accepted packets; their order is intentionally not
    // hidden. The deterministic simulation orders events by emulated cycle.
    [[nodiscard]] bool receive(Packet &);
    void service();
    [[nodiscard]] bool drained() const noexcept;
    [[nodiscard]] const Statistics &statistics() const noexcept;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct InputFrame { std::uint64_t frame{}; std::uint8_t buttons{}; };
struct SerialEvent {
    std::uint64_t cycle{}, transfer{};
    std::uint8_t kind{}, data{}, control{}, bit{};
    friend bool operator==(const SerialEvent &, const SerialEvent &) = default;
};
// Canonical payload codecs never expose host padding or byte order on wire.
[[nodiscard]] std::array<std::uint8_t, 9> encode_input(const InputFrame &) noexcept;
[[nodiscard]] bool decode_input(const Packet &, InputFrame &) noexcept;
[[nodiscard]] std::array<std::uint8_t, 20> encode_serial(const SerialEvent &) noexcept;
[[nodiscard]] bool decode_serial(const Packet &, SerialEvent &) noexcept;
} // namespace dmg::netplay

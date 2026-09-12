#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace matcha {

struct FriendTransportOptions {
    bool host = false;
    std::string address = "0.0.0.0";
    std::uint16_t port = 27888;
    std::string code;
};

struct FriendTransportDiagnostics {
    std::uint64_t sent_packets = 0;
    std::uint64_t received_packets = 0;
    std::uint64_t retransmissions = 0;
    std::uint64_t silence_ms = 0;
};

// Reliable ordered messages over real IPv4 UDP. Call poll frequently, including
// while idle, and serialize all access to an instance on the caller's thread.
// Each direction retains at most 1 MiB and 64 queued messages; individual
// messages are at most 256 KiB, including zero-length messages. send() means
// accepted into the bounded queue, not confirmed delivery. close() discards
// pending messages, sends best-effort BYE packets, and releases the socket.
//
// The 32-hex-character room token and all payloads travel in plaintext. This
// is not encryption or authenticated peer identity. Use a trusted LAN/VPN for
// private traffic. Internet hosting requires forwarded UDP or a suitable VPN;
// the transport makes no OS firewall/router changes and does no NAT traversal.
class FriendTransport {
public:
    explicit FriendTransport(FriendTransportOptions options);
    ~FriendTransport();
    FriendTransport(FriendTransport &&) noexcept;
    FriendTransport &operator=(FriendTransport &&) noexcept;
    FriendTransport(const FriendTransport &) = delete;
    FriendTransport &operator=(const FriendTransport &) = delete;

    void poll();
    bool connected() const;
    // True after explicit close, peer departure, connection/liveness timeout,
    // or fatal socket failure. Listening/handshaking are not terminal states.
    bool finished() const;
    std::string status() const;
    // Successful UDP sends, accepted peer packets and repeated DATA sends.
    // Silence is time since the last accepted peer packet (or creation before
    // any peer replies), frozen when the socket closes. No addresses/tokens.
    FriendTransportDiagnostics diagnostics() const;
    std::uint16_t local_port() const;
    bool send(std::span<const std::uint8_t> message);
    bool receive(std::vector<std::uint8_t> &message);
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace matcha

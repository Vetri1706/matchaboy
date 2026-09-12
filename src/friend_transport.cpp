#include "friend_transport.hpp"
#include "dmg/socket_platform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <limits>
#include <map>
#include <random>
#include <stdexcept>
#include <utility>
#ifndef _WIN32
#include <netdb.h>
#endif

namespace matcha {
namespace {
namespace sockets = dmg::netplay::socket_platform;
using Clock = std::chrono::steady_clock;
constexpr std::size_t header_bytes = 60, packet_bytes = 1200;
constexpr std::size_t fragment_bytes = packet_bytes - header_bytes;
constexpr std::size_t message_limit = 256 * 1024, queue_limit = 1024 * 1024, message_window = 64;
constexpr auto retry_interval = std::chrono::milliseconds(100);
constexpr auto hello_interval = std::chrono::milliseconds(250);
constexpr auto ping_interval = std::chrono::milliseconds(500);
constexpr auto liveness_timeout = std::chrono::seconds(10);
constexpr auto join_timeout = std::chrono::seconds(15);
enum class Kind : std::uint8_t { hello = 1, welcome, data, ack, ping, bye, delivered };

void put(std::uint8_t *where, std::uint64_t value, unsigned count) noexcept {
    for (unsigned i = count; i != 0; --i) { where[i - 1] = static_cast<std::uint8_t>(value); value >>= 8; }
}
std::uint64_t get(const std::uint8_t *where, unsigned count) noexcept {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) value = (value << 8) | where[i];
    return value;
}
std::uint32_t crc(std::span<const std::uint8_t> bytes) noexcept {
    std::uint32_t value = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value ^= i >= 56 && i < 60 ? 0 : bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1) ? 0xEDB88320U : 0);
    }
    return ~value;
}
std::size_t fragment_count(std::size_t bytes) noexcept {
    return std::max(std::size_t(1), (bytes + fragment_bytes - 1) / fragment_bytes);
}
std::size_t charge(std::size_t bytes) noexcept { return std::max(std::size_t(1), bytes); }
bool same_endpoint(const sockaddr_in &a, const sockaddr_in &b) noexcept {
    return a.sin_family == b.sin_family && a.sin_port == b.sin_port && a.sin_addr.s_addr == b.sin_addr.s_addr;
}
std::array<std::uint8_t, 16> room_token(const std::string &text) {
    if (text.size() != 32) throw std::invalid_argument("Room code must contain exactly 32 hexadecimal characters.");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        throw std::invalid_argument("Room code must contain only hexadecimal characters.");
    };
    std::array<std::uint8_t, 16> token{};
    for (std::size_t i = 0; i < token.size(); ++i)
        token[i] = static_cast<std::uint8_t>((digit(text[i * 2]) << 4) | digit(text[i * 2 + 1]));
    return token;
}
std::uint64_t nonce() {
    std::random_device random;
    const auto result = (static_cast<std::uint64_t>(random()) << 32) ^ random();
    return result ? result : 1;
}
sockaddr_in resolve(const std::string &address, std::uint16_t port) {
    if (address.empty()) throw std::invalid_argument("IPv4 address or host name is required.");
    addrinfo hints{};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
    addrinfo *result = nullptr;
    const auto service = std::to_string(port);
    const auto error = getaddrinfo(address.c_str(), service.c_str(), &hints, &result);
    if (error || !result) {
        if (result) freeaddrinfo(result);
        throw std::runtime_error("Cannot resolve IPv4 host: " + address);
    }
    const auto value = *reinterpret_cast<const sockaddr_in *>(result->ai_addr);
    freeaddrinfo(result);
    return value;
}

struct Packet {
    Kind kind{};
    std::uint64_t sender{}, recipient{};
    std::uint32_t sequence{}, total{};
    std::uint16_t fragment{}, fragments{};
    std::span<const std::uint8_t> payload;
};
bool decode(std::span<const std::uint8_t> bytes, const std::array<std::uint8_t, 16> &token, Packet &packet) noexcept {
    if (bytes.size() < header_bytes || bytes.size() > packet_bytes || get(bytes.data(), 4) != 0x4D465231U ||
        bytes[4] != 1 || bytes[5] < 1 || bytes[5] > 7 || get(bytes.data() + 6, 2) != header_bytes ||
        !std::equal(token.begin(), token.end(), bytes.begin() + 8) || get(bytes.data() + 54, 2) != 0 ||
        get(bytes.data() + 56, 4) != crc(bytes) || get(bytes.data() + 52, 2) != bytes.size() - header_bytes) return false;
    packet.kind = static_cast<Kind>(bytes[5]);
    packet.sender = get(bytes.data() + 24, 8); packet.recipient = get(bytes.data() + 32, 8);
    packet.sequence = static_cast<std::uint32_t>(get(bytes.data() + 40, 4));
    packet.total = static_cast<std::uint32_t>(get(bytes.data() + 44, 4));
    packet.fragment = static_cast<std::uint16_t>(get(bytes.data() + 48, 2));
    packet.fragments = static_cast<std::uint16_t>(get(bytes.data() + 50, 2));
    packet.payload = bytes.subspan(header_bytes);
    if (!packet.sender) return false;
    if (packet.kind == Kind::data || packet.kind == Kind::ack) {
        if (!packet.recipient || !packet.sequence || packet.total > message_limit ||
            packet.fragments != fragment_count(packet.total) || packet.fragment >= packet.fragments) return false;
        const auto expected = std::min(fragment_bytes, packet.total - packet.fragment * fragment_bytes);
        return packet.payload.size() == (packet.kind == Kind::ack ? 0 : expected);
    }
    if (packet.total || packet.fragment || packet.fragments || !packet.payload.empty()) return false;
    if (packet.kind == Kind::delivered) return packet.recipient && packet.sequence;
    if (packet.sequence) return false;
    return packet.kind == Kind::hello ? packet.recipient == 0 : packet.recipient != 0;
}
} // namespace

struct FriendTransport::Impl {
    struct Outgoing {
        std::uint32_t sequence{};
        std::vector<std::uint8_t> data, acknowledged;
        std::vector<Clock::time_point> sent;
        std::vector<bool> transmitted;
    };
    struct Incoming {
        std::vector<std::uint8_t> data, present;
        std::size_t missing{};
    };
    enum class State { listening, connecting, connected, closed, disconnected, timeout, failed };

    // Winsock lifetime encloses every socket lifetime, including exceptions.
    sockets::Runtime socket_runtime;
    std::unique_ptr<sockets::Socket> socket;
    std::array<std::uint8_t, 16> token;
    bool host;
    sockaddr_in peer{};
    std::uint16_t port{};
    std::uint64_t identity = nonce(), peer_identity{};
    std::uint64_t send_sequence = 1, receive_sequence = 1;
    State state;
    std::string failure;
    Clock::time_point created = Clock::now(), heard = created, hello_sent{}, ping_sent{};
    Clock::time_point stopped_at{};
    std::uint64_t sent_packets{}, received_packets{}, retransmissions{};
    std::deque<Outgoing> outgoing;
    std::map<std::uint32_t, Incoming> assembling;
    std::deque<std::vector<std::uint8_t>> ready;
    std::size_t outgoing_bytes{}, incoming_bytes{};

    explicit Impl(const FriendTransportOptions &options)
        : token(room_token(options.code)), host(options.host), state(host ? State::listening : State::connecting) {
        if (!host && options.port == 0) throw std::invalid_argument("Friend's UDP port must be nonzero.");
        sockaddr_in local{}; local.sin_family = AF_INET;
        if (host) local = resolve(options.address, options.port);
        else peer = resolve(options.address, options.port);
        socket = std::make_unique<sockets::Socket>(sockets::create_datagram());
        if (!socket->valid()) throw std::runtime_error("Cannot create friend UDP socket.");
        if (bind(socket->get(), reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0 ||
            !sockets::nonblocking(socket->get()))
            throw std::runtime_error("Cannot bind friend UDP socket: " + sockets::describe_error(sockets::last_error()));
        sockets::AddressLength length = sizeof(local);
        if (getsockname(socket->get(), reinterpret_cast<sockaddr *>(&local), &length) != 0)
            throw std::runtime_error("Cannot read friend UDP port.");
        port = ntohs(local.sin_port);
    }
    bool write(Kind kind, std::uint32_t sequence = 0, std::uint32_t total = 0,
               std::uint16_t fragment = 0, std::uint16_t fragments = 0,
               std::span<const std::uint8_t> payload = {}) noexcept {
        if (!socket) return false;
        std::array<std::uint8_t, packet_bytes> bytes{};
        put(bytes.data(), 0x4D465231U, 4); bytes[4] = 1; bytes[5] = static_cast<std::uint8_t>(kind);
        put(bytes.data() + 6, header_bytes, 2); std::copy(token.begin(), token.end(), bytes.begin() + 8);
        put(bytes.data() + 24, identity, 8); put(bytes.data() + 32, kind == Kind::hello ? 0 : peer_identity, 8);
        put(bytes.data() + 40, sequence, 4); put(bytes.data() + 44, total, 4);
        put(bytes.data() + 48, fragment, 2); put(bytes.data() + 50, fragments, 2);
        put(bytes.data() + 52, payload.size(), 2);
        std::copy(payload.begin(), payload.end(), bytes.begin() + header_bytes);
        const auto size = header_bytes + payload.size();
        put(bytes.data() + 56, crc(std::span(bytes).first(size)), 4);
        const bool sent = sockets::send_datagram(socket->get(), bytes.data(), size, peer) == static_cast<std::ptrdiff_t>(size);
        if (sent) ++sent_packets;
        return sent;
    }
    void shutdown(State reason) noexcept {
        if (stopped_at == Clock::time_point{}) stopped_at = Clock::now();
        state = reason; socket.reset();
        outgoing.clear(); assembling.clear(); ready.clear();
        outgoing_bytes = incoming_bytes = 0;
    }
    void close() noexcept {
        if (socket && peer_identity)
            for (unsigned attempt = 0; attempt < 3; ++attempt) write(Kind::bye);
        shutdown(State::closed);
    }
    void acknowledge(const Packet &packet) noexcept {
        write(Kind::ack, packet.sequence, packet.total, packet.fragment, packet.fragments);
    }
    void delivered() noexcept {
        if (receive_sequence > 1)
            write(Kind::delivered, static_cast<std::uint32_t>(receive_sequence - 1));
    }
    bool data(const Packet &packet) {
        if (packet.sequence < receive_sequence) {
            // Recently delivered packets still need ACKs when the first ACK
            // was dropped. Older replayed sequence numbers are outside window.
            if (receive_sequence - packet.sequence <= message_window) {
                acknowledge(packet); delivered();
            }
            return false;
        }
        if (packet.sequence - receive_sequence >= message_window) return false;
        auto found = assembling.find(packet.sequence);
        if (found == assembling.end()) {
            if (incoming_bytes + charge(packet.total) > queue_limit || assembling.size() + ready.size() >= message_window)
                return false; // No ACK: sender retains it until the caller drains receive().
            Incoming message;
            message.data.resize(packet.total); message.present.resize(packet.fragments); message.missing = packet.fragments;
            found = assembling.emplace(packet.sequence, std::move(message)).first;
            incoming_bytes += charge(packet.total);
        }
        auto &message = found->second;
        if (message.data.size() != packet.total || message.present.size() != packet.fragments) return false;
        auto destination = std::span(message.data).subspan(packet.fragment * fragment_bytes, packet.payload.size());
        if (message.present[packet.fragment]) {
            if (!std::equal(packet.payload.begin(), packet.payload.end(), destination.begin())) return false;
        } else {
            std::copy(packet.payload.begin(), packet.payload.end(), destination.begin());
            message.present[packet.fragment] = 1; --message.missing;
        }
        acknowledge(packet);
        while (receive_sequence <= std::numeric_limits<std::uint32_t>::max()) {
            auto next = assembling.find(static_cast<std::uint32_t>(receive_sequence));
            if (next == assembling.end() || next->second.missing) break;
            ready.push_back(std::move(next->second.data));
            assembling.erase(next); ++receive_sequence;
        }
        delivered();
        return true;
    }
    void heard_peer(Clock::time_point now) noexcept {
        heard = now;
        ++received_packets;
    }
    void packet(const Packet &packet, const sockaddr_in &source, Clock::time_point now) {
        if (host && packet.kind == Kind::hello) {
            if (state == State::listening) {
                peer = source; peer_identity = packet.sender; state = State::connected;
                heard_peer(now); write(Kind::welcome);
            } else if (state == State::connected && same_endpoint(peer, source) && peer_identity == packet.sender) {
                heard_peer(now); write(Kind::welcome);
            }
            return;
        }
        if (!same_endpoint(peer, source) || packet.recipient != identity) return;
        if (!host && packet.kind == Kind::welcome && state == State::connecting) {
            peer_identity = packet.sender; state = State::connected; heard_peer(now);
            write(Kind::ping);
            return;
        }
        if (state != State::connected || packet.sender != peer_identity) return;
        if (packet.kind == Kind::bye) { heard_peer(now); shutdown(State::disconnected); return; }
        if (packet.kind == Kind::ping || (!host && packet.kind == Kind::welcome)) {
            heard_peer(now); delivered(); return;
        }
        if (packet.kind == Kind::data) {
            if (data(packet)) heard_peer(now);
            return;
        }
        if (packet.kind == Kind::ack) {
            auto found = std::find_if(outgoing.begin(), outgoing.end(), [&](const auto &message) { return message.sequence == packet.sequence; });
            if (found == outgoing.end() || found->data.size() != packet.total ||
                found->acknowledged.size() != packet.fragments || !found->transmitted[packet.fragment]) return;
            found->acknowledged[packet.fragment] = 1; heard_peer(now);
            return;
        }
        if (packet.kind == Kind::delivered) {
            if (packet.sequence >= send_sequence) return;
            // Only a contiguous sequence the sender actually transmitted may
            // release capacity. Fragment ACKs alone do not release later
            // payloads: otherwise they could fill the receiver while a missing
            // earlier message is permanently denied its allocation budget.
            for (const auto &message : outgoing) {
                if (message.sequence > packet.sequence) break;
                if (!std::all_of(message.transmitted.begin(), message.transmitted.end(), [](bool sent) { return sent; })) return;
            }
            while (!outgoing.empty() && outgoing.front().sequence <= packet.sequence) {
                outgoing_bytes -= charge(outgoing.front().data.size()); outgoing.pop_front();
            }
            heard_peer(now);
        }
    }
    void poll() {
        if (!socket) return;
        auto now = Clock::now();
        // Fixed work budget prevents an invalid-packet flood from trapping the
        // host UI in a receive loop. One extra byte detects POSIX truncation.
        for (unsigned i = 0; i < 256 && socket; ++i) {
            std::array<std::uint8_t, packet_bytes + 1> buffer{};
            sockaddr_in source{}; sockets::AddressLength length = sizeof(source);
            const auto received = sockets::receive_datagram(socket->get(), buffer.data(), buffer.size(), source, length);
            if (received.size < 0) {
                const auto error = sockets::last_error();
                if (sockets::temporary_error(error)) break;
#ifdef _WIN32
                // Windows reports ICMP Port Unreachable through recvfrom. A
                // missing peer is diagnosed by the connection/liveness timer.
                if (error == WSAECONNRESET) break;
#endif
                failure = sockets::describe_error(error); shutdown(State::failed); return;
            }
            if (received.truncated || received.size > static_cast<std::ptrdiff_t>(packet_bytes) ||
                length != sizeof(source) || source.sin_family != AF_INET) continue;
            Packet parsed;
            if (decode(std::span(buffer).first(static_cast<std::size_t>(received.size)), token, parsed)) packet(parsed, source, now);
        }
        if (!socket) return;
        now = Clock::now();
        if (state == State::connecting) {
            if (now - created >= join_timeout) { shutdown(State::timeout); return; }
            if (hello_sent == Clock::time_point{} || now - hello_sent >= hello_interval) {
                if (write(Kind::hello)) hello_sent = now;
            }
        }
        if (state != State::connected) return;
        if (now - heard >= liveness_timeout) { shutdown(State::timeout); return; }
        if (ping_sent == Clock::time_point{} || now - ping_sent >= ping_interval) {
            if (write(Kind::ping)) ping_sent = now;
        }
        unsigned budget = 64;
        for (auto &message : outgoing) {
            const bool fragments_acked = std::all_of(message.acknowledged.begin(), message.acknowledged.end(),
                                                    [](auto value) { return value != 0; });
            for (std::size_t fragment = 0; fragment < message.acknowledged.size() && budget; ++fragment) {
                // If the cumulative delivery packet was lost after all fragment
                // ACKs arrived, retry the final fragment to solicit it again.
                if ((message.acknowledged[fragment] && !(fragments_acked && fragment + 1 == message.acknowledged.size())) ||
                    (message.transmitted[fragment] && now - message.sent[fragment] < retry_interval)) continue;
                const auto offset = fragment * fragment_bytes;
                const auto payload = std::span(message.data).subspan(offset, std::min(fragment_bytes, message.data.size() - offset));
                --budget;
                if (write(Kind::data, message.sequence, static_cast<std::uint32_t>(message.data.size()),
                          static_cast<std::uint16_t>(fragment), static_cast<std::uint16_t>(message.acknowledged.size()), payload)) {
                    if (message.transmitted[fragment]) ++retransmissions;
                    message.sent[fragment] = now; message.transmitted[fragment] = true;
                }
            }
            if (!budget) break;
        }
    }
};

FriendTransport::FriendTransport(FriendTransportOptions options) : impl_(std::make_unique<Impl>(options)) {}
FriendTransport::~FriendTransport() { close(); }
FriendTransport::FriendTransport(FriendTransport &&) noexcept = default;
FriendTransport &FriendTransport::operator=(FriendTransport &&other) noexcept {
    if (this != &other) { close(); impl_ = std::move(other.impl_); }
    return *this;
}
void FriendTransport::poll() { if (impl_) impl_->poll(); }
bool FriendTransport::connected() const { return impl_ && impl_->state == Impl::State::connected; }
bool FriendTransport::finished() const {
    return !impl_ || (impl_->state != Impl::State::listening && impl_->state != Impl::State::connecting &&
                      impl_->state != Impl::State::connected);
}
std::uint16_t FriendTransport::local_port() const { return impl_ ? impl_->port : 0; }
std::string FriendTransport::status() const {
    if (!impl_) return "Closed";
    switch (impl_->state) {
    case Impl::State::listening: return "Waiting for friend on UDP " + std::to_string(impl_->port);
    case Impl::State::connecting: return "Connecting to friend";
    case Impl::State::connected: return "Connected";
    case Impl::State::closed: return "Closed";
    case Impl::State::disconnected: return "Friend disconnected";
    case Impl::State::timeout:
        return impl_->peer_identity
            ? "Friend connection lost: no valid peer packets for 10 seconds. Check both apps are open and the network is reachable, then reconnect."
            : "Connection timed out; check the host address, room code and UDP forwarding";
    case Impl::State::failed: return "UDP error: " + impl_->failure;
    }
    return "Closed";
}
FriendTransportDiagnostics FriendTransport::diagnostics() const {
    if (!impl_) return {};
    const auto end = impl_->stopped_at == Clock::time_point{} ? Clock::now() : impl_->stopped_at;
    const auto silence = std::chrono::duration_cast<std::chrono::milliseconds>(end - impl_->heard).count();
    return {impl_->sent_packets, impl_->received_packets, impl_->retransmissions,
            static_cast<std::uint64_t>(std::max<std::int64_t>(0, silence))};
}
bool FriendTransport::send(std::span<const std::uint8_t> message) {
    if (!connected() || message.size() > message_limit || impl_->outgoing.size() >= message_window ||
        impl_->outgoing_bytes + charge(message.size()) > queue_limit || impl_->send_sequence > std::numeric_limits<std::uint32_t>::max() ||
        (!impl_->outgoing.empty() && impl_->send_sequence - impl_->outgoing.front().sequence >= message_window)) return false;
    Impl::Outgoing pending;
    pending.sequence = static_cast<std::uint32_t>(impl_->send_sequence);
    pending.data.assign(message.begin(), message.end());
    const auto count = fragment_count(message.size());
    pending.acknowledged.resize(count); pending.sent.resize(count); pending.transmitted.resize(count);
    impl_->outgoing.push_back(std::move(pending)); impl_->outgoing_bytes += charge(message.size()); ++impl_->send_sequence;
    return true;
}
bool FriendTransport::receive(std::vector<std::uint8_t> &message) {
    if (!impl_ || impl_->ready.empty()) return false;
    impl_->incoming_bytes -= charge(impl_->ready.front().size());
    message = std::move(impl_->ready.front()); impl_->ready.pop_front();
    return true;
}
void FriendTransport::close() noexcept { if (impl_) impl_->close(); }

} // namespace matcha

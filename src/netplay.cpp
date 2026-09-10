#include "dmg/netplay.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace dmg::netplay {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t magic = 0x444e504cU;
void put(std::uint8_t *p, std::uint64_t v, unsigned n) noexcept {
    for (unsigned i = n; i != 0; --i) { p[i - 1] = static_cast<std::uint8_t>(v); v >>= 8U; }
}
std::uint64_t get(const std::uint8_t *p, unsigned n) noexcept {
    std::uint64_t v = 0;
    for (unsigned i = 0; i < n; ++i) v = (v << 8U) | p[i];
    return v;
}
std::uint32_t checksum(const std::uint8_t *p, std::size_t size) noexcept {
    std::uint32_t crc = 0xffffffffU;
    for (std::size_t i = 0; i < size; ++i) {
        // The checksum field is logically zero during calculation.
        crc ^= i >= 52 && i < 56 ? 0U : p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1U) ^ ((crc & 1U) != 0 ? 0xedb88320U : 0U);
    }
    return ~crc;
}
bool newer(std::uint32_t a, std::uint32_t b) noexcept {
    return a != b && static_cast<std::uint32_t>(a - b) < 0x80000000U;
}
bool known_kind(std::uint8_t k) noexcept {
    return k >= static_cast<std::uint8_t>(PacketKind::Hello) &&
           k <= static_cast<std::uint8_t>(PacketKind::Ack);
}
} // namespace

bool encode(const Packet &p, Datagram &d) noexcept {
    if (p.size > max_payload || !known_kind(static_cast<std::uint8_t>(p.kind)) || p.session == 0 ||
        (p.kind == PacketKind::Ack ? p.sequence != 0 || p.size != 0 : p.sequence == 0)) return false;
    d.size = 56U + p.size;
    auto *b = d.bytes.data();
    put(b, magic, 4); put(b + 4, protocol_version, 2);
    b[6] = static_cast<std::uint8_t>(p.kind); b[7] = 0;
    put(b + 8, p.session, 8); put(b + 16, p.sequence, 4); put(b + 20, p.ack, 4);
    put(b + 24, p.ack_bits, 8); put(b + 32, p.frame, 8); put(b + 40, p.cycle, 8);
    put(b + 48, p.size, 2); put(b + 50, 0, 2); put(b + 52, 0, 4);
    std::copy_n(p.payload.begin(), p.size, d.bytes.begin() + 56);
    put(b + 52, checksum(b, d.size), 4);
    return true;
}
bool decode(const std::uint8_t *b, std::size_t n, Packet &p) noexcept {
    if (b == nullptr || n < 56 || n > max_payload + 56 || get(b, 4) != magic ||
        get(b + 4, 2) != protocol_version || b[7] != 0 || get(b + 50, 2) != 0 ||
        !known_kind(b[6]) || get(b + 48, 2) != n - 56 || get(b + 8, 8) == 0 ||
        get(b + 52, 4) != checksum(b, n)) return false;
    const auto kind = static_cast<PacketKind>(b[6]);
    const auto sequence = static_cast<std::uint32_t>(get(b + 16, 4));
    if (kind == PacketKind::Ack ? sequence != 0 || n != 56 : sequence == 0) return false;
    p.kind = kind; p.session = get(b + 8, 8); p.sequence = sequence;
    p.ack = static_cast<std::uint32_t>(get(b + 20, 4)); p.ack_bits = get(b + 24, 8);
    p.frame = get(b + 32, 8); p.cycle = get(b + 40, 8);
    p.size = static_cast<std::uint16_t>(n - 56);
    std::copy_n(b + 56, p.size, p.payload.begin());
    return true;
}

struct Channel::Impl {
    struct Pending { Packet packet{}; Clock::time_point sent{}; bool used{}, transmitted{}; };
    int fd{-1};
    sockaddr_in peer{};
    std::uint16_t local_port{};
    std::uint64_t session{};
    std::uint32_t next{1}, latest{};
    std::uint64_t mask{};
    std::chrono::milliseconds retry;
    std::array<Pending, send_window> pending{};
    Statistics stats{};
    Impl(std::uint16_t port, const std::string &address, std::uint16_t remote,
         std::uint64_t id, std::uint32_t interval, const std::string &bind_address) : session(id), retry(interval) {
        if (session == 0 || remote == 0 || interval == 0)
            throw std::invalid_argument("UDP session, peer port and retry interval must be nonzero");
        peer.sin_family = AF_INET; peer.sin_port = htons(remote);
        if (inet_pton(AF_INET, address.c_str(), &peer.sin_addr) != 1)
            throw std::invalid_argument("UDP peer must be a numeric IPv4 address");
        fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) throw std::runtime_error("UDP socket: " + std::string(std::strerror(errno)));
        sockaddr_in local{}; local.sin_family = AF_INET; local.sin_port = htons(port);
        if (inet_pton(AF_INET, bind_address.c_str(), &local.sin_addr) != 1) {
            close(fd); fd = -1; throw std::invalid_argument("invalid numeric bind IPv4 address");
        }
        // Explicit bind avoids silently exposing a test peer on all interfaces.
        // Internet deployments require a deliberate interface/routing policy.
        if (bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) != 0 ||
            fcntl(fd, F_SETFL, O_NONBLOCK) != 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) {
            const auto reason = std::string(std::strerror(errno)); close(fd); fd = -1;
            throw std::runtime_error("UDP bind/nonblocking: " + reason);
        }
        socklen_t length = sizeof(local);
        if (getsockname(fd, reinterpret_cast<sockaddr *>(&local), &length) != 0) {
            const auto reason = std::string(std::strerror(errno)); close(fd); fd = -1;
            throw std::runtime_error("UDP getsockname: " + reason);
        }
        local_port = ntohs(local.sin_port);
    }
    ~Impl() { if (fd >= 0) close(fd); }
    bool transmit(Packet &p) {
        p.ack = latest; p.ack_bits = mask;
        Datagram d;
        if (!encode(p, d)) throw std::logic_error("invalid outgoing UDP packet");
        const auto sent = sendto(fd, d.bytes.data(), d.size, 0,
                                 reinterpret_cast<const sockaddr *>(&peer), sizeof(peer));
        if (sent == static_cast<ssize_t>(d.size)) { ++stats.sent; return true; }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return false;
        ++stats.socket_errors;
        return false;
    }
    void acknowledge(const Packet &p) {
        if (p.ack == 0) return;
        for (auto &slot : pending) {
            if (!slot.used) continue;
            const auto sequence = slot.packet.sequence;
            const auto distance = static_cast<std::uint32_t>(p.ack - sequence);
            if (sequence == p.ack || (distance >= 1 && distance <= 64 &&
                                     ((p.ack_bits >> (distance - 1U)) & 1U) != 0)) slot.used = false;
        }
    }
    bool accept(std::uint32_t sequence) {
        if (latest == 0) { latest = sequence; mask = 0; return true; }
        if (newer(sequence, latest)) {
            const auto distance = static_cast<std::uint32_t>(sequence - latest);
            mask = distance > 64 ? 0 : distance == 64 ? (std::uint64_t{1} << 63U)
                : (mask << distance) | (std::uint64_t{1} << (distance - 1U));
            latest = sequence;
            return true;
        }
        const auto distance = static_cast<std::uint32_t>(latest - sequence);
        if (distance == 0 || distance > 64) return false;
        const auto flag = std::uint64_t{1} << (distance - 1U);
        if ((mask & flag) != 0) return false;
        mask |= flag;
        return true;
    }
    void send_ack() {
        Packet ack; ack.kind = PacketKind::Ack; ack.session = session;
        static_cast<void>(transmit(ack));
    }
};
Channel::Channel(std::uint16_t port, const std::string &peer, std::uint16_t remote,
                 std::uint64_t session, std::uint32_t retry, const std::string &bind_address)
    : impl_(std::make_unique<Impl>(port, peer, remote, session, retry, bind_address)) {}
Channel::~Channel() = default;
std::uint16_t Channel::port() const noexcept { return impl_->local_port; }
const Statistics &Channel::statistics() const noexcept { return impl_->stats; }
bool Channel::drained() const noexcept {
    return std::none_of(impl_->pending.begin(), impl_->pending.end(), [](const auto &p) { return p.used; });
}
bool Channel::send(PacketKind kind, std::uint64_t frame, std::uint64_t cycle,
                   const std::uint8_t *payload, std::size_t size) {
    auto &s = *impl_;
    if (kind == PacketKind::Ack || !known_kind(static_cast<std::uint8_t>(kind)) || size > max_payload ||
        (size != 0 && payload == nullptr)) throw std::invalid_argument("invalid UDP payload");
    for (const auto &p : s.pending)
        if (p.used && static_cast<std::uint32_t>(s.next - p.packet.sequence) >= 63) {
            ++s.stats.backpressure; return false;
        }
    auto free = std::find_if(s.pending.begin(), s.pending.end(), [](const auto &p) { return !p.used; });
    if (free == s.pending.end()) { ++s.stats.backpressure; return false; }
    auto &p = free->packet;
    p.kind = kind; p.session = s.session; p.sequence = s.next++; p.frame = frame; p.cycle = cycle;
    if (s.next == 0) ++s.next;
    p.size = static_cast<std::uint16_t>(size);
    if (size != 0) std::copy_n(payload, size, p.payload.begin());
    free->used = true; free->sent = Clock::now(); free->transmitted = s.transmit(p);
    return true;
}
void Channel::service() {
    auto &s = *impl_;
    const auto now = Clock::now();
    for (auto &p : s.pending) {
        if (!p.used || (p.transmitted && now - p.sent < s.retry)) continue;
        if (s.transmit(p.packet)) {
            if (p.transmitted) ++s.stats.retransmitted;
            p.transmitted = true; p.sent = now;
        }
    }
}
bool Channel::receive(Packet &out) {
    auto &s = *impl_;
    service();
    // A hostile sender cannot monopolize one simulation polling call.
    for (unsigned budget = 0; budget < 128; ++budget) {
        std::array<std::uint8_t, max_payload + 57> bytes{};
        sockaddr_in sender{}; socklen_t length = sizeof(sender);
        const auto count = recvfrom(s.fd, bytes.data(), bytes.size(), 0,
                                    reinterpret_cast<sockaddr *>(&sender), &length);
        if (count < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) ++s.stats.socket_errors;
            return false;
        }
        ++s.stats.received;
        if (length < sizeof(sender) || sender.sin_family != AF_INET ||
            sender.sin_port != s.peer.sin_port || sender.sin_addr.s_addr != s.peer.sin_addr.s_addr) {
            ++s.stats.wrong_sender; continue;
        }
        Packet p;
        if (!decode(bytes.data(), static_cast<std::size_t>(count), p)) { ++s.stats.malformed; continue; }
        if (p.session != s.session) { ++s.stats.wrong_session; continue; }
        s.acknowledge(p);
        if (p.kind == PacketKind::Ack) continue;
        const bool accepted = s.accept(p.sequence);
        s.send_ack();
        if (!accepted) { ++s.stats.duplicates; continue; }
        out = p;
        return true;
    }
    return false;
}
std::array<std::uint8_t, 9> encode_input(const InputFrame &p) noexcept {
    std::array<std::uint8_t, 9> out{}; put(out.data(), p.frame, 8); out[8] = p.buttons; return out;
}
bool decode_input(const Packet &p, InputFrame &out) noexcept {
    if (p.kind != PacketKind::Inputs || p.size != 9 || get(p.payload.data(), 8) != p.frame) return false;
    out.frame = p.frame; out.buttons = p.payload[8]; return true;
}
std::array<std::uint8_t, 20> encode_serial(const SerialEvent &e) noexcept {
    std::array<std::uint8_t, 20> out{}; put(out.data(), e.cycle, 8); put(out.data() + 8, e.transfer, 8);
    out[16] = e.kind; out[17] = e.data; out[18] = e.control; out[19] = e.bit; return out;
}
bool decode_serial(const Packet &p, SerialEvent &e) noexcept {
    if (p.kind != PacketKind::Serial || p.size != 20 || get(p.payload.data(), 8) != p.cycle ||
        p.payload[19] > 1 || p.payload[16] > 2) return false;
    e.cycle = p.cycle; e.transfer = get(p.payload.data() + 8, 8); e.kind = p.payload[16];
    e.data = p.payload[17]; e.control = p.payload[18]; e.bit = p.payload[19]; return true;
}
} // namespace dmg::netplay

#include "dmg/netplay.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <source_location>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace dmg::netplay;
namespace {
void check(bool value, std::source_location where = std::source_location::current()) { if (!value) throw std::runtime_error("netplay assertion failed at line " + std::to_string(where.line())); }
std::uint16_t free_port() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0); check(fd >= 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0);
    socklen_t size = sizeof(a); check(getsockname(fd, reinterpret_cast<sockaddr *>(&a), &size) == 0);
    close(fd); return ntohs(a.sin_port);
}
void transmit(int fd, std::uint16_t port, const Packet &packet) {
    Datagram data; check(encode(packet, data));
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    check(sendto(fd, data.bytes.data(), data.size, 0, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
          static_cast<ssize_t>(data.size));
}
void receive_datagrams(Channel &channel, std::uint64_t count) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    Packet packet;
    while (channel.statistics().received < count && std::chrono::steady_clock::now() < deadline) {
        while (channel.receive(packet)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(channel.statistics().received >= count);
}
}
int main() {
    try {
        Packet p; p.session = 0x123456789abcdef0ULL; p.sequence = 0xffffffffU;
        p.ack = 0x11223344; p.ack_bits = 0xfedcba9876543210ULL; p.frame = 0x1020304050607080ULL;
        p.cycle = 0x8877665544332211ULL; p.size = 3; p.payload[0] = 0x80; p.payload[2] = 0xff;
        Datagram d; check(encode(p, d)); check(d.size == 59 && d.bytes[16] == 0xff && d.bytes[8] == 0x12);
        Packet q; check(decode(d.bytes.data(), d.size, q));
        check(q.sequence == p.sequence && q.frame == p.frame && q.cycle == p.cycle && q.ack_bits == p.ack_bits);
        for (std::size_t bit = 0; bit < d.size * 8; ++bit) {
            auto corrupt = d; corrupt.bytes[bit / 8] ^= static_cast<std::uint8_t>(1U << (bit % 8));
            check(!decode(corrupt.bytes.data(), corrupt.size, q));
        }
        for (std::size_t n = 0; n < d.size; ++n) check(!decode(d.bytes.data(), n, q));
        auto a_port = free_port(), b_port = free_port(); while (b_port == a_port) b_port = free_port();
        Channel a(a_port, "127.0.0.1", b_port, 7, 1), b(b_port, "127.0.0.1", a_port, 7, 1);
        std::array<bool, 100> seen{};
        unsigned next = 0, received = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((received != 100 || !a.drained()) && std::chrono::steady_clock::now() < deadline) {
            while (next < 100) {
                const auto input = encode_input(InputFrame{next, static_cast<std::uint8_t>(next)});
                if (!a.send(PacketKind::Inputs, next, next * 70224ULL, input.data(), input.size())) break;
                ++next;
            }
            while (b.receive(q)) {
                InputFrame input; check(decode_input(q, input)); check(input.frame < seen.size());
                check(!seen[input.frame] && input.buttons == input.frame); seen[input.frame] = true; ++received;
            }
            while (a.receive(q)) {}
            a.service(); b.service(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(received == 100 && a.drained()); check(a.statistics().backpressure != 0);
        // A delayed first receive causes actual socket retransmission and deduplication.
        const auto input = encode_input(InputFrame{100, 3});
        check(a.send(PacketKind::Inputs, 100, 100 * 70224ULL, input.data(), input.size()));
        const auto duplicate_before = b.statistics().duplicates;
        const auto retry_before = a.statistics().retransmitted;
        const auto retry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        unsigned copies = 0;
        while (b.statistics().duplicates == duplicate_before && std::chrono::steady_clock::now() < retry_deadline) {
            a.service(); while (b.receive(q)) ++copies;
            // Leave ACKs unread until a genuine retransmission has traversed
            // the kernel socket queues; scheduling is not assumed to be 3ms.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        while (a.receive(q)) {}
        check(copies == 1 && b.statistics().duplicates > duplicate_before && a.statistics().retransmitted > retry_before);
        const auto raw_port = free_port();
        const int raw = socket(AF_INET, SOCK_DGRAM, 0); check(raw >= 0);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(raw_port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(bind(raw, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
        Channel wrap(0, "127.0.0.1", raw_port, 9);
        Packet wire; wire.session = 9;
        for (const auto sequence : {0xfffffffeU, 1U, 0xffffffffU, 1U, 0xfffffe00U}) {
            wire.sequence = sequence; transmit(raw, wrap.port(), wire);
        }
        std::array<std::uint32_t, 3> delivered{}; unsigned count = 0;
        const auto wrap_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (wrap.statistics().received < 5 && std::chrono::steady_clock::now() < wrap_deadline) {
            while (wrap.receive(q)) { check(count < delivered.size()); delivered[count++] = q.sequence; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(count == 3 && delivered == std::array<std::uint32_t, 3>{0xfffffffeU, 1U, 0xffffffffU});
        check(wrap.statistics().duplicates == 2);
        check(wrap.send(PacketKind::Inputs, 0, 0, nullptr, 0));
        check(wrap.send(PacketKind::Inputs, 1, 0, nullptr, 0));
        wire.kind = PacketKind::Ack; wire.sequence = 0; wire.ack = 1;
        transmit(raw, wrap.port(), wire); receive_datagrams(wrap, 6);
        check(!wrap.drained());
        // An old/wrapped ACK cannot retire the remaining new sequence 2.
        wire.ack = 0xffffffffU; transmit(raw, wrap.port(), wire); receive_datagrams(wrap, 7);
        check(!wrap.drained());
        wire.ack = 2; transmit(raw, wrap.port(), wire); receive_datagrams(wrap, 8);
        check(wrap.drained()); close(raw);
        std::cout << "PASS netplay wire corruption, real UDP delivery, backpressure, retransmission, duplicates, sequence wrap and stale ACKs\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}

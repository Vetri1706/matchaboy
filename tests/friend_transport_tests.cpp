#include "friend_transport.hpp"
#include "dmg/socket_platform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using matcha::FriendTransport;
using matcha::FriendTransportOptions;
namespace sockets = dmg::netplay::socket_platform;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr std::size_t maximum_message = 256U * 1024U;
constexpr std::size_t maximum_queued = 1024U * 1024U;
const std::string token = "0123456789abcdef0123456789ABCDEF";

void check(bool success, const std::string &message,
           std::source_location location = std::source_location::current()) {
    if (!success) throw std::runtime_error(message + " (line " + std::to_string(location.line()) + ")");
}

FriendTransportOptions options(bool host, std::uint16_t port, const std::string &code = token,
                               const std::string &address = "127.0.0.1") {
    FriendTransportOptions value;
    value.host = host;
    value.address = address;
    value.port = port;
    value.code = code;
    return value;
}

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

bool same_endpoint(const sockaddr_in &a, const sockaddr_in &b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

class RawSocket {
    sockets::Socket socket_{sockets::create_datagram()};
    std::uint16_t port_{};
public:
    RawSocket() {
        check(socket_.valid(), "create test UDP socket");
        check(sockets::nonblocking(socket_.get()), "make test UDP socket nonblocking");
        auto address = loopback(0);
        check(bind(socket_.get(), reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0,
              "bind test UDP socket");
        sockets::AddressLength length = sizeof(address);
        check(getsockname(socket_.get(), reinterpret_cast<sockaddr *>(&address), &length) == 0,
              "query test UDP socket port");
        port_ = ntohs(address.sin_port);
    }
    std::uint16_t port() const { return port_; }
    void send(std::span<const std::uint8_t> bytes, const sockaddr_in &destination) {
        check(sockets::send_datagram(socket_.get(), bytes.data(), bytes.size(), destination) ==
                  static_cast<std::ptrdiff_t>(bytes.size()), "send real UDP test datagram");
    }
    bool receive(std::vector<std::uint8_t> &bytes, sockaddr_in &source) {
        std::array<std::uint8_t, 65536> buffer{};
        sockets::AddressLength length = sizeof(source);
        const auto result = sockets::receive_datagram(socket_.get(), buffer.data(), buffer.size(), source, length);
        if (result.size < 0) {
            check(sockets::temporary_error(sockets::last_error()), "read real UDP test datagram");
            return false;
        }
        check(!result.truncated, "test socket receive buffer holds complete datagram");
        bytes.assign(buffer.begin(), buffer.begin() + result.size);
        return true;
    }
};

// This relay manipulates real kernel UDP datagrams. Both endpoints execute the
// production handshake, fragmentation, retransmission and receive state machines.
class LossyRelay {
    struct Delayed {
        std::vector<std::uint8_t> bytes;
        sockaddr_in destination;
        Clock::time_point due;
        std::uint64_t sequence;
    };
    RawSocket socket_;
    sockaddr_in host_;
    sockaddr_in client_{};
    bool has_client_{};
    std::deque<Delayed> delayed_;
    std::uint64_t sequence_{};
    std::uint64_t last_forwarded_{};
    bool faults_;
public:
    struct Captured { std::vector<std::uint8_t> bytes; bool to_host; };
    std::vector<Captured> captured;
    bool capture{};
    bool blackhole{};
    bool withhold_host_first_message{};
    std::uint64_t withheld_fragments{};
    std::uint64_t dropped{};
    std::uint64_t reordered{};
    std::size_t largest_datagram{};
    explicit LossyRelay(std::uint16_t host_port, bool faults = true) : host_(loopback(host_port)), faults_(faults) {}
    std::uint16_t port() const { return socket_.port(); }
    void reset_client() {
        delayed_.clear();
        captured.clear();
        capture = false;
        std::vector<std::uint8_t> bytes;
        sockaddr_in source{};
        while (socket_.receive(bytes, source)) {}
        has_client_ = false;
    }
    void replay(const std::vector<Captured> &history) {
        check(has_client_, "replay targets the replacement connection");
        for (const auto &entry : history) socket_.send(entry.bytes, entry.to_host ? host_ : client_);
    }
    void poll() {
        std::vector<std::uint8_t> bytes;
        sockaddr_in source{};
        while (socket_.receive(bytes, source)) {
            largest_datagram = std::max(largest_datagram, bytes.size());
            check(bytes.size() <= 1200, "wire datagrams fit the 1200-byte fragmentation bound");
            sockaddr_in destination{};
            if (same_endpoint(source, host_)) {
                check(has_client_, "relay learned client before host reply");
                destination = client_;
            } else {
                if (!has_client_) { client_ = source; has_client_ = true; }
                check(same_endpoint(source, client_), "relay receives only its two real peers");
                destination = host_;
            }
            if (capture) captured.push_back({bytes, same_endpoint(destination, host_)});
            if (blackhole) { ++dropped; continue; }
            // MFR1 DATA message sequence is network-order bytes 40..43.
            // Withhold every fragment of the first message while allowing all
            // later messages and ACKs to traverse real sockets normally.
            if (withhold_host_first_message && same_endpoint(source, host_) && bytes.size() >= 60 &&
                bytes[5] == 3 && bytes[40] == 0 && bytes[41] == 0 && bytes[42] == 0 && bytes[43] == 1) {
                ++withheld_fragments;
                continue;
            }
            ++sequence_;
            if (faults_ && sequence_ % 20 == 0) { ++dropped; continue; }
            // Alternating delays ensure later datagrams overtake earlier ones.
            const auto delay = std::chrono::milliseconds(faults_ ? (sequence_ * 7) % 13 : 0);
            delayed_.push_back({bytes, destination, Clock::now() + delay, sequence_});
        }
        const auto now = Clock::now();
        for (auto it = delayed_.begin(); it != delayed_.end();) {
            if (it->due > now) { ++it; continue; }
            socket_.send(it->bytes, it->destination);
            if (it->sequence < last_forwarded_) ++reordered;
            last_forwarded_ = std::max(last_forwarded_, it->sequence);
            it = delayed_.erase(it);
        }
    }
};

template<class Predicate, class Service>
void until(Predicate complete, Service service, std::chrono::seconds timeout, const std::string &message) {
    const auto deadline = Clock::now() + timeout;
    while (!complete() && Clock::now() < deadline) {
        service();
        std::this_thread::sleep_for(1ms);
    }
    check(complete(), message);
}

void connect(FriendTransport &host, FriendTransport &join) {
    until([&] { return host.connected() && join.connected(); },
          [&] { host.poll(); join.poll(); }, 5s,
          "two real UDP peers connect: " + host.status() + "; " + join.status());
}

void service_for(FriendTransport &a, FriendTransport &b, std::chrono::milliseconds duration) {
    const auto deadline = Clock::now() + duration;
    do { a.poll(); b.poll(); std::this_thread::sleep_for(1ms); } while (Clock::now() < deadline);
}

std::vector<std::uint8_t> payload(std::size_t size, std::uint32_t seed) {
    std::vector<std::uint8_t> result(size);
    for (auto &byte : result) {
        seed = seed * 1664525U + 1013904223U;
        byte = static_cast<std::uint8_t>(seed >> 24);
    }
    return result;
}

void invalid_configuration() {
    for (const auto &invalid : std::array<std::string, 6>{"", "0", std::string(31, 'a'),
                                                        std::string(33, 'a'), std::string(32, 'g'),
                                                        "0123456789abcdef0123456789abcde "}) {
        bool rejected = false;
        try { FriendTransport peer(options(true, 0, invalid)); }
        catch (const std::exception &) { rejected = true; }
        check(rejected, "reject malformed 128-bit connection code");
    }
    FriendTransport host(options(true, 0));
    check(host.local_port() != 0 && !host.connected() && !host.finished(), "host binds an ephemeral real port and waits");
    check(!host.status().empty(), "host exposes a connection status");
    const auto bytes = payload(10, 4);
    check(!host.send(bytes), "disconnected send is rejected");
    host.close();
    host.close();
    host.poll();
    check(!host.connected() && host.finished() && !host.send(bytes), "close is terminal/idempotent and prevents new sends");
    std::vector<std::uint8_t> received;
    check(!host.receive(received), "closed transport has no invented messages");
}

void ordered_exchange() {
    FriendTransport host(options(true, 0));
    FriendTransport join(options(false, host.local_port(), "0123456789abcdef0123456789abcdef", "localhost"));
    check(!join.finished() && !join.connected(), "initial handshake is not a terminal state");
    check(join.local_port() != 0, "join binds an ephemeral real UDP port");
    connect(host, join);
    check(!host.finished() && !join.finished(), "connected transports remain active");
    check(host.diagnostics().sent_packets && join.diagnostics().sent_packets &&
          host.diagnostics().received_packets && join.diagnostics().received_packets,
          "diagnostics count successful handshake sends and accepted peer replies");
    std::vector<std::vector<std::uint8_t>> outbound_host{{}}, outbound_join{{}};
    for (std::uint32_t i = 1; i <= 40; ++i) {
        outbound_host.push_back(payload(i * 131, i));
        outbound_join.push_back(payload(i * 73, i + 100));
    }
    std::size_t sent_host = 0, sent_join = 0, received_host = 0, received_join = 0;
    until([&] { return received_host == outbound_join.size() && received_join == outbound_host.size(); }, [&] {
        if (sent_host < outbound_host.size() && host.send(outbound_host[sent_host])) ++sent_host;
        if (sent_join < outbound_join.size() && join.send(outbound_join[sent_join])) ++sent_join;
        host.poll(); join.poll();
        std::vector<std::uint8_t> bytes;
        while (host.receive(bytes)) {
            check(received_host < outbound_join.size() && bytes == outbound_join[received_host],
                  "host receives exactly once and in sender order");
            ++received_host;
        }
        while (join.receive(bytes)) {
            check(received_join < outbound_host.size() && bytes == outbound_host[received_join],
                  "join receives exactly once and in sender order");
            ++received_join;
        }
    }, 10s, "bidirectional ordered payloads including empty messages arrive");
    service_for(host, join, 100ms);
    std::vector<std::uint8_t> bytes;
    check(!host.receive(bytes) && !join.receive(bytes), "retries do not duplicate delivered messages");
    join.close();
    until([&] { return !host.connected(); }, [&] { host.poll(); }, 3s, "BYE disconnects the real host");
    check(host.finished() && join.finished(), "explicit close and peer departure are terminal states");
    check(!host.send(payload(1, 1)), "peer departure rejects new messages");
}

void capacity_and_recovery() {
    FriendTransport host(options(true, 0));
    FriendTransport join(options(false, host.local_port()));
    connect(host, join);
    check(!host.send(payload(maximum_message + 1, 77)), "oversized application message is rejected");
    const auto large = payload(maximum_message, 99);
    std::size_t accepted = 0;
    // Do not poll the receiver yet: no ACKs can retire this outstanding data.
    while (accepted < 8 && host.send(large)) ++accepted;
    check(accepted > 0 && accepted * maximum_message <= maximum_queued,
          "send backpressure bounds outstanding payload bytes to one MiB");
    check(!host.send(large), "full send queue remains bounded");
    std::size_t received = 0;
    until([&] { return received == accepted; }, [&] {
        host.poll(); join.poll();
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) { check(bytes == large, "maximum-size payload arrives intact"); ++received; }
    }, 15s, "bounded send queue drains without losing accepted payloads");
    const auto tail = payload(97, 107);
    bool submitted = false, delivered = false;
    until([&] { return delivered; }, [&] {
        host.poll(); join.poll();
        if (!submitted) submitted = host.send(tail);
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) { check(!delivered && bytes == tail, "backpressure recovery keeps message order"); delivered = true; }
    }, 5s, "send resumes after acknowledgement frees capacity");
}

void bounded_receive_queue() {
    FriendTransport host(options(true, 0));
    FriendTransport join(options(false, host.local_port()));
    connect(host, join);
    const auto large = payload(maximum_message, 456);
    std::size_t accepted = 0;
    const auto deadline = Clock::now() + 500ms;
    do {
        if (host.send(large)) ++accepted;
        host.poll(); join.poll();
        check(accepted * maximum_message <= 2 * maximum_queued,
              "unconsumed receive queue and unacknowledged send queue remain bounded together");
        std::this_thread::sleep_for(1ms);
    } while (Clock::now() < deadline);
    check(accepted > 0, "queue saturation test enqueued real messages");
    std::size_t received = 0;
    until([&] { return received == accepted; }, [&] {
        host.poll(); join.poll();
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) {
            check(received < accepted && bytes == large, "receiver resumes without losing accepted messages");
            ++received;
        }
    }, 15s, "draining saturated receiver releases backpressure without a message gap");
}

void bounded_empty_message_queue() {
    FriendTransport host(options(true, 0));
    FriendTransport join(options(false, host.local_port()));
    connect(host, join);
    const std::vector<std::uint8_t> empty;
    std::size_t accepted = 0;
    while (accepted < 65 && host.send(empty)) ++accepted;
    check(accepted == 64 && !host.send(empty), "zero-length messages still obey the 64-message outstanding limit");
    std::size_t received = 0;
    until([&] { return received == accepted; }, [&] {
        host.poll(); join.poll();
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) {
            check(received < accepted && bytes.empty(), "empty messages arrive once each without a phantom payload");
            ++received;
        }
    }, 5s, "full empty-message queue drains normally");
}

void stale_connection_datagrams() {
    std::uint16_t host_port = 0;
    std::unique_ptr<LossyRelay> relay;
    std::vector<LossyRelay::Captured> previous;
    {
        FriendTransport host(options(true, 0));
        host_port = host.local_port();
        relay = std::make_unique<LossyRelay>(host_port, false);
        relay->capture = true;
        FriendTransport join(options(false, relay->port()));
        const auto service = [&] { relay->poll(); host.poll(); join.poll(); relay->poll(); };
        until([&] { return host.connected() && join.connected(); }, service, 5s,
              "original connection handshake through stable relay endpoint");
        const auto old_host = payload(2700, 700), old_join = payload(2900, 701);
        check(host.send(old_host) && join.send(old_join), "original peers exchange fragmentable payloads");
        bool got_host = false, got_join = false;
        until([&] { return got_host && got_join; }, [&] {
            service();
            std::vector<std::uint8_t> bytes;
            while (host.receive(bytes)) { check(!got_host && bytes == old_join, "original host payload"); got_host = true; }
            while (join.receive(bytes)) { check(!got_join && bytes == old_host, "original join payload"); got_join = true; }
        }, 5s, "original connection messages arrive before capturing replay evidence");
        previous = relay->captured;
        check(previous.size() >= 6, "capture includes original handshake and fragmented application datagrams");
        host.close(); join.close();
        relay->reset_client();
    }
    FriendTransport host(options(true, host_port));
    FriendTransport join(options(false, relay->port()));
    const auto service = [&] { relay->poll(); host.poll(); join.poll(); relay->poll(); };
    until([&] { return host.connected() && join.connected(); }, service, 5s,
          "replacement peers establish fresh connection nonces at the same host and relay addresses");
    relay->replay(previous);
    const auto deadline = Clock::now() + 150ms;
    do { service(); std::this_thread::sleep_for(1ms); } while (Clock::now() < deadline);
    std::vector<std::uint8_t> bytes;
    check(!host.receive(bytes) && !join.receive(bytes), "previous-session packets from selected source endpoints are rejected");
    check(host.connected() && join.connected(), "stale handshake and ACK packets cannot replace current connection state");
    const auto fresh = payload(1511, 999);
    check(host.send(fresh), "fresh generation remains writable after stale packet replay");
    bool delivered = false;
    until([&] { return delivered; }, [&] {
        service();
        while (join.receive(bytes)) { check(!delivered && bytes == fresh, "replacement connection only delivers fresh payload"); delivered = true; }
    }, 5s, "stale packet replay does not acknowledge or suppress the next valid message");
}

void missing_first_message_flow_control() {
    FriendTransport host(options(true, 0));
    LossyRelay relay(host.local_port(), false);
    FriendTransport join(options(false, relay.port()));
    const auto service = [&] { relay.poll(); host.poll(); join.poll(); relay.poll(); };
    until([&] { return host.connected() && join.connected(); }, service, 5s, "flow-control relay connection");
    std::vector<std::vector<std::uint8_t>> messages;
    for (std::uint32_t i = 0; i < 8; ++i) messages.push_back(payload(maximum_message, 810 + i));
    std::size_t sent = 0, received = 0;
    relay.withhold_host_first_message = true;
    const auto deadline = Clock::now() + 750ms;
    do {
        if (sent < messages.size() && host.send(messages[sent])) ++sent;
        service();
        std::vector<std::uint8_t> bytes;
        check(!join.receive(bytes), "later complete messages wait behind the missing first message");
        check(sent * maximum_message <= maximum_queued,
              "out-of-order fragment ACKs do not release the cumulative one-MiB sender window");
        std::this_thread::sleep_for(1ms);
    } while (Clock::now() < deadline);
    check(relay.withheld_fragments > 0 && sent > 1, "fault actually withholds first-message fragments while later messages are sent");
    relay.withhold_host_first_message = false;
    until([&] { return received == messages.size(); }, [&] {
        if (sent < messages.size() && host.send(messages[sent])) ++sent;
        service();
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) {
            check(received < messages.size() && bytes == messages[received],
                  "recovered first message unblocks every subsequent message in exact order");
            ++received;
        }
    }, 15s, "receiver capacity remains available for the missing message, avoiding reassembly deadlock");
}

void reject_unrelated_peers() {
    FriendTransport host(options(true, 0));
    FriendTransport wrong(options(false, host.local_port(), "fedcba9876543210fedcba9876543210"));
    service_for(host, wrong, 250ms);
    check(!host.connected() && !wrong.connected(), "a wrong code cannot complete the handshake");
    RawSocket raw;
    for (const std::size_t size : {0U, 1U, 4U, 31U, 64U, 1200U, 4096U}) {
        raw.send(payload(size, static_cast<std::uint32_t>(size)), loopback(host.local_port()));
        host.poll();
    }
    check(!host.connected(), "malformed and oversized UDP datagrams cannot select a peer");
    check(host.diagnostics().received_packets == 0 && host.diagnostics().sent_packets == 0,
          "wrong-token/malformed datagrams do not count as accepted peer traffic or trigger replies");
    wrong.close();
    FriendTransport join(options(false, host.local_port()));
    connect(host, join);
    FriendTransport third(options(false, host.local_port()));
    const auto deadline = Clock::now() + 300ms;
    do { host.poll(); join.poll(); third.poll(); std::this_thread::sleep_for(1ms); } while (Clock::now() < deadline);
    check(host.connected() && join.connected() && !third.connected(), "host retains exactly one selected peer");
    check(!third.send(payload(32, 44)), "unselected peer cannot enqueue application traffic");
    for (unsigned i = 0; i < 20; ++i) {
        raw.send(payload(1200, i), loopback(host.local_port()));
        host.poll();
    }
    const auto valid = payload(2048, 84);
    check(join.send(valid), "legitimate peer sends after unrelated traffic");
    bool delivered = false;
    until([&] { return delivered; }, [&] {
        host.poll(); join.poll(); third.poll();
        std::vector<std::uint8_t> bytes;
        while (host.receive(bytes)) { check(!delivered && bytes == valid, "only selected peer payload is delivered"); delivered = true; }
    }, 5s, "malformed traffic and competing joins leave the active session usable");
    third.close();
    service_for(host, join, 100ms);
    check(host.connected() && join.connected(), "unselected peer BYE cannot terminate active session");
}

void real_network_faults() {
    FriendTransport host(options(true, 0));
    LossyRelay relay(host.local_port());
    FriendTransport join(options(false, relay.port()));
    const auto service = [&] { relay.poll(); host.poll(); join.poll(); relay.poll(); };
    until([&] { return host.connected() && join.connected(); }, service, 10s,
          "handshake completes over delayed real UDP relay");
    std::vector<std::vector<std::uint8_t>> from_host{payload(maximum_message, 1234)};
    std::vector<std::vector<std::uint8_t>> from_join{payload(maximum_message, 4321)};
    for (std::uint32_t i = 0; i < 12; ++i) {
        from_host.push_back(payload(i * 511, i + 55));
        from_join.push_back(payload(i * 701, i + 66));
    }
    std::size_t sent_host = 0, sent_join = 0, received_host = 0, received_join = 0;
    until([&] { return received_host == from_join.size() && received_join == from_host.size(); }, [&] {
        if (sent_host < from_host.size() && host.send(from_host[sent_host])) ++sent_host;
        if (sent_join < from_join.size() && join.send(from_join[sent_join])) ++sent_join;
        service();
        std::vector<std::uint8_t> bytes;
        while (host.receive(bytes)) {
            check(received_host < from_join.size() && bytes == from_join[received_host],
                  "host preserves complete message bytes/order through loss and reordering");
            ++received_host;
        }
        while (join.receive(bytes)) {
            check(received_join < from_host.size() && bytes == from_host[received_join],
                  "join preserves complete message bytes/order through loss and reordering");
            ++received_join;
        }
    }, 30s, "both directions recover every message through deterministic five-percent packet loss");
    check(relay.dropped > 0 && relay.reordered > 0, "relay actually dropped and reordered real datagrams");
    check(relay.largest_datagram > 1000, "maximum messages exercise fragmentation on the wire");
    const auto deadline = Clock::now() + 250ms;
    do { service(); std::this_thread::sleep_for(1ms); } while (Clock::now() < deadline);
    std::vector<std::uint8_t> bytes;
    check(!host.receive(bytes) && !join.receive(bytes), "late retries remain deduplicated after network fault recovery");
    std::cout << "PASS fault relay: " << relay.dropped << " dropped datagrams, "
              << relay.reordered << " reordered datagrams; largest wire packet " << relay.largest_datagram << " bytes\n";
}

void temporary_network_gap() {
    FriendTransport host(options(true, 0));
    LossyRelay relay(host.local_port(), false);
    FriendTransport join(options(false, relay.port()));
    const auto service = [&] { relay.poll(); host.poll(); join.poll(); relay.poll(); };
    until([&] { return host.connected() && join.connected(); }, service, 5s, "temporary gap handshake");
    relay.blackhole = true;
    service(); // Drain legitimate replies already queued before the blackout.
    const auto before_host = host.diagnostics(), before_join = join.diagnostics();
    const auto from_host = payload(3001, 510), from_join = payload(1901, 511);
    check(host.send(from_host) && join.send(from_join), "queue real messages before network blackout");
    const auto deadline = Clock::now() + 3s;
    do {
        service();
        check(host.connected() && join.connected(), "three-second network gap must not disconnect established peers");
        std::vector<std::uint8_t> bytes;
        check(!host.receive(bytes) && !join.receive(bytes), "blackout did not invent message delivery");
        std::this_thread::sleep_for(1ms);
    } while (Clock::now() < deadline);
    check(relay.dropped > 0, "temporary blackout dropped actual UDP datagrams");
    check(host.diagnostics().received_packets == before_host.received_packets &&
          join.diagnostics().received_packets == before_join.received_packets,
          "blackout cannot advance accepted receive counters");
    check(host.diagnostics().sent_packets > before_host.sent_packets &&
          join.diagnostics().sent_packets > before_join.sent_packets &&
          host.diagnostics().retransmissions > 0 && join.diagnostics().retransmissions > 0,
          "outage diagnostics observe actual packet sends and retried DATA fragments");
    check(host.diagnostics().silence_ms >= 2900 && join.diagnostics().silence_ms >= 2900,
          "silence age observes real time without accepting self-sent packets");
    relay.blackhole = false;
    bool got_host = false, got_join = false;
    until([&] { return got_host && got_join; }, [&] {
        service();
        std::vector<std::uint8_t> bytes;
        while (host.receive(bytes)) { check(!got_host && bytes == from_join, "host recovers original accepted data after gap"); got_host = true; }
        while (join.receive(bytes)) { check(!got_join && bytes == from_host, "join recovers original accepted data after gap"); got_join = true; }
    }, 3s, "both directions recover without reconnecting after temporary blackout");
    check(host.connected() && join.connected(), "recovered peers remain connected");
    check(host.diagnostics().received_packets > before_host.received_packets &&
          join.diagnostics().received_packets > before_join.received_packets &&
          host.diagnostics().silence_ms < 1000 && join.diagnostics().silence_ms < 1000,
          "recovery updates receive counters and resets the observed silence age");
}

void temporary_polling_pause() {
    FriendTransport host(options(true, 0)), join(options(false, host.local_port()));
    connect(host, join);
    const auto data = payload(2401, 512);
    check(host.send(data), "queue data while receiving application is temporarily suspended");
    const auto deadline = Clock::now() + 3s;
    do {
        host.poll();
        check(host.connected(), "three-second peer polling pause must not disconnect host");
        std::this_thread::sleep_for(1ms);
    } while (Clock::now() < deadline);
    bool received = false;
    until([&] { return received; }, [&] {
        host.poll(); join.poll();
        std::vector<std::uint8_t> bytes;
        while (join.receive(bytes)) { check(!received && bytes == data, "resumed process receives accepted data exactly once"); received = true; }
    }, 3s, "resuming the real receiving state machine restores progress");
    check(host.connected() && join.connected(), "temporary polling pause leaves both sessions usable");
}

void terminal_network_silence() {
    FriendTransport host(options(true, 0));
    LossyRelay relay(host.local_port(), false);
    FriendTransport join(options(false, relay.port()));
    const auto service = [&] { relay.poll(); host.poll(); join.poll(); relay.poll(); };
    until([&] { return host.connected() && join.connected(); }, service, 5s, "terminal gap handshake");
    relay.blackhole = true;
    const auto began = Clock::now();
    until([&] { return host.finished() && join.finished(); }, [&] {
        service();
        if (Clock::now() - began < 9500ms)
            check(host.connected() && join.connected(), "silence timeout must not fire before its ten-second budget");
    }, 12s, "continuous peer silence remains bounded by the established liveness timeout");
    check(Clock::now() - began >= 9500ms, "terminal timeout must preserve its production duration");
    check(!host.connected() && !join.connected(), "timeout closes both real sockets");
    check(!host.send(payload(1, 513)) && !join.send(payload(1, 514)), "terminal transports reject later data");
    check(host.status().find("no valid peer packets for 10 seconds") != std::string::npos,
          "established-session silence is explained separately from wrong-code/initial-handshake failures");
    const auto stopped_host = host.diagnostics(), stopped_join = join.diagnostics();
    check(stopped_host.silence_ms >= 10000 && stopped_join.silence_ms >= 10000,
          "timeout preserves its actual observed silence duration");
    std::this_thread::sleep_for(25ms);
    host.close(); join.close(); host.poll(); join.poll();
    check(host.diagnostics().silence_ms == stopped_host.silence_ms &&
          join.diagnostics().silence_ms == stopped_join.silence_ms &&
          host.diagnostics().sent_packets == stopped_host.sent_packets &&
          join.diagnostics().sent_packets == stopped_join.sent_packets,
          "repeated cleanup cannot discard or advance frozen terminal diagnostics");
    std::cout << "PASS terminal silence: bounded ten-second timeout and retained diagnostics\n";
}
} // namespace

int main() {
    try {
        sockets::Runtime runtime;
        invalid_configuration();
        ordered_exchange();
        capacity_and_recovery();
        bounded_receive_queue();
        bounded_empty_message_queue();
        reject_unrelated_peers();
        stale_connection_datagrams();
        missing_first_message_flow_control();
        real_network_faults();
        temporary_network_gap();
        temporary_polling_pause();
        terminal_network_silence();
        std::cout << "PASS friend transport: real UDP/DNS handshake, bounded framing, ordered bidirectional delivery, "
                     "empty/max-size payloads, bounded queues/missing-first-message recovery, stale-generation replay, "
                     "wrong-code/malformed/second-peer isolation, temporary gaps/polling pause, terminal silence and BYE\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL friend transport: " << error.what() << '\n';
        return 1;
    }
}

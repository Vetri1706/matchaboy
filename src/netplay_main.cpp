#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/netplay.hpp"
#include "dmg/serial_link.hpp"
#include "dmg/snapshot.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace dmg;
using namespace dmg::netplay;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t frame_cycles = 70224, absent = std::numeric_limits<std::uint64_t>::max();
constexpr std::size_t history = 60, event_capacity = 256, per_packet = 57;
void require(bool v, std::string_view why) { if (!v) throw std::runtime_error(std::string(why)); }
void put(std::uint8_t *p, std::uint64_t v, unsigned n) {
    for (unsigned i = n; i != 0; --i) { p[i - 1] = static_cast<std::uint8_t>(v); v >>= 8U; }
}
std::uint64_t get(const std::uint8_t *p, unsigned n) {
    std::uint64_t v = 0; for (unsigned i = 0; i < n; ++i) v = (v << 8U) | p[i]; return v;
}
std::vector<std::uint8_t> file_bytes(const std::string &path) {
    std::ifstream in(path, std::ios::binary); require(bool(in), "cannot open " + path);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    require(!in.bad(), "ROM read failed: " + path); return bytes;
}
std::uint64_t digest(const void *data, std::size_t n) {
    const auto *p = static_cast<const std::uint8_t *>(data); std::uint64_t h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ULL; return h;
}
struct Options {
    std::string rom, peer_rom, output, bind{"127.0.0.1"}, peer{"127.0.0.1"};
    std::uint64_t session{1}, frames{1000}, seed{1};
    std::uint16_t port{}, peer_port{};
    unsigned side{}, pace_ms{17}, timeout{120};
};
struct Frame {
    std::uint32_t revision{}, dependency{}, previous{}, next{}, parent{};
    std::uint64_t begin{}, end{}, hash{};
    std::uint16_t count{};
    std::uint8_t buttons{};
    std::array<SerialEvent, event_capacity> events{};
};
struct LinkState {
    std::uint64_t transfer{}, remote_transfer{}, remote_bit_cycle{absent};
    std::uint8_t remote_data{0xff}, remote_control{}, remote_bits{};
    bool remote_bit{true};
};
struct Slot {
    std::uint64_t number{absent};
    MachineSnapshot start{};
    LinkState link{};
    Frame local{}, remote{}, assembling{};
    std::uint8_t chunks{}, received_mask{}, next_chunk{};
    bool dirty{}, have_start{};
    std::array<std::uint8_t, 160 * 144> video{};
    std::array<std::int16_t, 2048> audio{};
    std::size_t audio_size{};
};
struct Certificate {
    std::uint64_t number{absent};
    std::uint32_t local{}, remote{};
};
bool event_less(const SerialEvent &a, const SerialEvent &b) {
    if (a.cycle != b.cycle) return a.cycle < b.cycle;
    if (a.kind != b.kind) return a.kind < b.kind;
    return a.transfer < b.transfer;
}
bool same_state(const Frame &a, const Frame &b) {
    return a.begin == b.begin && a.end == b.end && a.hash == b.hash && a.count == b.count &&
           a.buttons == b.buttons && std::equal(a.events.begin(), a.events.begin() + a.count, b.events.begin());
}

class Peer {
    Options options_;
    Bus bus_;
    Cpu cpu_;
    Channel channel_;
    SerialEndpoint endpoint_{};
    std::unique_ptr<Slot[]> slots_{std::make_unique<Slot[]>(history)};
    std::unique_ptr<MachineSnapshot> scratch_{std::make_unique<MachineSnapshot>()};
    Frame building_{};
    LinkState link_{};
    std::array<SerialEvent, event_capacity * 3> incoming_{};
    std::size_t incoming_count_{}, incoming_cursor_{};
    std::uint64_t current_{}, highwater_{}, confirmed_{}, rollback_cycle_{absent}, rollbacks_{}, replay_frames_{};
    // Datagrams may outlive snapshot slots: retain compact certificates across
    // the 64-sequence receive window plus the 60-frame speculative horizon.
    std::array<Certificate, 128> retired_{};
    std::uint32_t confirmed_local_{}, confirmed_remote_{};
    std::uint64_t confirmed_end_{};
    std::array<Packet, 32> before_hello_{};
    std::size_t before_hello_count_{};
    std::array<double, 16384> rollback_ms_{};
    std::size_t rollback_samples_{};
    std::uint64_t replay_target_{}, replayed_cycles_{};
    double rollback_max_ms_{};
    Clock::time_point replay_begun_{};
    bool replay_active_{};
    std::uint64_t revisions_{}, predicted_bits_{}, max_rewind_{}, presented_hash_{1469598103934665603ULL};
    std::array<std::uint8_t, 32> peer_identity_{};
    bool hello_{}, finishing_{}, remote_finished_{};
    std::ofstream hashes_, audio_, video_;
    Clock::time_point begun_{Clock::now()}, deadline_{}, paced_{}, status_{};

    Slot &slot(std::uint64_t n) {
        auto &s = slots_[n % history];
        if (s.number != n) {
            require(s.number == absent || s.number < confirmed_, "rollback history exhausted: unconfirmed frame would be evicted");
            s.number = n; s.local = {}; s.remote = {}; s.assembling = {}; s.chunks = s.received_mask = s.next_chunk = 0;
            s.dirty = s.have_start = false;
        }
        return s;
    }
    void snapshot_check(SnapshotResult r) {
        if (r != SnapshotResult::Ok)
            throw std::runtime_error(std::string("snapshot: ") + snapshot_result_name(r));
    }
    void emit(SerialEvent e) {
        require(building_.count < event_capacity, "serial frame event capacity exceeded (256)");
        building_.events[building_.count++] = e;
    }
    static void start(void *context, Bus &, std::uint64_t cycle, std::uint8_t value, std::uint8_t control) {
        auto &self = *static_cast<Peer *>(context);
        self.link_.transfer = cycle;
        // MMIO SC writes follow the bus tick at C, so remote visibility begins
        // at C+1; an internal serial edge already happened at C.
        self.emit(SerialEvent{cycle + 1, cycle, static_cast<std::uint8_t>((control & 0x80U) != 0 ? 0 : 2), value, control, 0});
    }
    static bool edge(void *context, Bus &bus, std::uint64_t cycle, bool outgoing) {
        auto &self = *static_cast<Peer *>(context);
        const bool known = self.link_.remote_bit_cycle == cycle;
        const bool input = known ? self.link_.remote_bit :
            (self.link_.remote_control & 0x80U) == 0 || (self.link_.remote_data & 0x80U) != 0;
        if (!known) ++self.predicted_bits_;
        self.emit(SerialEvent{cycle, self.link_.transfer, 1, static_cast<std::uint8_t>(bus.serial_bits()),
                              bus.serial_control(), static_cast<std::uint8_t>(outgoing)});
        self.link_.remote_data = static_cast<std::uint8_t>((self.link_.remote_data << 1U) | (outgoing ? 1U : 0U));
        if (!known && (self.link_.remote_control & 0x80U) != 0 && ++self.link_.remote_bits == 8)
            self.link_.remote_control &= 0x7fU;
        return input;
    }
    static void tick(void *context, Bus &bus, std::uint64_t cycle) {
        auto &self = *static_cast<Peer *>(context);
        while (self.incoming_cursor_ < self.incoming_count_ && self.incoming_[self.incoming_cursor_].cycle <= cycle) {
            const auto e = self.incoming_[self.incoming_cursor_++];
            if (e.kind != 1) {
                self.link_.remote_data = e.data; self.link_.remote_control = e.control; self.link_.remote_bits = 0;
                self.link_.remote_transfer = e.transfer; continue;
            }
            self.link_.remote_bit_cycle = e.cycle; self.link_.remote_bit = e.bit != 0;
            self.link_.remote_bits = static_cast<std::uint8_t>(e.data + 1U);
            if (self.link_.remote_bits == 8) self.link_.remote_control &= 0x7fU;
            if ((e.control & 1U) != 0 && bus.serial_active() && !bus.serial_internal()) {
                require(e.cycle == cycle, "external clock passed without a matching machine tick");
                const bool outgoing = (bus.serial_data() & 0x80U) != 0;
                self.emit(SerialEvent{cycle, self.link_.transfer, 1, static_cast<std::uint8_t>(bus.serial_bits()),
                                      bus.serial_control(), static_cast<std::uint8_t>(outgoing)});
                static_cast<void>(bus.serial_clock(e.bit != 0));
                self.link_.remote_data = static_cast<std::uint8_t>((self.link_.remote_data << 1U) | (outgoing ? 1U : 0U));
            }
        }
    }
    void prepare_incoming() {
        incoming_count_ = incoming_cursor_ = 0;
        const auto first = current_ == 0 ? 0 : current_ - 1;
        for (auto n = first; n <= current_ + 1; ++n) {
            const auto &s = slots_[n % history];
            if (s.number != n || s.remote.revision == 0) continue;
            for (std::size_t i = 0; i < s.remote.count; ++i) {
                const auto e = s.remote.events[i];
                if (e.cycle > bus_.cycles()) incoming_[incoming_count_++] = e;
            }
        }
        std::sort(incoming_.begin(), incoming_.begin() + incoming_count_, event_less);
    }
    void receive_frame(const Packet &p) {
        require(p.size >= 52 && (p.size - 52) % 20 == 0, "invalid frame bundle length");
        if (p.frame < confirmed_) {
            const auto &old = retired_[p.frame % retired_.size()];
            if (old.number != p.frame || get(p.payload.data(), 4) > old.remote)
                throw std::runtime_error("late revision frame=" + std::to_string(p.frame) + " confirmed=" + std::to_string(confirmed_) + " certificate=" + std::to_string(old.number) + " incoming=" + std::to_string(get(p.payload.data(),4)) + " stored=" + std::to_string(old.remote));
            return;
        }
        require(p.frame < confirmed_ + history, "remote frame exceeds fixed history window");
        auto &s = slot(p.frame);
        const auto *b = p.payload.data();
        const auto revision = static_cast<std::uint32_t>(get(b, 4));
        const auto dependency = static_cast<std::uint32_t>(get(b + 4, 4));
        const auto count = static_cast<std::uint16_t>(get(b + 8, 2));
        const auto previous = static_cast<std::uint32_t>(get(b + 40, 4));
        const auto next = static_cast<std::uint32_t>(get(b + 44, 4));
        const auto parent = static_cast<std::uint32_t>(get(b + 48, 4));
        const unsigned part = b[10], chunks = b[11], entries = (p.size - 52U) / 20U;
        const auto expected_chunks = std::max<std::size_t>(1, (static_cast<unsigned>(count) + per_packet - 1U) / per_packet);
        require(revision != 0 && count <= event_capacity && chunks == expected_chunks && part < chunks &&
                entries == std::min<std::size_t>(per_packet, count - std::min<std::size_t>(count, part * per_packet)) &&
                b[13] == 0 && b[14] == 0 && b[15] == 0, "invalid frame bundle metadata");
        if (revision < s.remote.revision || revision < s.assembling.revision ||
            (revision == s.remote.revision && (dependency < s.remote.dependency || previous < s.remote.previous || next < s.remote.next || parent < s.remote.parent)) ||
            (revision == s.assembling.revision && (dependency < s.assembling.dependency || previous < s.assembling.previous || next < s.assembling.next || parent < s.assembling.parent))) return;
        if (revision > s.assembling.revision || dependency != s.assembling.dependency || previous != s.assembling.previous || next != s.assembling.next || parent != s.assembling.parent) {
            s.assembling = {}; s.assembling.revision = revision; s.assembling.dependency = dependency;
            s.assembling.parent = parent; s.assembling.previous = previous; s.assembling.next = next;
            s.assembling.count = count; s.assembling.buttons = b[12]; s.assembling.end = get(b + 16, 8);
            s.assembling.hash = get(b + 24, 8); s.assembling.begin = get(b + 32, 8);
            s.received_mask = 0; s.chunks = static_cast<std::uint8_t>(chunks);
        }
        auto &assembly = s.assembling;
        require(assembly.count == count && assembly.end == get(b + 16, 8) && assembly.hash == get(b + 24, 8) &&
                assembly.begin == get(b + 32, 8) && assembly.buttons == b[12], "inconsistent frame revision fragments");
        for (unsigned i = 0; i < entries; ++i) {
            const auto *v = b + 52 + i * 20;
            auto &e = assembly.events[part * per_packet + i];
            const SerialEvent incoming{get(v, 8), get(v + 8, 8), v[16], v[17], v[18], v[19]};
            require((s.received_mask & (1U << part)) == 0 || e == incoming, "frame revision fragment changed its event list");
            e = incoming;
            require(e.kind <= 2 && e.bit <= 1 && (e.kind != 1 || e.data < 8) && e.cycle >= assembly.begin && e.cycle <= assembly.end + 1,
                    "invalid serial event in frame revision");
        }
        s.received_mask = static_cast<std::uint8_t>(s.received_mask | (1U << part));
        if (s.received_mask != (1U << chunks) - 1U) return;
        require(std::is_sorted(assembly.events.begin(), assembly.events.begin() + count, event_less), "unordered serial event list");
        require(revision != s.remote.revision || same_state(assembly, s.remote), "frame revision changed immutable state");
        // Only a changed event can alter executed hardware. A revision may
        // change an unconsumed suffix after an earlier prefix was confirmed.
        std::size_t divergence = 0;
        while (divergence < count && divergence < s.remote.count &&
               assembly.events[divergence] == s.remote.events[divergence]) ++divergence;
        auto changed_cycle = absent;
        if (divergence < count) changed_cycle = assembly.events[divergence].cycle;
        if (divergence < s.remote.count) changed_cycle = std::min(changed_cycle, s.remote.events[divergence].cycle);
        const auto old_revision = s.remote.revision;
        s.remote = assembly;
        require(changed_cycle == absent || confirmed_ == 0 || changed_cycle > confirmed_end_,
                "authoritative serial event changed an already confirmed prefix");
        if (changed_cycle <= bus_.cycles()) {
            rollback_cycle_ = std::min(rollback_cycle_, changed_cycle);
        } else if (old_revision != revision) {
            for (auto n = p.frame == 0 ? 0 : p.frame - 1; n <= p.frame + 1; ++n) {
                auto &neighbor = slots_[n % history];
                if (neighbor.number != n || neighbor.local.revision == 0 || n < confirmed_) continue;
                if (n == p.frame) neighbor.local.dependency = revision;
                else if (n < p.frame) neighbor.local.next = revision;
                else neighbor.local.previous = revision;
                neighbor.dirty = true; neighbor.next_chunk = 0;
            }
        }
    }
    void poll() {
        Packet p;
        for (unsigned received = 0; received < 128 && channel_.receive(p); ++received) {
            if (p.kind == PacketKind::Hello) {
                require(p.size == 49 && p.payload[0] == 1 - options_.side &&
                        get(p.payload.data() + 1, 8) == options_.frames &&
                        std::equal(peer_identity_.begin(), peer_identity_.end(), p.payload.begin() + 17),
                        "peer ROM, side or run configuration mismatch");
                hello_ = true;
                for (std::size_t i = 0; i < before_hello_count_; ++i) receive_frame(before_hello_[i]);
                before_hello_count_ = 0;
            } else if (p.kind == PacketKind::Inputs) {
                if (hello_) receive_frame(p);
                else { require(before_hello_count_ < before_hello_.size(), "pre-handshake packet capacity exceeded"); before_hello_[before_hello_count_++] = p; }
            }
            else if (p.kind == PacketKind::Finish) {
                require(hello_ && p.size == 0 && p.frame == options_.frames && p.cycle >= options_.frames * frame_cycles, "invalid finish packet"); remote_finished_ = true;
            }
            else throw std::runtime_error("unexpected UDP packet kind");
        }
        channel_.service();
        if (Clock::now() >= status_) {
            const auto &f = slots_[confirmed_ % history];
            std::cerr << "progress current=" << current_ << " confirmed=" << confirmed_ << " local=" << f.local.revision << ":" << f.local.dependency << " remote=" << f.remote.revision << ":" << f.remote.dependency << " rollback=" << rollbacks_ << " dirty=" << f.dirty << ":" << unsigned(f.next_chunk) << ":" << unsigned(f.received_mask) << ":" << f.assembling.revision << "\n"; status_ = Clock::now() + std::chrono::seconds(1);
        }
        require(Clock::now() < deadline_, "netplay wall-clock timeout");
    }
    void flush() {
        for (auto n = confirmed_; n < current_; ++n) {
            auto &s = slots_[n % history];
            if (s.number != n || !s.dirty || s.local.revision == 0) continue;
            const auto &f = s.local;
            const auto chunks = std::max<std::size_t>(1, (f.count + per_packet - 1) / per_packet);
            while (s.next_chunk < chunks) {
                std::array<std::uint8_t, max_payload> b{};
                const auto part = s.next_chunk; const auto first = part * per_packet;
                const auto count = std::min<std::size_t>(per_packet, f.count - std::min<std::size_t>(f.count, first));
                put(b.data(), f.revision, 4); put(b.data() + 4, f.dependency, 4); put(b.data() + 8, f.count, 2);
                b[10] = part; b[11] = static_cast<std::uint8_t>(chunks); b[12] = f.buttons;
                put(b.data() + 16, f.end, 8); put(b.data() + 24, f.hash, 8); put(b.data() + 32, f.begin, 8);
                put(b.data() + 40, f.previous, 4); put(b.data() + 44, f.next, 4); put(b.data() + 48, f.parent, 4);
                for (std::size_t i = 0; i < count; ++i) {
                    const auto encoded = encode_serial(f.events[first + i]);
                    std::copy(encoded.begin(), encoded.end(), b.begin() + static_cast<std::ptrdiff_t>(52 + i * 20));
                }
                if (!channel_.send(PacketKind::Inputs, n, f.end, b.data(), 52 + count * 20)) return;
                ++s.next_chunk;
            }
            s.dirty = false;
        }
    }
    void confirm() {
        if (rollback_cycle_ != absent) return;
        while (confirmed_ < current_ && confirmed_ < options_.frames) {
            auto &s = slot(confirmed_);
            if (s.local.revision == 0 || s.remote.revision == 0 || s.dirty ||
                s.local.dependency != s.remote.revision || s.remote.dependency != s.local.revision) break;
            if (confirmed_ != 0) {
                if (s.local.parent != confirmed_local_ || s.remote.parent != confirmed_remote_ ||
                    s.local.previous != confirmed_remote_ || s.remote.previous != confirmed_local_) break;
            }
            const auto &next = slots_[(confirmed_ + 1) % history];
            if (s.local.end > s.remote.end && (next.number != confirmed_ + 1 || next.remote.revision == 0 ||
                s.local.next != next.remote.revision || next.remote.previous != s.local.revision ||
                next.remote.parent != s.remote.revision)) break;
            if (s.remote.end > s.local.end && (next.number != confirmed_ + 1 || next.local.revision == 0 ||
                s.remote.next != next.local.revision || next.local.previous != s.remote.revision ||
                next.local.parent != s.local.revision)) break;
            hashes_ << confirmed_ << ' ' << s.local.hash << ' ' << digest(s.video.data(), s.video.size()) << ' '
                    << digest(s.audio.data(), s.audio_size * sizeof(std::int16_t)) << '\n';
            // Only mutually confirmed frames leave the rollback boundary.
            audio_.write(reinterpret_cast<const char *>(s.audio.data()), static_cast<std::streamsize>(s.audio_size * sizeof(std::int16_t)));
            video_.write(reinterpret_cast<const char *>(s.video.data()), static_cast<std::streamsize>(s.video.size()));
            presented_hash_ = (presented_hash_ ^ s.local.hash) * 1099511628211ULL;
            retired_[confirmed_ % retired_.size()] = Certificate{confirmed_, s.local.revision, s.remote.revision};
            confirmed_local_ = s.local.revision; confirmed_remote_ = s.remote.revision; confirmed_end_ = s.local.end;
            ++confirmed_;
        }
    }
    void rollback() {
        if (rollback_cycle_ == absent) return;
        std::uint64_t target = absent;
        for (auto n = confirmed_; n < current_; ++n) {
            auto &s = slot(n);
            // A snapshot at C already includes the serial edge at C.
            if (s.have_start && s.local.begin < rollback_cycle_) target = n;
        }
        require(target != absent, "late authoritative event predates available rollback snapshot");
        auto &s = slot(target);
        if (!replay_active_) { replay_active_ = true; replay_begun_ = Clock::now(); replay_target_ = highwater_; }
        snapshot_check(load_snapshot(cpu_, bus_, s.start)); link_ = s.link;
        max_rewind_ = std::max(max_rewind_, current_ - target);
        current_ = target; rollback_cycle_ = absent; ++rollbacks_;
    }
    std::uint8_t buttons(std::uint64_t n) const {
        std::uint64_t x = n + options_.seed * 0x9e3779b97f4a7c15ULL;
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL; x ^= x >> 27; x *= 0x94d049bb133111ebULL;
        return static_cast<std::uint8_t>(x ^ (x >> 31));
    }
    void step_frame() {
        auto &s = slot(current_);
        snapshot_check(save_snapshot(cpu_, bus_, s.start)); s.link = link_; s.have_start = true;
        building_ = {}; building_.begin = bus_.cycles(); building_.buttons = buttons(current_);
        building_.dependency = s.remote.revision;
        if (current_ != 0) {
            if (current_ == confirmed_) { building_.previous = confirmed_remote_; building_.parent = confirmed_local_; }
            else { const auto &prev = slots_[(current_ - 1) % history]; require(prev.number == current_ - 1, "missing unconfirmed parent"); building_.previous = prev.remote.revision; building_.parent = prev.local.revision; }
        }
        { const auto &next = slots_[(current_ + 1) % history]; if (next.number == current_ + 1) building_.next = next.remote.revision; }
        bus_.set_buttons(building_.buttons); prepare_incoming();
        const auto target = (current_ + 1) * frame_cycles;
        while (bus_.cycles() < target) {
            const auto before = bus_.cycles(); cpu_.step();
            if (cpu_.locked) throw std::runtime_error("ROM executed locked opcode: " + cpu_.describe());
            if (bus_.cycles() <= before)
                throw std::runtime_error("oscillator stopped at cycle " + std::to_string(before) + ": " + cpu_.describe());
        }
        building_.end = bus_.cycles();
        std::sort(building_.events.begin(), building_.events.begin() + building_.count, event_less);
        s.audio_size = bus_.apu.drain_samples(s.audio);
        require(bus_.apu.state().sample_count == 0, "per-frame presentation audio capacity exceeded");
        s.video = bus_.ppu.framebuffer;
        snapshot_check(save_snapshot(cpu_, bus_, *scratch_)); building_.hash = scratch_->checksum;
        const bool changed = s.local.revision == 0 || !same_state(building_, s.local);
        require(!changed || s.local.revision < 10000, "revision convergence bound exceeded");
        building_.revision = s.local.revision + (changed ? 1U : 0U);
        if (changed || building_.dependency != s.local.dependency || building_.previous != s.local.previous || building_.next != s.local.next || building_.parent != s.local.parent) { s.dirty = true; s.next_chunk = 0; }
        if (changed && s.local.revision != 0) ++revisions_;
        s.local = building_;
        if (current_ < highwater_) { ++replay_frames_; replayed_cycles_ += building_.end - building_.begin; }
        ++current_; highwater_ = std::max(highwater_, current_);
        if (replay_active_ && current_ >= replay_target_) {
            const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - replay_begun_).count();
            rollback_max_ms_ = std::max(rollback_max_ms_, elapsed);
            if (rollback_samples_ < rollback_ms_.size()) rollback_ms_[rollback_samples_++] = elapsed;
            replay_active_ = false;
        }
    }
  public:
    explicit Peer(Options options) : options_(std::move(options)), bus_(file_bytes(options_.rom)), cpu_(bus_),
        channel_(options_.port, options_.peer, options_.peer_port, options_.session, 30, options_.bind) {
        require(options_.side < 2 && options_.frames != 0 && options_.frames <= 1000000, "invalid side or frame count");
        peer_identity_ = snapshot_rom_identity(file_bytes(options_.peer_rom));
        std::filesystem::create_directories(options_.output);
        hashes_.exceptions(std::ios::failbit | std::ios::badbit); audio_.exceptions(std::ios::failbit | std::ios::badbit); video_.exceptions(std::ios::failbit | std::ios::badbit);
        hashes_.open(options_.output + "/frames.txt"); audio_.open(options_.output + "/confirmed.pcm", std::ios::binary);
        video_.open(options_.output + "/confirmed.video", std::ios::binary);
        require(bool(hashes_) && bool(audio_) && bool(video_), "cannot create presentation outputs");
        bus_.apu.set_sample_rate(48000);
        endpoint_ = SerialEndpoint{this, start, tick, edge}; bus_.serial_endpoint = &endpoint_;
        deadline_ = begun_ + std::chrono::seconds(options_.timeout); paced_ = begun_;
    }
    void run() {
        std::array<std::uint8_t, 49> h{}; h[0] = static_cast<std::uint8_t>(options_.side);
        put(h.data() + 1, options_.frames, 8); put(h.data() + 9, options_.seed, 8);
        const auto identity = snapshot_rom_identity(file_bytes(options_.rom));
        std::copy(identity.begin(), identity.end(), h.begin() + 17);
        require(channel_.send(PacketKind::Hello, 0, 0, h.data(), h.size()), "cannot enqueue handshake");
        while (!hello_ || !channel_.drained()) { poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        begun_ = paced_ = Clock::now();
        while (confirmed_ < options_.frames || current_ < options_.frames + 1) {
            poll(); rollback(); flush(); confirm();
            if (current_ < options_.frames + 1 && current_ < confirmed_ + history - 2 &&
                (current_ < highwater_ || Clock::now() >= paced_)) {
                const bool fresh = current_ == highwater_; step_frame();
                if (fresh) paced_ += std::chrono::milliseconds(options_.pace_ms);
                flush();
            } else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        auto &final_boundary = slot(options_.frames);
        require(final_boundary.have_start, "missing final boundary snapshot");
        snapshot_check(load_snapshot(cpu_, bus_, final_boundary.start)); link_ = final_boundary.link;
        while (!finishing_ || !remote_finished_ || !channel_.drained()) {
            if (!finishing_) finishing_ = channel_.send(PacketKind::Finish, confirmed_, bus_.cycles(), nullptr, 0);
            poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        // Allow a peer whose final ACK was lost to retransmit its Finish.
        const auto grace = Clock::now() + std::chrono::milliseconds(500);
        while (Clock::now() < grace) { poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        snapshot_check(save_snapshot(cpu_, bus_, *scratch_));
        hashes_.flush(); audio_.flush(); video_.flush();
        std::ofstream state; state.exceptions(std::ios::failbit | std::ios::badbit); state.open(options_.output + "/final.snapshot", std::ios::binary);
        state.write(reinterpret_cast<const char *>(scratch_->data.data()), scratch_->size);
        state.close();
        std::ofstream outgoing; outgoing.exceptions(std::ios::failbit | std::ios::badbit); outgoing.open(options_.output + "/outgoing.bin", std::ios::binary);
        outgoing.write(bus_.serial_output.data(), static_cast<std::streamsize>(bus_.serial_output.size()));
        outgoing.close();
        std::ofstream memory; memory.exceptions(std::ios::failbit | std::ios::badbit); memory.open(options_.output + "/memory.bin", std::ios::binary);
        for (unsigned address = 0; address < 65536; ++address) memory.put(static_cast<char>(bus_.peek(static_cast<std::uint16_t>(address))));
        memory.close(); hashes_.close(); audio_.close(); video_.close();
        std::sort(rollback_ms_.begin(), rollback_ms_.begin() + rollback_samples_);
        const double median = rollback_samples_ == 0 ? 0 : rollback_ms_[rollback_samples_ / 2];
        const double p95 = rollback_samples_ == 0 ? 0 : rollback_ms_[(rollback_samples_ - 1) * 95 / 100];
        const auto seconds = std::chrono::duration<double>(Clock::now() - begun_).count();
        const auto &stats = channel_.statistics();
        std::ofstream report; report.exceptions(std::ios::failbit | std::ios::badbit); report.open(options_.output + "/report.json");
        report << "{\n  \"passed\": true, \"side\": " << options_.side << ", \"frames\": " << confirmed_
               << ", \"cycles\": " << bus_.cycles() << ", \"state_hash\": " << snapshot_hash(*scratch_)
               << ", \"presentation_hash\": " << presented_hash_ << ", \"rollbacks\": " << rollbacks_
               << ", \"replayed_frames\": " << replay_frames_ << ", \"revisions\": " << revisions_
               << ", \"max_rewind_frames\": " << max_rewind_ << ", \"predicted_bits\": " << predicted_bits_
               << ", \"sent\": " << stats.sent << ", \"received\": " << stats.received
               << ", \"retransmitted\": " << stats.retransmitted << ", \"duplicates\": " << stats.duplicates
               << ", \"rollback_groups_measured\": " << rollback_samples_ << ", \"rollback_p50_ms\": " << median << ", \"rollback_p95_ms\": " << p95 << ", \"rollback_max_ms\": " << rollback_max_ms_ << ", \"replayed_cycles\": " << replayed_cycles_
               << ", \"malformed\": " << stats.malformed << ", \"seconds\": " << seconds << "\n}\n";
        report.close();
        std::cout << "confirmed " << confirmed_ << " frames, " << rollbacks_ << " rollbacks\n";
    }
};
} // namespace
int main(int argc, char **argv) {
    try {
        Options o;
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i]; require(i + 1 < argc, "missing option value");
            const std::string value = argv[++i];
            if (key == "--rom") o.rom = value; else if (key == "--peer-rom") o.peer_rom = value;
            else if (key == "--output") o.output = value; else if (key == "--peer") o.peer = value;
            else if (key == "--bind") o.bind = value;
            else {
                std::size_t used = 0; const auto number = std::stoull(value, &used); require(!value.empty() && value[0] != char(45) && used == value.size(), "invalid integer");
                if (key == "--side" || key == "--pace-ms" || key == "--timeout") require(number <= std::numeric_limits<unsigned>::max(), "integer exceeds option range");
                if (key == "--timeout") require(number != 0 && number <= 86400, "timeout must be1..86400seconds");
                if (key == "--port" || key == "--peer-port") { require(number > 0 && number <= 65535, "invalid port");
                    if (key == "--port") o.port = static_cast<std::uint16_t>(number); else o.peer_port = static_cast<std::uint16_t>(number); }
                else if (key == "--session") o.session = number; else if (key == "--frames") o.frames = number;
                else if (key == "--seed") o.seed = number; else if (key == "--side") o.side = static_cast<unsigned>(number);
                else if (key == "--pace-ms") o.pace_ms = static_cast<unsigned>(number);
                else if (key == "--timeout") o.timeout = static_cast<unsigned>(number);
                else throw std::runtime_error("unknown option " + key);
            }
        }
        require(!o.rom.empty() && !o.peer_rom.empty() && !o.output.empty() && o.port != 0 && o.peer_port != 0,
                "required: --rom --peer-rom --output --port --peer-port; optional --side --seed --frames --bind --peer --session --pace-ms --timeout");
        auto peer = std::make_unique<Peer>(o); peer->run();
    } catch (const std::exception &e) { std::cerr << "netplay failed: " << e.what() << '\n'; return 1; }
}

#include "dmg/snapshot.hpp"
#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <type_traits>

namespace dmg {
namespace {
constexpr std::uint64_t Magic = 0x3150414E53474D44ULL; // DMGSNAP1, little endian
static_assert(sizeof(unsigned) == 4 && sizeof(int) == 4);

struct Encoder {
    MachineSnapshot &out;
    std::size_t position{};
    SnapshotResult result{SnapshotResult::Ok};
    void block(const std::uint8_t *data, std::size_t count) noexcept {
        if (count > out.data.size() - position) {
            result = SnapshotResult::CapacityExceeded;
            return;
        }
        std::copy_n(data, count, out.data.data() + position);
        position += count;
    }
    template<class T> void value(const T &v) noexcept {
        if constexpr (std::is_same_v<T, bool>) {
            const auto byte = static_cast<std::uint8_t>(v);
            block(&byte, 1);
        } else {
            static_assert(std::is_integral_v<T>);
            using U = std::make_unsigned_t<T>;
            U bits = std::bit_cast<U>(v);
            std::array<std::uint8_t, sizeof(T)> bytes{};
            for (auto &byte : bytes) {
                byte = static_cast<std::uint8_t>(bits & 255U);
                bits = static_cast<U>(bits >> 8U);
            }
            block(bytes.data(), bytes.size());
        }
    }
    template<class T> void bounded(const T &v, std::uint64_t) noexcept { value(v); }
    template<class T> void enumeration(const T &v, unsigned) noexcept {
        value(static_cast<std::uint8_t>(v));
    }
    template<std::size_t N> void bytes(const std::array<std::uint8_t, N> &v) noexcept {
        block(v.data(), v.size());
    }
    void text(const std::string &v) noexcept {
        value(static_cast<std::uint32_t>(v.size()));
        // This copies character bytes, never an object representation.
        block(reinterpret_cast<const std::uint8_t *>(v.data()), v.size());
    }
};

struct Decoder {
    const MachineSnapshot &in;
    bool apply{};
    std::size_t position{};
    SnapshotResult result{SnapshotResult::Ok};
    bool available(std::size_t count) noexcept {
        if (position > in.size || count > in.size - position) {
            result = SnapshotResult::InvalidSnapshot;
            return false;
        }
        return result == SnapshotResult::Ok;
    }
    void block(std::uint8_t *data, std::size_t count) noexcept {
        if (!available(count))
            return;
        if (apply)
            std::copy_n(in.data.data() + position, count, data);
        position += count;
    }
    template<class T> void bounded(T &v, std::uint64_t maximum) noexcept {
        if (!available(sizeof(T)))
            return;
        if constexpr (std::is_same_v<T, bool>) {
            const auto byte = in.data[position++];
            if (byte > 1)
                result = SnapshotResult::InvalidSnapshot;
            else if (apply)
                v = byte != 0;
        } else {
            static_assert(std::is_integral_v<T>);
            using U = std::make_unsigned_t<T>;
            U bits = 0;
            for (unsigned i = 0; i < sizeof(T); ++i)
                bits = static_cast<U>(bits | (static_cast<U>(in.data[position++]) << (i * 8U)));
            if constexpr (std::is_unsigned_v<T>) {
                if (bits > maximum) {
                    result = SnapshotResult::InvalidSnapshot;
                    return;
                }
            } else {
                const auto decoded = static_cast<std::int64_t>(std::bit_cast<T>(bits));
                if (maximum != std::numeric_limits<std::uint64_t>::max() &&
                    (decoded < -static_cast<std::int64_t>(maximum) ||
                     decoded > static_cast<std::int64_t>(maximum))) {
                    result = SnapshotResult::InvalidSnapshot;
                    return;
                }
            }
            if (apply)
                v = std::bit_cast<T>(bits);
        }
    }
    template<class T> void value(T &v) noexcept {
        bounded(v, std::numeric_limits<std::uint64_t>::max());
    }
    template<class T> void enumeration(T &v, unsigned maximum) noexcept {
        if (!available(1))
            return;
        const auto byte = in.data[position++];
        if (byte > maximum)
            result = SnapshotResult::InvalidSnapshot;
        else if (apply)
            v = static_cast<T>(byte);
    }
    template<std::size_t N> void bytes(std::array<std::uint8_t, N> &v) noexcept {
        block(v.data(), v.size());
    }
    void text(std::string &v) noexcept {
        if (!available(4))
            return;
        std::uint32_t count = 0;
        for (unsigned i = 0; i < 4; ++i)
            count |= static_cast<std::uint32_t>(in.data[position++]) << (i * 8U);
        if (count > SnapshotSerialCapacity) {
            result = SnapshotResult::SerialOverflow;
            return;
        }
        if (count > v.capacity()) {
            result = SnapshotResult::SerialCapacity;
            return;
        }
        if (!available(count))
            return;
        if (apply) {
            // Construction reserves the full supported backlog. Shrinking and
            // restoring a different branch never retains external string data.
            v.resize(count);
            std::memcpy(v.data(), in.data.data() + position, count);
        }
        position += count;
    }
};

constexpr std::array<std::uint32_t, 64> ShaK{
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U};
void sha_block(std::array<std::uint32_t, 8> &state, const std::uint8_t *block) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (static_cast<std::uint32_t>(block[i*4]) << 24U) |
               (static_cast<std::uint32_t>(block[i*4+1]) << 16U) |
               (static_cast<std::uint32_t>(block[i*4+2]) << 8U) | block[i*4+3];
    for (unsigned i = 16; i < 64; ++i) {
        const auto a = w[i-15], b = w[i-2];
        w[i] = w[i-16] + (std::rotr(a,7) ^ std::rotr(a,18) ^ (a>>3U)) + w[i-7] +
               (std::rotr(b,17) ^ std::rotr(b,19) ^ (b>>10U));
    }
    auto v = state;
    for (unsigned i = 0; i < 64; ++i) {
        const auto s1 = std::rotr(v[4],6) ^ std::rotr(v[4],11) ^ std::rotr(v[4],25);
        const auto choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const auto t1 = v[7] + s1 + choose + ShaK[i] + w[i];
        const auto s0 = std::rotr(v[0],2) ^ std::rotr(v[0],13) ^ std::rotr(v[0],22);
        const auto t2 = s0 + ((v[0]&v[1]) ^ (v[0]&v[2]) ^ (v[1]&v[2]));
        v = {t1+t2,v[0],v[1],v[2],v[3]+t1,v[4],v[5],v[6]};
    }
    for (unsigned i = 0; i < 8; ++i)
        state[i] += v[i];
}
} // namespace

// One explicit schema visits every mutable field. References, immutable ROM,
// object padding, capacities and host pointers never enter the byte stream.
struct SnapshotAccess {
    static bool compatible(const Cpu &c, const Bus &b) noexcept {
        return &c.bus_ == &b && &b.ppu.interrupts_ == &b.iflag &&
               &b.timer.iflag_ == &b.iflag && !c.step_active_;
    }
    template<class Io, class C, class B> static void state(Io &io, C &c, B &b) noexcept {
#define S(field) io.value(field)
#define A(field) io.bytes(field)
#define LIMIT(field, maximum) io.bounded(field, maximum)
        S(c.a); S(c.f); S(c.b); S(c.c); S(c.d); S(c.e); S(c.h); S(c.l); S(c.sp); S(c.pc);
        S(c.ime); S(c.halted); S(c.stopped); S(c.locked); S(c.instructions);
        S(c.last_opcode); S(c.last_opcode_pc); A(c.last_bytes); LIMIT(c.last_byte_count,3);
        S(c.halt_bug_); S(c.enable_pending_); S(c.prefetched_); S(c.interrupt_latched_);
        S(c.ir_); S(c.ir_address_); S(c.next_pc_);
        S(b.iflag); S(b.ie); A(b.wram_); A(b.hram_); A(b.io_); S(b.cycles_);
        S(b.joy_select_); S(b.buttons_); S(b.capture_serial_output);
        S(b.serial_data_); S(b.serial_control_); S(b.serial_sent_);
        LIMIT(b.serial_bits_,8); LIMIT(b.serial_phase_,511);
        S(b.dma_active_); LIMIT(b.dma_startup_,8); LIMIT(b.dma_phase_,3); LIMIT(b.dma_index_,160);
        S(b.dma_source_); S(b.dma_pending_source_); io.text(b.serial_output);
        auto &t = b.timer;
        S(t.divider_); S(t.tima_); S(t.tma_); LIMIT(t.tac_,7);
        LIMIT(t.overflow_delay_,4); LIMIT(t.reload_window_,4);
        auto &k = b.cartridge;
        S(k.ram_enabled_); S(k.rumble_active_); S(k.low_bank_); S(k.high_bank_); S(k.mode_);
        S(k.ram_select_); S(k.latch_previous_); A(k.rtc_); A(k.latched_rtc_);
        LIMIT(k.rtc_subsecond_,4194303); io.block(k.ram_.data(),k.ram_.size());
        auto &p = b.ppu;
        A(p.vram); A(p.oam); A(p.framebuffer); S(p.lcd_cp_enabled_);
        S(p.scroll_bus_address_); S(p.scroll_bus_value_); S(p.scroll_bus_age_);
        S(p.control_bus_active_); S(p.control_bus_value_); S(p.control_bus_age_);
        S(p.window_match_emitted_); S(p.window_match_consumed_);
        S(p.lcdc_); S(p.stat_select_); S(p.scy_); S(p.scx_); S(p.ly_); S(p.lyc_);
        S(p.bgp_); S(p.obp0_); S(p.obp1_); S(p.wy_); S(p.wx_);
        LIMIT(p.line_,153); LIMIT(p.dot_,455); LIMIT(p.mode_,3); LIMIT(p.reported_mode_,3);
        S(p.mode_delay_); S(p.compare_delay_); S(p.frames_); S(p.stat_line_); S(p.ly_compare_);
        S(p.first_line_); S(p.vclk_); S(p.paru_); S(p.dma_active_); S(p.stat_glitch_ticks_);
        S(p.palette_bus_address_); S(p.palette_bus_value_); S(p.palette_bus_settling_);
        for (auto &o : p.objects_) {
            S(o.y); S(o.x); S(o.tile); S(o.flags); LIMIT(o.index,39);
        }
        LIMIT(p.object_count_,10); LIMIT(p.next_object_,10);
        for (auto &o : p.object_fifo_) { LIMIT(o.color,3); S(o.flags); }
        S(p.active_object_.y); S(p.active_object_.x); S(p.active_object_.tile);
        S(p.active_object_.flags); LIMIT(p.active_object_.index,39);
        LIMIT(p.object_wait_,5); LIMIT(p.object_phase_,6); S(p.object_low_); S(p.object_high_);
        S(p.object_fetching_); S(p.last_object_tile_);
        for (auto &color : p.background_fifo_) LIMIT(color,3);
        LIMIT(p.fifo_head_,7); LIMIT(p.fifo_size_,8); LIMIT(p.fetch_ticks_,1); S(p.fetch_tile_x_);
        io.enumeration(p.fetch_stage_,3); S(p.discard_first_fetch_); S(p.fetched_window_);
        S(p.fetched_tile_); S(p.fetched_low_); S(p.fetched_high_); LIMIT(p.previsible_discard_,8);
        LIMIT(p.screen_x_,160); LIMIT(p.discard_,14); S(p.window_line_); S(p.window_fetch_line_);
        LIMIT(p.window_match_wait_,1); S(p.window_match_wx_); S(p.window_match_pending_);
        S(p.window_y_triggered_); S(p.window_active_);
        auto &a = b.apu.state_;
        A(a.registers); A(a.wave_ram);
        for (auto &pulse : a.pulse) {
            S(pulse.timer); LIMIT(pulse.length,64); LIMIT(pulse.sweep_shadow,2047); LIMIT(pulse.position,7);
            LIMIT(pulse.sweep_timer,8); LIMIT(pulse.envelope.volume,15); LIMIT(pulse.envelope.timer,8);
            S(pulse.envelope.running); S(pulse.enabled); S(pulse.sweep_enabled); S(pulse.sweep_negated);
            S(pulse.triggered); S(pulse.duty_started);
        }
        S(a.wave.timer); LIMIT(a.wave.length,256); LIMIT(a.wave.position,31); LIMIT(a.wave.sample,15);
        LIMIT(a.wave.access_ticks,2); S(a.wave.enabled);
        S(a.noise.timer); LIMIT(a.noise.length,64); LIMIT(a.noise.lfsr,32767);
        LIMIT(a.noise.envelope.volume,15); LIMIT(a.noise.envelope.timer,8); S(a.noise.envelope.running);
        S(a.noise.enabled); S(a.t_cycles); LIMIT(a.sample_phase,4194303); LIMIT(a.sample_rate,4194304);
        for (auto &capacitor : a.highpass_capacitor) LIMIT(capacitor,1ULL<<40U);
        LIMIT(a.highpass_coefficient,65536);
        for (auto &sample : a.samples) S(sample);
        LIMIT(a.sample_read,4095); LIMIT(a.sample_write,4095); LIMIT(a.sample_count,4096);
        LIMIT(a.frame_step,7); S(a.powered);
#undef LIMIT
#undef A
#undef S
    }
    static SnapshotResult save(const Cpu &c, const Bus &b, MachineSnapshot &out) noexcept {
        if (!compatible(c,b)) return SnapshotResult::MachineMismatch;
        if (b.serial_output.size() > SnapshotSerialCapacity) return SnapshotResult::SerialOverflow;
        if (b.cartridge.ram_.size() > 131072) return SnapshotResult::CapacityExceeded;
        Encoder writer{out};
        writer.value(Magic); writer.value(SnapshotVersion);
        writer.bytes(b.cartridge.rom_identity_);
        writer.value(static_cast<std::uint64_t>(b.cartridge.rom_.size()));
        writer.value(static_cast<std::uint32_t>(b.cartridge.ram_.size()));
        state(writer,c,b);
        if (writer.result != SnapshotResult::Ok) return writer.result;
        out.size = static_cast<std::uint32_t>(writer.position);
        out.checksum = snapshot_hash(out);
        return SnapshotResult::Ok;
    }
    static SnapshotResult load(Cpu &c, Bus &b, const MachineSnapshot &in) noexcept {
        if (!compatible(c,b)) return SnapshotResult::MachineMismatch;
        if (in.size < 56 || in.size > in.data.size() || snapshot_hash(in) != in.checksum)
            return SnapshotResult::InvalidSnapshot;
        std::uint64_t magic{}, rom_size{};
        std::uint32_t version{}, ram_size{};
        std::array<std::uint8_t,32> identity{};
        Decoder reader{in,true};
        reader.value(magic); reader.value(version); reader.bytes(identity);
        reader.value(rom_size); reader.value(ram_size);
        if (magic != Magic) return SnapshotResult::InvalidSnapshot;
        if (version != SnapshotVersion) return SnapshotResult::VersionMismatch;
        if (identity != b.cartridge.rom_identity_ || rom_size != b.cartridge.rom_.size())
            return SnapshotResult::RomMismatch;
        if (ram_size != b.cartridge.ram_.size()) return SnapshotResult::MachineMismatch;
        const auto start = reader.position;
        reader.apply = false;
        state(reader,c,b);
        if (reader.result != SnapshotResult::Ok) return reader.result;
        if (reader.position != in.size) return SnapshotResult::InvalidSnapshot;
        // All bounds, booleans, enum values, lengths and capacities have been
        // checked before the first live write. No MMIO side effects occur here.
        Decoder commit{in,true,start};
        state(commit,c,b);
#ifdef ENABLE_AUTOPSY
        if (commit.result == SnapshotResult::Ok)
            b.ppu.invalidate_fifo_provenance();
#endif
        return commit.result;
    }
};

std::array<std::uint8_t,32> snapshot_rom_identity(std::span<const std::uint8_t> rom) noexcept {
    std::array<std::uint32_t,8> state{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                    0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::size_t offset = 0;
    while (rom.size() - offset >= 64) {
        sha_block(state,rom.data()+offset);
        offset += 64;
    }
    std::array<std::uint8_t,128> tail{};
    const auto remaining = rom.size()-offset;
    if (remaining != 0)
        std::copy_n(rom.data()+offset,remaining,tail.data());
    tail[remaining] = 0x80;
    const std::size_t total = remaining < 56 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(rom.size())*8U;
    for (unsigned i = 0; i < 8; ++i)
        tail[total-1-i] = static_cast<std::uint8_t>(bits >> (i*8U));
    sha_block(state,tail.data());
    if (total == 128) sha_block(state,tail.data()+64);
    std::array<std::uint8_t,32> result{};
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j)
            result[i*4+j] = static_cast<std::uint8_t>(state[i] >> ((3U-j)*8U));
    return result;
}
SnapshotResult save_snapshot(const Cpu &c,const Bus &b,MachineSnapshot &s) noexcept {
    return SnapshotAccess::save(c,b,s);
}
SnapshotResult load_snapshot(Cpu &c,Bus &b,const MachineSnapshot &s) noexcept {
    return SnapshotAccess::load(c,b,s);
}
std::uint64_t snapshot_hash(const MachineSnapshot &s) noexcept {
    if (s.size > s.data.size()) return 0;
    // Fixed little-endian words and a specified avalanche, independent of
    // object padding, alignment or host byte order. ROM identity uses SHA256;
    // this non-cryptographic checksum is for deterministic state comparison.
    std::uint64_t hash = 0x9E3779B185EBCA87ULL ^ s.size;
    std::size_t i = 0;
    for (; i + 8 <= s.size; i += 8) {
        std::uint64_t word = 0;
        for (unsigned byte = 0; byte < 8; ++byte)
            word |= static_cast<std::uint64_t>(s.data[i + byte]) << (byte * 8U);
        hash = std::rotl(hash ^ word,27) * 0xC2B2AE3D27D4EB4FULL;
    }
    for (; i < s.size; ++i)
        hash = (hash ^ s.data[i]) * 0x100000001B3ULL;
    hash ^= hash >> 33U;
    hash *= 0xFF51AFD7ED558CCDULL;
    hash ^= hash >> 33U;
    hash *= 0xC4CEB9FE1A85EC53ULL;
    return hash ^ (hash >> 33U);
}
bool snapshot_equal(const MachineSnapshot &a,const MachineSnapshot &b) noexcept {
    return a.size <= a.data.size() && a.size == b.size &&
           std::equal(a.data.begin(),a.data.begin()+a.size,b.data.begin());
}
const char *snapshot_result_name(SnapshotResult result) noexcept {
    switch (result) {
    case SnapshotResult::Ok: return "ok";
    case SnapshotResult::InvalidSnapshot: return "invalid snapshot";
    case SnapshotResult::VersionMismatch: return "snapshot version mismatch";
    case SnapshotResult::RomMismatch: return "snapshot ROM mismatch";
    case SnapshotResult::MachineMismatch: return "machine or instruction-boundary mismatch";
    case SnapshotResult::SerialOverflow: return "serial backlog exceeds 64 KiB";
    case SnapshotResult::SerialCapacity: return "serial destination was not preallocated";
    case SnapshotResult::CapacityExceeded: return "snapshot capacity exceeded";
    case SnapshotResult::HistoryUnavailable: return "snapshot history unavailable";
    }
    return "unknown snapshot result";
}
SnapshotRing::SnapshotRing() : frames_(std::make_unique<MachineSnapshot[]>(Capacity)) {}
SnapshotResult SnapshotRing::capture(const Cpu &c,const Bus &b) noexcept {
    const auto result = save_snapshot(c,b,frames_[next_]);
    if (result == SnapshotResult::Ok) {
        next_ = (next_+1)%Capacity;
        count_ = std::min(count_+1,Capacity);
    }
    return result;
}
const MachineSnapshot *SnapshotRing::at(std::size_t frames_back) const noexcept {
    if (frames_back >= count_) return nullptr;
    return &frames_[(next_+Capacity-1-frames_back)%Capacity];
}
SnapshotResult SnapshotRing::rewind(Cpu &c,Bus &b,std::size_t frames_back) noexcept {
    const auto *snapshot = at(frames_back);
    if (snapshot == nullptr) return SnapshotResult::HistoryUnavailable;
    const auto result = load_snapshot(c,b,*snapshot);
    if (result == SnapshotResult::Ok) {
        next_ = (next_+Capacity-frames_back)%Capacity;
        count_ -= frames_back;
    }
    return result;
}
} // namespace dmg

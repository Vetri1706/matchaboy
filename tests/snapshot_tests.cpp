#include "dmg/cpu.hpp"
#include "dmg/mmu.hpp"
#include "dmg/snapshot.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
bool track_allocations = false;
std::size_t allocations = 0;
unsigned assertions = 0;
void require(bool condition,const char *message) {
    ++assertions;
    if (!condition) throw std::runtime_error(message);
}
void ok(dmg::SnapshotResult result) {
    require(result == dmg::SnapshotResult::Ok,dmg::snapshot_result_name(result));
}
std::vector<std::uint8_t> rom(std::uint8_t type = 0x10, std::uint8_t ram_size = 3) {
    std::vector<std::uint8_t> bytes(32768,0);
    bytes[0x100]=0xC3; bytes[0x101]=0x50; bytes[0x102]=0x01;
    bytes[0x147]=type; bytes[0x149]=ram_size;
    // The same actual instruction stream also runs from HRAM during DMA.
    constexpr std::array<std::uint8_t,14> loop{
        0xFA,0x00,0xC0,0x3C,0xEA,0x00,0xC0,0xE0,0x42,0xCB,0x07,0x18,0xF3,0x00};
    std::copy(loop.begin(),loop.end(),bytes.begin()+0x150);
    bytes[0x40]=bytes[0x48]=bytes[0x50]=bytes[0x58]=bytes[0x60]=0xD9;
    return bytes;
}
void hram_program(dmg::Cpu &cpu,dmg::Bus &bus) {
    const auto bytes=rom();
    for (unsigned i=0;i<14;++i) bus.poke(static_cast<std::uint16_t>(0xFF80+i),bytes[0x150+i]);
    cpu.pc=0xFF80;
    cpu.sp=0xFFFE;
    cpu.prime();
}
void replay(dmg::Cpu &cpu,dmg::Bus &bus,unsigned count) {
    for (unsigned i=0;i<count;++i) {
        if (i%17==0) bus.set_buttons(static_cast<std::uint8_t>(i));
        if (i%71==0) bus.write(0xFF43,static_cast<std::uint8_t>(i));
        if (i%101==0) bus.write(0xFF4B,static_cast<std::uint8_t>(i%168));
        if (i%503==0) {
            bus.write(0xFF01,static_cast<std::uint8_t>(i));
            bus.write(0xFF02,0x81);
        }
        if (i==1) {
            bus.ppu.release_control_bus();
            bus.ppu.release_scroll_bus();
            bus.ppu.release_palette_bus();
        }
        cpu.step();
    }
}
void sha_vectors() {
    constexpr std::array<const char *,3> messages{"","abc",
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    constexpr std::array<const char *,3> expected{
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (unsigned i=0;i<messages.size();++i) {
        const std::string message(messages[i]);
        const auto digest=dmg::snapshot_rom_identity({
            reinterpret_cast<const std::uint8_t *>(message.data()),message.size()});
        std::string actual;
        for (auto byte:digest) {
            actual.push_back("0123456789abcdef"[byte>>4U]);
            actual.push_back("0123456789abcdef"[byte&15U]);
        }
        require(actual==expected[i],"SHA256 published empty, abc and two-block padding vectors");
    }
}
void device_replay() {
    for (unsigned phase=0;phase<12;++phase) {
        dmg::Bus bus(rom());
        dmg::Cpu cpu(bus);
        hram_program(cpu,bus);
        bus.poke(0xFF40,0);
        bus.poke(0xFF43,7);
        bus.poke(0xFF4B,6);
        bus.ppu.oam[0]=16; bus.ppu.oam[1]=8; bus.ppu.oam[2]=1;
        bus.poke(0xFF40,0xF3);
        bus.tick(452+83+phase);
        require(bus.ppu.mode()==3,"snapshot fixture actually reaches a live pixel transfer");
        bus.poke(0xFF06,0xAB);
        bus.poke(0xFF05,0xFF);
        bus.poke(0xFF07,5);
        bus.write(0xFF01,0xA5);
        bus.write(0xFF02,0x81);
        bus.tick(phase*37);
        bus.write(0xFF46,0xC0);
        bus.tick(phase);
        require(bus.dma_active()==(phase>=4),
                "snapshot spans the actual four remaining DMA startup dots and active transfer");
        bus.ppu.drive_control_bus(0xD3);
        bus.ppu.drive_scroll_bus(0xFF42,static_cast<std::uint8_t>(phase));
        bus.ppu.drive_palette_bus(0xFF47,0xE4,true);
        bus.apu.set_sample_rate(44100);
        bus.poke(0xFF26,0x80); bus.poke(0xFF12,0xF3); bus.poke(0xFF14,0x87);
        dmg::MachineSnapshot before,after,repeated;
        ok(dmg::save_snapshot(cpu,bus,before));
        replay(cpu,bus,3000);
        ok(dmg::save_snapshot(cpu,bus,after));
        ok(dmg::load_snapshot(cpu,bus,before));
        replay(cpu,bus,3000);
        ok(dmg::save_snapshot(cpu,bus,repeated));
        require(dmg::snapshot_equal(after,repeated),"CPU/device replay is byte-exact after restore");
        require(dmg::snapshot_hash(after)==dmg::snapshot_hash(repeated),"replay hashes agree");
    }
}
void interrupt_and_rtc() {
    dmg::Bus bus(rom());
    dmg::Cpu cpu(bus);
    bus.poke(0xFF80,0xFB); bus.poke(0xFF81,0); bus.poke(0xFF82,0x76);
    cpu.pc=0xFF80; cpu.prime(); cpu.step(); // EI has a pending delayed enable.
    bus.ie=4; bus.iflag=4;
    bus.cartridge.write(0,0x0A);
    for (unsigned i=0;i<5;++i) {
        bus.cartridge.write(0x4000,static_cast<std::uint8_t>(8+i));
        bus.cartridge.write(0xA000,static_cast<std::uint8_t>(i==4?0x81:59-i));
    }
    bus.cartridge.write(0x6000,0); bus.cartridge.write(0x6000,1);
    bus.tick(12345);
    dmg::MachineSnapshot before,after,repeated;
    ok(dmg::save_snapshot(cpu,bus,before));
    replay(cpu,bus,2000); ok(dmg::save_snapshot(cpu,bus,after));
    ok(dmg::load_snapshot(cpu,bus,before));
    replay(cpu,bus,2000); ok(dmg::save_snapshot(cpu,bus,repeated));
    require(dmg::snapshot_equal(after,repeated),"delayed EI, interrupt prefetch and latched RTC replay");
}
void rejection_is_atomic() {
    dmg::Bus bus(rom()); dmg::Cpu cpu(bus); cpu.prime();
    dmg::MachineSnapshot original,altered,current;
    ok(dmg::save_snapshot(cpu,bus,original));
    altered=original; altered.data[70]^=1;
    require(dmg::load_snapshot(cpu,bus,altered)==dmg::SnapshotResult::InvalidSnapshot,
            "corrupted checksum rejected");
    altered=original; altered.data[8]=0xFF; altered.checksum=dmg::snapshot_hash(altered);
    require(dmg::load_snapshot(cpu,bus,altered)==dmg::SnapshotResult::VersionMismatch,
            "version mismatch rejected");
    altered=original; altered.data[68]=2; altered.checksum=dmg::snapshot_hash(altered);
    require(dmg::load_snapshot(cpu,bus,altered)==dmg::SnapshotResult::InvalidSnapshot,
            "invalid boolean rejected even with matching checksum");
    altered=original; --altered.size; altered.checksum=dmg::snapshot_hash(altered);
    require(dmg::load_snapshot(cpu,bus,altered)==dmg::SnapshotResult::InvalidSnapshot,
            "truncated payload rejected before writing state");
    ok(dmg::save_snapshot(cpu,bus,current));
    require(dmg::snapshot_equal(original,current),"failed loads leave every machine field unchanged");
    auto other_rom=rom(); other_rom[0x300]=1;
    dmg::Bus other_bus(std::move(other_rom)); dmg::Cpu other_cpu(other_bus);
    require(dmg::load_snapshot(other_cpu,other_bus,original)==dmg::SnapshotResult::RomMismatch,
            "same-size different ROM rejected");
    dmg::Bus smaller_bus(rom(),8192); dmg::Cpu smaller_cpu(smaller_bus);
    require(dmg::load_snapshot(smaller_cpu,smaller_bus,original)==dmg::SnapshotResult::MachineMismatch,
            "same ROM with different physical cartridge RAM rejected");
    require(dmg::save_snapshot(cpu,other_bus,current)==dmg::SnapshotResult::MachineMismatch,
            "CPU cannot be paired with a different bus");
    original.data.back()=0x99;
    require(dmg::snapshot_equal(original,current),"unused preallocation is excluded from equality");
    bus.serial_output.resize(dmg::SnapshotSerialCapacity+1,'x');
    require(dmg::save_snapshot(cpu,bus,current)==dmg::SnapshotResult::SerialOverflow,
            "serial overflow is explicit rather than silently truncated");
}
void execution_boundary_and_external_ownership() {
    dmg::Bus bus(rom()); dmg::Cpu cpu(bus);
    bus.poke(0xFF80,0xE0); bus.poke(0xFF81,0x02); // Actual LDH (SC),A instruction.
    cpu.pc=0xFF80; cpu.a=0x81; cpu.prime();
    // Operand read uses four T-cycles, then the SC write latches at T3.
    // Its real callback therefore coincides with the frame quantum boundary.
    bus.tick(static_cast<unsigned>(70217-bus.cycles()));
    dmg::MachineSnapshot before,scratch;
    ok(dmg::save_snapshot(cpu,bus,before));
    struct Attempt {
        dmg::Cpu *cpu;
        dmg::MachineSnapshot *scratch;
        const dmg::MachineSnapshot *before;
        dmg::SnapshotResult save{dmg::SnapshotResult::Ok},load{dmg::SnapshotResult::Ok};
        bool called{};
        std::uint64_t cycle{};
    } attempt{&cpu,&scratch,&before};
    dmg::SerialEndpoint endpoint;
    endpoint.context=&attempt;
    endpoint.on_start=[](void *context,dmg::Bus &live,std::uint64_t cycle,std::uint8_t,std::uint8_t) {
        auto &a=*static_cast<Attempt *>(context);
        a.called=true;
        a.cycle=cycle;
        a.save=dmg::save_snapshot(*a.cpu,live,*a.scratch);
        a.load=dmg::load_snapshot(*a.cpu,live,*a.before);
    };
    bus.serial_endpoint=&endpoint;
    cpu.step();
    require(attempt.called,"actual CPU serial instruction invokes the transport callback");
    require(attempt.cycle==70224,"actual SC write lands exactly on the frame quantum boundary");
    require(attempt.save==dmg::SnapshotResult::MachineMismatch &&
                attempt.load==dmg::SnapshotResult::MachineMismatch,
            "save and restore reject an active C++ instruction stack");
    std::array<std::uint32_t,4096> first_bits{},repeated_bits{};
    const auto serial_trace=[&](auto &trace) {
        for (auto &sample:trace) {
            bus.tick(1);
            sample=bus.serial_data() | (bus.serial_bits()<<8U) |
                   (static_cast<std::uint32_t>(bus.iflag)<<16U);
        }
    };
    serial_trace(first_bits);
    ok(dmg::save_snapshot(cpu,bus,scratch));
    bus.capture_serial_output=false;
    ok(dmg::load_snapshot(cpu,bus,before));
    require(bus.capture_serial_output,"serial capture configuration is machine state");
    require(bus.serial_endpoint==&endpoint,"restore retains caller-owned transport attachment");
    cpu.step();
    serial_trace(repeated_bits);
    dmg::MachineSnapshot repeated;
    ok(dmg::save_snapshot(cpu,bus,repeated));
    require(first_bits==repeated_bits && dmg::snapshot_equal(scratch,repeated),
            "frame-boundary SC replay preserves every serial bit, IF sample and complete machine state");
    bus.serial_output.assign(1024,'a');
    ok(dmg::save_snapshot(cpu,bus,before));
    std::string empty;
    bus.serial_output.swap(empty); // Explicitly remove the construction-time reserve.
    require(dmg::load_snapshot(cpu,bus,before)==dmg::SnapshotResult::SerialCapacity,
            "restore refuses an unreserved destination before any allocation");
}
void late_validation_does_not_partially_restore() {
    dmg::Bus bus(rom()); dmg::Cpu cpu(bus); cpu.prime();
    dmg::MachineSnapshot old_state,before,after;
    ok(dmg::save_snapshot(cpu,bus,old_state));
    cpu.a=0xA5;
    bus.poke(0xC123,0x7E);
    bus.serial_output.assign(2048,'b');
    bus.capture_serial_output=false;
    ok(dmg::save_snapshot(cpu,bus,before));
    // The last schema byte is an APU boolean. A valid checksum deliberately
    // forces complete semantic validation after all earlier device payloads.
    old_state.data[old_state.size-1]=2;
    old_state.checksum=dmg::snapshot_hash(old_state);
    require(dmg::load_snapshot(cpu,bus,old_state)==dmg::SnapshotResult::InvalidSnapshot,
            "late malformed field is rejected");
    ok(dmg::save_snapshot(cpu,bus,after));
    require(dmg::snapshot_equal(before,after),"late validation failure restores no earlier fields");
}
void allocation_and_latency() {
    dmg::Bus bus(rom(0x1B,4)); dmg::Cpu cpu(bus); cpu.prime();
    bus.cartridge.write(0,0x0A);
    for (unsigned bank=0;bank<16;++bank) {
        bus.cartridge.write(0x4000,static_cast<std::uint8_t>(bank));
        for (unsigned i=0;i<8192;++i)
            bus.cartridge.write(static_cast<std::uint16_t>(0xA000+i),static_cast<std::uint8_t>(bank*17+i));
    }
    bus.serial_output.resize(dmg::SnapshotSerialCapacity);
    for (std::size_t i=0;i<bus.serial_output.size();++i) bus.serial_output[i]=static_cast<char>(i);
    dmg::MachineSnapshot snapshot,again;
    dmg::SnapshotRing ring;
    ok(dmg::save_snapshot(cpu,bus,snapshot));
    require(snapshot.size<dmg::SnapshotCapacity,"maximum cartridge RAM and serial backlog fit");
    allocations=0; track_allocations=true;
    const auto saved=dmg::save_snapshot(cpu,bus,again);
    bus.serial_output.resize(1); bus.poke(0xC000,0xA5);
    const auto restored=dmg::load_snapshot(cpu,bus,snapshot);
    const auto captured=ring.capture(cpu,bus);
    const auto rewound=ring.rewind(cpu,bus,0);
    track_allocations=false;
    ok(saved); ok(restored); ok(captured); ok(rewound);
    require(allocations==0,"save, full serial restore, capture and rewind allocate zero times");
    ok(dmg::save_snapshot(cpu,bus,again));
    require(dmg::snapshot_equal(snapshot,again),"all128KiB cartridge RAM and64KiB serial roundtrip");
    constexpr unsigned repeats=500;
    std::array<double,repeats> save_times{},restore_times{};
    for (unsigned i=0;i<8;++i) {
        ok(dmg::save_snapshot(cpu,bus,again)); ok(dmg::load_snapshot(cpu,bus,snapshot));
    }
    bool successful=true;
    allocations=0; track_allocations=true;
    for (unsigned i=0;i<repeats;++i) {
        const auto start=std::chrono::steady_clock::now();
        const auto result_save=dmg::save_snapshot(cpu,bus,again);
        const auto middle=std::chrono::steady_clock::now();
        const auto result_restore=dmg::load_snapshot(cpu,bus,snapshot);
        const auto end=std::chrono::steady_clock::now();
        successful=successful && result_save==dmg::SnapshotResult::Ok && result_restore==dmg::SnapshotResult::Ok;
        save_times[i]=std::chrono::duration<double,std::micro>(middle-start).count();
        restore_times[i]=std::chrono::duration<double,std::micro>(end-middle).count();
    }
    track_allocations=false;
    require(successful,"all timed saves and restores succeed");
    require(allocations==0,"all500 warmed saves and restores allocate zero times");
    std::sort(save_times.begin(),save_times.end());
    std::sort(restore_times.begin(),restore_times.end());
    const auto median=[](const auto &times) { return (times[249]+times[250])/2; };
    std::cout<<"Snapshot benchmark: "<<snapshot.size<<" canonical bytes; 500 warmed calls; "
             <<allocations<<" measured allocations\n"
             <<"save_us median="<<median(save_times)<<" p95="<<save_times[474]<<" max="<<save_times.back()<<'\n'
             <<"restore_us median="<<median(restore_times)<<" p95="<<restore_times[474]<<" max="<<restore_times.back()<<'\n';
    std::cout<<"SNAPSHOT_BENCHMARK {\"bytes\":"<<snapshot.size<<",\"samples\":"<<repeats
             <<",\"save_median_us\":"<<median(save_times)<<",\"save_p95_us\":"<<save_times[474]
             <<",\"save_max_us\":"<<save_times.back()<<",\"restore_median_us\":"<<median(restore_times)
             <<",\"restore_p95_us\":"<<restore_times[474]<<",\"restore_max_us\":"<<restore_times.back()
             <<",\"allocations\":"<<allocations<<"}\n";
}
void ring_branching() {
    dmg::Bus bus(rom()); dmg::Cpu cpu(bus); cpu.prime(); dmg::SnapshotRing ring;
    require(ring.rewind(cpu,bus,0)==dmg::SnapshotResult::HistoryUnavailable,"empty ring rejected");
    for (unsigned frame=0;frame<65;++frame) {
        const auto target=bus.ppu.frames()+1;
        unsigned guard=0;
        while (bus.ppu.frames()<target && guard++<100000) cpu.step();
        require(guard<100000,"ring captures actual completed emulated frames");
        bus.set_buttons(static_cast<std::uint8_t>(frame));
        ok(ring.capture(cpu,bus));
    }
    require(ring.size()==60 && ring.at(60)==nullptr,"ring retains exactly60 newest frames");
    const auto expected=*ring.at(29);
    ok(ring.rewind(cpu,bus,29));
    require(ring.size()==31,"rewind truncates future history");
    dmg::MachineSnapshot actual;
    ok(dmg::save_snapshot(cpu,bus,actual));
    require(dmg::snapshot_equal(expected,actual),"ring restores all fields from the selected frame");
    replay(cpu,bus,200); ok(ring.capture(cpu,bus));
    require(ring.size()==32,"new execution creates a retained branch");
}
} // namespace

void *operator new(std::size_t count) {
    if (track_allocations) ++allocations;
    if (void *memory=std::malloc(count==0?1:count)) return memory;
    throw std::bad_alloc();
}
void *operator new[](std::size_t count) { return ::operator new(count); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory,std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory,std::size_t) noexcept { std::free(memory); }

int main() {
    try {
        sha_vectors(); device_replay(); interrupt_and_rtc(); rejection_is_atomic();
        execution_boundary_and_external_ownership(); late_validation_does_not_partially_restore();
        allocation_and_latency(); ring_branching();
        std::cout<<"PASS "<<assertions<<" snapshot assertions\n";
        return 0;
    } catch (const std::exception &error) {
        track_allocations=false;
        std::cerr<<"FAIL snapshot: "<<error.what()<<'\n';
        return 1;
    }
}

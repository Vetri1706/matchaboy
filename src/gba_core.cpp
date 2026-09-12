#include "gba_core.hpp"
#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/sio/lockstep.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/log.h>
#include <mgba-util/vfs.h>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstdio>
#include <limits>
#include <ctime>

namespace {
void log_message(mLogger*, int, mLogLevel level, const char *message, va_list args) {
    if (level == mLOG_FATAL || level == mLOG_ERROR) { std::vfprintf(stderr, message, args); std::fputc('\n', stderr); }
}
mLogger logger{log_message, nullptr};
}

struct GbaCore::Impl {
    mCore *core = nullptr;
    bool initialized = false, configured = false;
    bool audio_enabled = false;
    bool fresh = false;
    std::array<std::int16_t, 1024> audio_history{};
    unsigned audio_head = 0, audio_count = 0;
    std::array<std::uint32_t, 240*160> video{};
    std::filesystem::path save_path;
    std::vector<char> previous_save;
    GbaLinkedPair *linked = nullptr;
    ~Impl() {
        if (configured) mCoreConfigDeinit(&core->config);
        if (initialized) core->deinit(core);
        else std::free(core);
    }
};

GbaCore::GbaCore(const std::filesystem::path &path) : impl(std::make_unique<Impl>()) {
    mLogSetDefaultLogger(&logger);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open GBA game.");
    std::vector<char> rom((std::istreambuf_iterator<char>(input)), {});
    if (rom.size() < 0xC0 || rom.size() > 32*1024*1024)
        throw std::runtime_error("Invalid GBA ROM size.");
    auto *vf = VFileMemChunk(rom.data(), rom.size());
    if (!vf) throw std::runtime_error("Cannot allocate GBA ROM.");
    impl->core = mCoreCreate(mPLATFORM_GBA);
    auto *core = impl->core;
    if (!core || !core->init(core)) {
        vf->close(vf);
        throw std::runtime_error("Cannot initialize GBA core.");
    }
    impl->initialized = true;
    mCoreInitConfig(core, "matchaboy");
    impl->configured = true;
    mCoreConfigSetDefaultIntValue(&core->config, "skipBios", 1);
    mCoreConfigSetDefaultIntValue(&core->config, "volume", 0x100);
    mCoreLoadConfig(core);
    core->setVideoBuffer(core, impl->video.data(), 240);
    if (!core->isROM(vf) || !core->loadROM(core, vf)) {
        vf->close(vf);
        throw std::runtime_error("This file is not a supported GBA ROM.");
    }
    impl->save_path = path;
    impl->save_path.replace_extension(".matchaboy.sav");
    std::ifstream saved(impl->save_path, std::ios::binary);
    if (saved) {
        impl->previous_save.assign(std::istreambuf_iterator<char>(saved), {});
    }
    auto *save = VFileMemChunk(impl->previous_save.data(), impl->previous_save.size());
    if (!save) throw std::runtime_error("Cannot allocate GBA save memory.");
    if (!core->loadSave(core, save)) {
        save->close(save);
        throw std::runtime_error("Cannot load this game's Matchaboy save file.");
    }
    core->reset(core);
    impl->fresh=true;
}
GbaCore::~GbaCore() = default;
void GbaCore::run_frame() {
    if (impl->linked) throw std::logic_error("Advance linked consoles through GbaLinkedPair.");
    impl->fresh=false;
    impl->core->runFrame(impl->core);
    if (!impl->audio_enabled) {
        blip_clear(impl->core->getAudioChannel(impl->core, 0));
        blip_clear(impl->core->getAudioChannel(impl->core, 1));
    }
}
void GbaCore::enable_audio() {
    impl->core->setAudioBufferSize(impl->core, 2048);
    for (int channel=0; channel<2; ++channel) {
        auto *buffer = impl->core->getAudioChannel(impl->core, channel);
        blip_clear(buffer);
        blip_set_rates(buffer, impl->core->frequency(impl->core), 48000);
    }
    impl->audio_enabled = true;
}
std::size_t GbaCore::drain_audio(std::span<std::int16_t> destination) {
    auto *left = impl->core->getAudioChannel(impl->core, 0);
    auto *right = impl->core->getAudioChannel(impl->core, 1);
    const auto count = std::min({static_cast<int>(destination.size()/2), blip_samples_avail(left), blip_samples_avail(right)});
    if (count <= 0) return 0;
    blip_read_samples(left, destination.data(), count, true);
    blip_read_samples(right, destination.data()+1, count, true);
    for (int i = 0; i < count; ++i) {
        impl->audio_history[impl->audio_head*2] = destination[i*2];
        impl->audio_history[impl->audio_head*2+1] = destination[i*2+1];
        impl->audio_head = (impl->audio_head+1)%512;
        impl->audio_count = std::min(512U, impl->audio_count+1);
    }
    return static_cast<std::size_t>(count)*2;
}
void GbaCore::set_buttons(std::uint16_t buttons) { impl->core->setKeys(impl->core, buttons); }
std::uint64_t GbaCore::frames() const { return impl->core->frameCounter(impl->core); }
bool GbaCore::fresh() const { return impl->fresh; }
std::span<const std::uint32_t> GbaCore::pixels() const { return impl->video; }
void GbaCore::flush_save() {
    void *raw = nullptr;
    const auto count = impl->core->savedataClone(impl->core, &raw);
    std::unique_ptr<void, decltype(&std::free)> owner(raw, std::free);
    if (!count || !raw) return;
    const auto *bytes = static_cast<const char *>(raw);
    std::vector<char> current(bytes, bytes+count);
    if (current == impl->previous_save) return;
    auto temporary = impl->save_path;
    temporary += ".tmp";
    { std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
      out.write(current.data(), static_cast<std::streamsize>(current.size()));
      out.close();
      if (!out) throw std::runtime_error("Cannot save GBA progress. Move the game to a writable folder."); }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), impl->save_path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace GBA save file; the previous save was preserved.");
#else
    std::error_code error;
    std::filesystem::rename(temporary, impl->save_path, error);
    if (error) throw std::runtime_error("Cannot replace GBA save file; the previous save was preserved: " + error.message());
#endif
    impl->previous_save = std::move(current);
}

void GbaCore::step() {
    if (impl->linked) throw std::logic_error("Cannot step one console while the link cable is attached.");
    impl->fresh=false;
    impl->core->step(impl->core);
}
GbaInspection GbaCore::inspect(std::uint32_t memory_base) const {
    GbaInspection out;
    auto *core = impl->core;
    const auto *cpu = static_cast<const ARMCore *>(core->cpu);
    for (unsigned i=0; i<16; ++i) out.registers[i] = static_cast<std::uint32_t>(cpu->gprs[i]);
    out.cpsr = cpu->cpsr.packed;
    const bool thumb = (out.cpsr & 32) != 0;
    out.code_base = out.registers[15] & (thumb ? ~1U : ~3U);
    for (unsigned i=0; i<16; ++i) out.opcodes[i] = thumb ? core->rawRead16(core,out.code_base+i*2,-1) : core->rawRead32(core,out.code_base+i*4,-1);
    out.memory_base = memory_base;
    for (unsigned i=0; i<out.memory.size(); ++i) out.memory[i] = static_cast<std::uint8_t>(core->rawRead8(core,memory_base+i,-1));
    for (unsigned i=0; i<256; ++i) out.palette[i] = static_cast<std::uint16_t>(core->rawRead16(core,0x05000000+i*2,-1));
    const auto io = [core](unsigned offset) { return static_cast<std::uint16_t>(core->rawRead16(core,0x04000000+offset,-1)); };
    out.dispcnt=io(0); out.dispstat=io(4); out.vcount=io(6);
    out.sound_low=io(0x80); out.sound_high=io(0x82); out.sound_enable=io(0x84); out.sound_bias=io(0x88);
    out.siocnt=io(0x128); out.rcnt=io(0x134); out.serial_send=io(0x12A); out.interrupt_flags=io(0x202);
    for(unsigned i=0;i<4;++i)out.serial_multi[i]=io(0x120+i*2);
    out.audio_frames=impl->audio_count;
    for (unsigned i=0; i<out.audio_frames; ++i) {
        const auto index=(impl->audio_head+512-impl->audio_count+i)%512;
        out.audio[i*2]=impl->audio_history[index*2]; out.audio[i*2+1]=impl->audio_history[index*2+1];
    }
    return out;
}

namespace {
struct StateHash {
    std::uint64_t value = 14695981039346656037ULL;
    void bytes(std::span<const std::uint8_t> data) {
        for(auto byte:data) { value^=byte; value*=1099511628211ULL; }
    }
    void word(std::uint64_t data) {
        for(unsigned i=0;i<8;++i) { const auto byte=static_cast<std::uint8_t>(data>>(i*8)); bytes({&byte,1}); }
    }
};
}

std::vector<std::uint8_t> GbaCore::export_save() const {
    void *raw=nullptr;
    const auto size=impl->core->savedataClone(impl->core,&raw);
    std::unique_ptr<void,decltype(&std::free)> owner(raw,std::free);
    if(!size)return {};
    if(!raw)throw std::runtime_error("Cannot export GBA save memory.");
    const auto *bytes=static_cast<const std::uint8_t *>(raw);
    return {bytes,bytes+size};
}

void GbaCore::import_save(std::span<const std::uint8_t> bytes) {
    if(impl->linked)throw std::logic_error("Import saves before attaching the link cable.");
    if(bytes.size()>1024*1024)throw std::invalid_argument("GBA save exceeds the supported size.");
    auto *file=VFileMemChunk(bytes.data(),bytes.size());
    if(!file)throw std::runtime_error("Cannot allocate imported GBA save.");
    if(!impl->core->loadSave(impl->core,file)) {
        file->close(file);throw std::runtime_error("Cannot import GBA save.");
    }
    impl->core->reset(impl->core);
    impl->fresh=true;
    impl->audio_history.fill(0);impl->audio_head=0;impl->audio_count=0;
    impl->video.fill(0);
    if(impl->audio_enabled)enable_audio();
}

struct GbaLinkedPair::Impl {
    std::array<GbaCore *,2> machines;
    GBASIOLockstep cable{};
    std::array<GBASIOLockstepNode,2> nodes{};
    std::array<bool,2> awake{true,true};
    unsigned wait_mask=0;
    std::array<std::int32_t,2> posted{};
    std::array<std::uint32_t,2> previous_time{};
    std::array<std::uint64_t,2> elapsed{};
    std::uint64_t frame=0;
    std::string failure;
    std::array<mRTCGenericType,2> previous_rtc_type{};
    std::array<std::int64_t,2> previous_rtc_value{};

    explicit Impl(GbaCore &a,GbaCore &b):machines{&a,&b} {
        mLockstepInit(&cable.d);GBASIOLockstepInit(&cable);
        cable.d.context=this;
        cable.d.signal=[](mLockstep *c,unsigned mask) {
            auto &p=*static_cast<Impl *>(c->context);
            p.wait_mask&=~mask;
            const bool woke=!p.wait_mask&&!p.awake[0];
            if(!p.wait_mask)p.awake[0]=true;
            return woke;
        };
        cable.d.wait=[](mLockstep *c,unsigned mask) {
            auto &p=*static_cast<Impl *>(c->context);
            p.wait_mask|=mask;p.awake[0]=false;return true;
        };
        cable.d.addCycles=[](mLockstep *c,int id,std::int32_t cycles) {
            auto &p=*static_cast<Impl *>(c->context);
            if(cycles<0){p.failure="negative lockstep credit";return;}
            const unsigned target=id?static_cast<unsigned>(id):1;
            if(!id&&p.nodes[target].d.p->mode>SIO_MULTI)return;
            p.posted[target]+=cycles;
            if(!id){if(!p.awake[target])p.nodes[target].nextEvent+=p.posted[target];p.awake[target]=true;}
        };
        cable.d.useCycles=[](mLockstep *c,int id,std::int32_t cycles) {
            auto &p=*static_cast<Impl *>(c->context);
            p.posted[static_cast<unsigned>(id)]-=cycles;
            const auto remaining=p.posted[static_cast<unsigned>(id)];
            if(remaining<=0)p.awake[static_cast<unsigned>(id)]=false;
            return remaining;
        };
        cable.d.unusedCycles=[](mLockstep *c,int id) {
            return static_cast<Impl *>(c->context)->posted[static_cast<unsigned>(id)];
        };
        cable.d.unload=[](mLockstep *c,int id) {
            auto &p=*static_cast<Impl *>(c->context);
            if(id){p.posted[1]=0;p.awake[1]=true;p.wait_mask&=~2U;if(!p.wait_mask)p.awake[0]=true;}
            else {
                p.posted[1]+=p.nodes[0].eventDiff;
                if(!p.awake[1])p.nodes[1].nextEvent+=p.posted[1];
                p.awake[1]=true;p.awake[0]=true;p.wait_mask=0;
            }
        };
        for(unsigned i=0;i<2;++i) {
            auto *core=machines[i]->impl->core;
            auto *gba=static_cast<GBA *>(core->board);
            previous_time[i]=static_cast<std::uint32_t>(mTimingCurrentTime(&gba->timing));
            previous_rtc_type[i]=core->rtc.override;previous_rtc_value[i]=core->rtc.value;
            // Linked sessions advance their shared clock from emulated frames,
            // never host wall time. The session has an explicit fixed epoch.
            core->rtc.override=RTC_FAKE_EPOCH_UTC;core->rtc.value=946684800000LL;
            // A loaded battery file may carry an RTC offset reconstructed by
            // mktime in the host timezone. Linked mode starts one canonical
            // hardware clock, independent of that local-time save trailer.
            if(gba->memory.hw.devices&HW_RTC)GBAHardwareInitRTC(&gba->memory.hw);
            GBASIOLockstepNodeCreate(&nodes[i]);
            if(!GBASIOLockstepAttachNode(&cable,&nodes[i]))throw std::runtime_error("Cannot attach GBA link node.");
        }
        for(unsigned i=0;i<2;++i) {
            auto *gba=static_cast<GBA *>(machines[i]->impl->core->board);
            GBASIOSetDriver(&gba->sio,&nodes[i].d,SIO_MULTI);
            GBASIOSetDriver(&gba->sio,&nodes[i].d,SIO_NORMAL_32);
        }
    }

    ~Impl() {
        for(unsigned i=0;i<2;++i) {
            auto *core=machines[i]->impl->core;
            auto *gba=static_cast<GBA *>(core->board);
            // The same node occupies normal and multi slots. Unload it once,
            // then clear both slots without the upstream double-unload path.
            if(gba->sio.activeDriver==&nodes[i].d&&nodes[i].d.unload)nodes[i].d.unload(&nodes[i].d);
            mTimingDeschedule(&gba->timing,&nodes[i].event);
            gba->sio.activeDriver=nullptr;
            gba->sio.drivers.normal=nullptr;gba->sio.drivers.multiplayer=nullptr;
            core->rtc.override=previous_rtc_type[i];core->rtc.value=previous_rtc_value[i];
            machines[i]->impl->linked=nullptr;
        }
        GBASIOLockstepDetachNode(&cable,&nodes[1]);GBASIOLockstepDetachNode(&cable,&nodes[0]);
        mLockstepDeinit(&cable.d);
    }

    std::uint64_t link_hash() const {
        StateHash hash;hash.word(frame);hash.word(wait_mask);hash.word(cable.d.transferActive);
        hash.word(static_cast<std::uint32_t>(cable.d.transferCycles));
        hash.word(cable.attachedMulti);hash.word(cable.attachedNormal);
        for(unsigned i=0;i<2;++i){
            const auto &n=nodes[i];hash.word(awake[i]);hash.word(static_cast<std::uint32_t>(posted[i]));
            hash.word(elapsed[i]);hash.word(previous_time[i]);hash.word(static_cast<std::uint32_t>(n.nextEvent));
            hash.word(static_cast<std::uint32_t>(n.eventDiff));hash.word(n.mode);hash.word(n.transferFinished);
            hash.word(n.event.when);hash.word(n.normalSO);
        }
        for(unsigned i=0;i<4;++i){hash.word(cable.multiRecv[i]);hash.word(cable.normalRecv[i]);}
        return hash.value;
    }
};

GbaLinkedPair::GbaLinkedPair(GbaCore &a,GbaCore &b) {
    if(&a==&b||a.impl->linked||b.impl->linked)throw std::invalid_argument("Link requires two unattached consoles.");
    if(!a.fresh()||!b.fresh())throw std::invalid_argument("Reset/import both saves before linking.");
    impl=std::make_unique<Impl>(a,b);a.impl->linked=this;b.impl->linked=this;
}
GbaLinkedPair::~GbaLinkedPair()=default;
std::uint64_t GbaLinkedPair::frames() const{return impl->frame;}
void GbaLinkedPair::run_frame() {
    for(auto *machine:impl->machines)machine->impl->fresh=false;
    const auto target=(impl->frame+1)*280896;
    unsigned iterations=0;
    while(impl->elapsed[0]<target||impl->elapsed[1]<target) {
        if(!impl->failure.empty())throw std::runtime_error("GBA link: "+impl->failure);
        if(++iterations>2000000)throw std::runtime_error("GBA link scheduler did not reach the next frame.");
        int selected=-1;
        for(unsigned i=0;i<2;++i)if(impl->awake[i]&&(selected<0||impl->elapsed[i]<impl->elapsed[static_cast<unsigned>(selected)]))selected=static_cast<int>(i);
        if(selected<0)throw std::runtime_error("GBA link scheduler deadlock: both consoles waiting.");
        const auto index=static_cast<unsigned>(selected);
        auto *core=impl->machines[index]->impl->core;
        core->runLoop(core);
        auto *gba=static_cast<GBA *>(core->board);
        const auto now=static_cast<std::uint32_t>(mTimingCurrentTime(&gba->timing));
        impl->elapsed[index]+=static_cast<std::uint32_t>(now-impl->previous_time[index]);
        impl->previous_time[index]=now;
    }
    ++impl->frame;
    for(auto *machine:impl->machines)if(!machine->impl->audio_enabled) {
        auto *core=machine->impl->core;
        blip_clear(core->getAudioChannel(core,0));blip_clear(core->getAudioChannel(core,1));
    }
}
std::uint64_t GbaCore::digest() const {
    // mGBA's documented endian-stable savestate stores no pointers. Zero-fill
    // reserved bytes before serialization so allocator padding is excluded.
    std::vector<std::uint8_t> state(impl->core->stateSize(impl->core),0);
    if(!impl->core->saveState(impl->core,state.data()))throw std::runtime_error("Cannot serialize GBA digest.");
    StateHash hash;hash.bytes(state);hash.word(impl->core->getKeys(impl->core));
    hash.bytes(export_save());
    // Generic RTC configuration and offset are outside mGBA's raw savestate.
    hash.word(impl->core->rtc.override);hash.word(static_cast<std::uint64_t>(impl->core->rtc.value));
    const auto *gba=static_cast<const GBA *>(impl->core->board);
    hash.word(static_cast<std::uint64_t>(gba->memory.hw.rtc.offset));
    hash.word(static_cast<std::uint64_t>(gba->memory.hw.rtc.lastLatch));
    if(impl->linked)hash.word(impl->linked->impl->link_hash());
    return hash.value;
}
std::uint64_t GbaLinkedPair::digest() const {
    StateHash hash;hash.word(impl->machines[0]->digest());hash.word(impl->machines[1]->digest());return hash.value;
}

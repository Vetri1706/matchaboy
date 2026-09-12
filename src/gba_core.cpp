#include "gba_core.hpp"
#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
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
    std::array<std::int16_t, 1024> audio_history{};
    unsigned audio_head = 0, audio_count = 0;
    std::array<std::uint32_t, 240*160> video{};
    std::filesystem::path save_path;
    std::vector<char> previous_save;
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
}
GbaCore::~GbaCore() = default;
void GbaCore::run_frame() {
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

void GbaCore::step() { impl->core->step(impl->core); }
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
    out.audio_frames=impl->audio_count;
    for (unsigned i=0; i<out.audio_frames; ++i) {
        const auto index=(impl->audio_head+512-impl->audio_count+i)%512;
        out.audio[i*2]=impl->audio_history[index*2]; out.audio[i*2+1]=impl->audio_history[index*2+1];
    }
    return out;
}

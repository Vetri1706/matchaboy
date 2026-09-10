#include "dmg/mmu.hpp"
#include "dmg/oam.hpp"
#include "dmg/snapshot.hpp"
#ifdef ENABLE_AUTOPSY
#include "dmg/autopsy.hpp"
#endif
#include <utility>
namespace dmg {
namespace {
void initialize_post_boot_vram(Ppu &ppu, const Cartridge &cartridge) {
    // This machine starts at the cartridge's post-boot entry point. Recreate
    // the generic VRAM handoff as well as the CPU/MMIO defaults: header logo
    // bits are doubled horizontally and vertically, then the final tilemap
    // and registered-mark tile remain in memory. This runs for every cartridge.
    // DMG bootstrap operations 0021..0053 and 0095..00A7, documented at:
    // https://gbdev.gg8.se/wiki/articles/Gameboy_Bootstrap_ROM
    ppu.vram.fill(0);
    unsigned destination = 0x10;
    for (unsigned source = 0x104; source < 0x134; ++source) {
        const auto byte = cartridge.read(static_cast<std::uint16_t>(source));
        for (unsigned part = 0; part < 2; ++part) {
            const unsigned nibble = (byte >> (4U * (1U - part))) & 15U;
            unsigned expanded = 0;
            for (unsigned bit = 0; bit < 4; ++bit)
                expanded = (expanded << 2U) | (((nibble >> (3U - bit)) & 1U) * 3U);
            ppu.vram[destination] = ppu.vram[destination + 2] =
                static_cast<std::uint8_t>(expanded);
            destination += 4;
        }
    }
    constexpr std::array<std::uint8_t, 8> registered_mark{
        0x3C, 0x42, 0xB9, 0xA5, 0xB9, 0xA5, 0x42, 0x3C};
    for (unsigned row = 0; row < registered_mark.size(); ++row)
        ppu.vram[0x190U + row * 2U] = registered_mark[row];
    for (unsigned tile = 0; tile < 12; ++tile) {
        ppu.vram[0x1904U + tile] = static_cast<std::uint8_t>(tile + 1U);
        ppu.vram[0x1924U + tile] = static_cast<std::uint8_t>(tile + 13U);
    }
    ppu.vram[0x1910] = 0x19;
}
} // namespace
Bus::Bus(std::vector<std::uint8_t> rom, std::optional<std::size_t> ram_override)
    : ppu(iflag), timer(iflag), cartridge(std::move(rom), ram_override) {
    serial_output.reserve(SnapshotSerialCapacity);
    apu.initialize_postboot();
    initialize_post_boot_vram(ppu, cartridge);
    ppu.initialize_postboot();
    io_.fill(0xFF);
    io_[0x10] = 0x80;
    io_[0x11] = 0xBF;
    io_[0x12] = 0xF3;
    io_[0x14] = 0xBF;
    io_[0x16] = 0x3F;
    io_[0x17] = 0;
    io_[0x19] = 0xBF;
    io_[0x1A] = 0x7F;
    io_[0x1B] = 0xFF;
    io_[0x1C] = 0x9F;
    io_[0x1E] = 0xBF;
    io_[0x20] = 0xFF;
    io_[0x21] = 0;
    io_[0x22] = 0;
    io_[0x23] = 0xBF;
    io_[0x24] = 0x77;
    io_[0x25] = 0xF3;
    io_[0x26] = 0xF1;
    io_[0x46] = 0xFF;
    io_[0x50] = 1;
}
std::uint8_t Bus::joypad() const {
    unsigned low = 15;
    if ((joy_select_ & 0x10U) == 0)
        low &= ~(buttons_ & 15U);
    if ((joy_select_ & 0x20U) == 0)
        low &= ~((buttons_ >> 4U) & 15U);
    return static_cast<std::uint8_t>(0xC0U | joy_select_ | low);
}
void Bus::set_buttons(std::uint8_t pressed) {
    const auto old = joypad();
    buttons_ = pressed;
    if ((old & ~joypad() & 15U) != 0)
        iflag |= 0x10;
}
bool Bus::cpu_blocked(std::uint16_t address) const {
    if (!dma_active_ || address >= 0xFF00)
        return false;
    if (address >= 0xFE00)
        return true;
    // VRAM has its own bus. In particular the hardware-verified Mooneye CPU
    // timing ROMs execute from ROM/WRAM while a VRAM-source DMA is in flight.
    const bool source_vram = dma_source_ >= 0x8000 && dma_source_ < 0xA000;
    const bool target_vram = address >= 0x8000 && address < 0xA000;
    return source_vram == target_vram;
}
std::uint8_t Bus::peek(std::uint16_t address) const {
    if (address < 0x8000)
        return cartridge.read(address);
    if (address < 0xA000)
        return ppu.vram[address - 0x8000U];
    if (address < 0xC000)
        return cartridge.read(address);
    if (address < 0xE000)
        return wram_[address - 0xC000U];
    if (address < 0xFE00)
        return wram_[address - 0xE000U];
    if (address < 0xFEA0)
        return ppu.oam[address - 0xFE00U];
    if (address < 0xFF00)
        return 0xFF;
    if (address < 0xFF80) {
        switch (address) {
        case 0xFF00:
            return joypad();
        case 0xFF01:
            return serial_data_;
        case 0xFF02:
            return static_cast<std::uint8_t>(serial_control_ | 0x7EU);
        case 0xFF04:
        case 0xFF05:
        case 0xFF06:
        case 0xFF07:
            return timer.read(address);
        case 0xFF0F:
            return static_cast<std::uint8_t>(iflag | 0xE0U);
        case 0xFF40:
        case 0xFF41:
        case 0xFF42:
        case 0xFF43:
        case 0xFF44:
        case 0xFF45:
        case 0xFF47:
        case 0xFF48:
        case 0xFF49:
        case 0xFF4A:
        case 0xFF4B:
            return ppu.read(address);
        case 0xFF46:
            return io_[0x46];
        case 0xFF50:
            return 0xFF;
        default:
            if (address >= 0xFF10 && address <= 0xFF3F)
                return apu.read(address);
            return 0xFF;
        }
    }
    return address == 0xFFFF ? ie : hram_[address - 0xFF80U];
}
std::uint8_t Bus::read(std::uint16_t address, bool idu, std::uint8_t *interrupt_sample) {
#ifdef ENABLE_AUTOPSY
    if (autopsy != nullptr) {
        autopsy->access(AccessKind::Read, address);
        if (interrupt_sample != nullptr)
            autopsy->access(AccessKind::Execute, address);
    }
#endif
    // Advance to the CPU bus strobe, then leave one trailing clock in this
    // machine cycle. Opcode fetches latch IRQ requests alongside their IR data.
    // The phase convention is exercised by the external instruction/memory
    // timing ROMs; it does not infer IRQ timing from an opcode's cycle count.
    tick(3);
    const bool granted = [&]() {
        if (cpu_blocked(address))
            return false;
        if (address >= 0xFE00 && address < 0xFF00)
            corrupt_oam(ppu, idu ? OamAccess::ReadIdu : OamAccess::Read);
        if (address >= 0x8000 && address < 0xA000 && ppu.vram_blocked())
            return false;
        if (address >= 0xFE00 && address < 0xFEA0 && ppu.oam_blocked())
            return false;
        return true;
    }();
    if (interrupt_sample != nullptr)
        *interrupt_sample = pending_interrupts();
    const auto value = granted ? peek(address) : static_cast<std::uint8_t>(0xFF);
    tick(1);
    return value;
}
void Bus::write(std::uint16_t address, std::uint8_t value) {
#ifdef ENABLE_AUTOPSY
    if (autopsy != nullptr)
        autopsy->access(AccessKind::Write, address);
#endif
    if (address == 0xFF42 || address == 0xFF43) {
        // The fetch address circuit sees scroll data during the CPU write;
        // architectural readback remains latched at the normal T3 strobe.
        ppu.drive_scroll_bus(address, value);
        tick(3);
        poke(address, value);
        ppu.release_scroll_bus();
        tick(1);
        return;
    }
    if (address == 0xFF40) {
        // LCDC's transparent consumers see the driven bus during the write.
        // CPU readback and LCD power transitions still latch at T3.
        ppu.drive_control_bus(value);
        tick(3);
        poke(address, value);
        ppu.release_control_bus();
        tick(1);
        return;
    }
    if (address >= 0xFF47 && address <= 0xFF49) {
        // The pixel mux observes the live palette write bus before the T3
        // architectural latch; its first dot combines the old and new bits.
        ppu.drive_palette_bus(address, value, true);
        tick(1);
        ppu.drive_palette_bus(address, value, false);
        tick(2);
        poke(address, value);
        ppu.release_palette_bus();
        tick(1);
        return;
    }
    tick(3);
    if (!cpu_blocked(address) && address >= 0xFE00 && address < 0xFF00)
        corrupt_oam(ppu, OamAccess::Write);
    poke(address, value);
    tick(1);
}
void Bus::idu_idle(std::uint16_t address) {
    tick(3);
    if (!dma_active_ && address >= 0xFE00 && address < 0xFF00)
        corrupt_oam(ppu, OamAccess::Write);
    tick(1);
}
void Bus::poke(std::uint16_t address, std::uint8_t value) {
    if (cpu_blocked(address) && address != 0xFF46)
        return;
    if (address < 0x8000) {
        cartridge.write(address, value);
        return;
    }
    if (address < 0xA000) {
        if (!ppu.vram_blocked(true))
            ppu.vram[address - 0x8000U] = value;
        return;
    }
    if (address < 0xC000) {
        cartridge.write(address, value);
        return;
    }
    if (address < 0xE000) {
        wram_[address - 0xC000U] = value;
        return;
    }
    if (address < 0xFE00) {
        wram_[address - 0xE000U] = value;
        return;
    }
    if (address < 0xFEA0) {
        if (!ppu.oam_blocked(true))
            ppu.oam[address - 0xFE00U] = value;
        return;
    }
    if (address < 0xFF00)
        return;
    if (address < 0xFF80) {
        switch (address) {
        case 0xFF00: {
            const auto old = joypad();
            joy_select_ = static_cast<std::uint8_t>(value & 0x30U);
            if ((old & ~joypad() & 15U) != 0)
                iflag |= 0x10;
            return;
        }
        case 0xFF01:
            serial_data_ = value;
            return;
        case 0xFF02:
            if (serial_endpoint != nullptr && serial_endpoint->on_start != nullptr)
                serial_endpoint->on_start(serial_endpoint->context, *this, cycles_, serial_data_,
                                          static_cast<std::uint8_t>(value & 0x81U));
            serial_control_ = static_cast<std::uint8_t>(value & 0x81U);
            serial_phase_ = 0;
            serial_bits_ = 0;
            serial_sent_ = 0;
            return;
        case 0xFF04:
            reset_divider();
            return;
        case 0xFF05:
        case 0xFF06:
        case 0xFF07:
            timer.write(address, value);
            return;
        case 0xFF0F:
            iflag = static_cast<std::uint8_t>(value | 0xE0U);
            return;
        case 0xFF40:
        case 0xFF41:
        case 0xFF42:
        case 0xFF43:
        case 0xFF44:
        case 0xFF45:
        case 0xFF47:
        case 0xFF48:
        case 0xFF49:
        case 0xFF4A:
        case 0xFF4B:
            ppu.write(address, value);
            return;
        case 0xFF46:
            // The next M-cycle still uses the old bus owner, including DMA restarts.
            // Five ticks places activation after the next CPU bus sample.
            io_[0x46] = value;
            dma_pending_source_ = static_cast<std::uint16_t>(static_cast<unsigned>(value) << 8U);
            dma_startup_ = 5;
            return;
        case 0xFF50:
            io_[0x50] |= value;
            return;
        default:
            if (address >= 0xFF10 && address <= 0xFF3F)
                apu.write(address, value);
            return;
        }
    }
    if (address == 0xFFFF)
        ie = value;
    else
        hram_[address - 0xFF80U] = value;
}
void Bus::acknowledge_interrupt(unsigned bit) {
    if (bit < 5)
        iflag = static_cast<std::uint8_t>(iflag & ~(1U << bit));
}
void Bus::reset_divider() {
    const bool frame_clock = (timer.divider() & 0x1000U) != 0;
    timer.reset_divider();
    if (frame_clock)
        apu.frame_sequencer_tick();
}
bool Bus::shift_serial(bool input_high) {
    const bool output_high = (serial_data_ & 0x80U) != 0;
    serial_sent_ =
        static_cast<std::uint8_t>((serial_sent_ << 1U) | static_cast<unsigned>(output_high));
    serial_data_ =
        static_cast<std::uint8_t>((serial_data_ << 1U) | static_cast<unsigned>(input_high));
    if (++serial_bits_ == 8) {
        if (capture_serial_output)
            serial_output.push_back(static_cast<char>(serial_sent_));
        serial_control_ &= 1;
        iflag |= 8;
    }
    return output_high;
}
bool Bus::serial_clock(bool input_high) {
    if ((serial_control_ & 0x81U) == 0x80)
        return shift_serial(input_high);
    return (serial_data_ & 0x80U) != 0;
}
void Bus::tick_dma() {
    if (dma_startup_ != 0 && --dma_startup_ == 0) {
        dma_source_ = dma_pending_source_;
        dma_index_ = 0;
        dma_phase_ = 0;
        dma_active_ = true;
        ppu.set_dma_active(true);
        // Ownership begins now. The first complete four-dot byte transfer
        // follows it; counting this activation dot twice ends DMA too early.
        return;
    }
    if (!dma_active_)
        return;
    if (++dma_phase_ != 4)
        return;
    dma_phase_ = 0;
    std::uint16_t source = static_cast<std::uint16_t>(dma_source_ + dma_index_);
    if (source >= 0xE000)
        source = static_cast<std::uint16_t>(source - 0x2000U);
    ppu.oam[dma_index_] = peek(source);
    if (++dma_index_ == 160) {
        dma_active_ = false;
        ppu.set_dma_active(false);
    }
}
void Bus::tick(unsigned t_cycles) {
    for (unsigned i = 0; i < t_cycles; ++i) {
        ++cycles_;
        if (serial_endpoint != nullptr && serial_endpoint->on_tick != nullptr)
            serial_endpoint->on_tick(serial_endpoint->context, *this, cycles_);
        const bool frame_clock = (timer.divider() & 0x1000U) != 0;
        timer.tick();
        if (frame_clock && (timer.divider() & 0x1000U) == 0)
            apu.frame_sequencer_tick();
        apu.tick();
#ifdef ENABLE_AUTOPSY
        if (autopsy != nullptr && (cycles_ & 31U) == 0)
            autopsy->audio_tick(apu, cycles_);
#endif
        ppu.tick();
        cartridge.tick();
        tick_dma();
        if ((serial_control_ & 0x81U) == 0x81 && ++serial_phase_ == 512) {
            serial_phase_ = 0;
            const bool incoming = serial_endpoint == nullptr || serial_endpoint->exchange_bit == nullptr
                ? true : serial_endpoint->exchange_bit(serial_endpoint->context, *this, cycles_,
                                                       (serial_data_ & 0x80U) != 0);
            shift_serial(incoming);
        }
    }
}
} // namespace dmg

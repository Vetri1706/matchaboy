#include "dmg/mbc.hpp"
#include "dmg/snapshot.hpp"
#include <stdexcept>
#include <string>
#include <utility>
namespace dmg {
Cartridge::Cartridge(std::vector<std::uint8_t> rom, std::optional<std::size_t> ram_override)
    : rom_(std::move(rom)) {
    if (rom_.size() < 0x150)
        throw std::runtime_error("ROM is shorter than its cartridge header");
    type_ = rom_[0x147];
    bool supports_ram = false;
    switch (type_) {
    case 0x00:
        break;
    case 0x08:
    case 0x09:
        supports_ram = true;
        break;
    case 0x01:
        controller_ = Controller::Mbc1;
        break;
    case 0x02:
    case 0x03:
        controller_ = Controller::Mbc1;
        supports_ram = true;
        break;
    case 0x0F:
        controller_ = Controller::Mbc3;
        has_rtc_ = true;
        break;
    case 0x10:
        controller_ = Controller::Mbc3;
        supports_ram = true;
        has_rtc_ = true;
        break;
    case 0x11:
        controller_ = Controller::Mbc3;
        break;
    case 0x12:
    case 0x13:
        controller_ = Controller::Mbc3;
        supports_ram = true;
        break;
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
        controller_ = Controller::Mbc5;
        supports_ram = type_ != 0x19 && type_ != 0x1C;
        has_rumble_ = type_ >= 0x1C;
        break;
    default:
        throw std::runtime_error("unsupported cartridge controller type " + std::to_string(type_));
    }
    const unsigned size_code = rom_[0x148];
    if (size_code > (controller_ == Controller::Mbc5 ? 8U : 6U))
        throw std::runtime_error("unsupported ROM size for this controller");
    const std::size_t expected_size = std::size_t{0x8000} << size_code;
    if (rom_.size() != expected_size)
        throw std::runtime_error("ROM length disagrees with cartridge header");
    if (controller_ == Controller::None && rom_.size() != 0x8000)
        throw std::runtime_error("ROM-only cartridges require exactly 32 KiB ROM");
    if (rom_[0x143] == 0xC0)
        throw std::runtime_error("CGB-only cartridge cannot execute on a DMG");
    std::size_t ram_size = 0;
    switch (rom_[0x149]) {
    case 0:
        break;
    case 1:
        ram_size = 0x800;
        break;
    case 2:
        ram_size = 0x2000;
        break;
    case 3:
        ram_size = 0x8000;
        break;
    case 4:
        ram_size = 0x20000;
        break;
    case 5:
        ram_size = 0x10000;
        break;
    default:
        throw std::runtime_error("unsupported cartridge RAM size (maximum 128 KiB)");
    }
    if (ram_override) {
        ram_size = *ram_override;
        if (ram_size != 0 && ram_size != 0x800 && ram_size != 0x2000 && ram_size != 0x8000)
            throw std::runtime_error("RAM override must be 0, 2048, 8192 or 32768 bytes");
    }
    if (!supports_ram && ram_size != 0)
        throw std::runtime_error("cartridge RAM size contradicts controller type");
    if (controller_ != Controller::Mbc5 && ram_size > 0x8000)
        throw std::runtime_error("this controller cannot address more than 32 KiB RAM");
    if (has_rumble_ && ram_size > 0x10000)
        throw std::runtime_error("rumble MBC5 cannot address more than 64 KiB RAM");
    if (controller_ == Controller::None && ram_size > 0x2000)
        throw std::runtime_error("unbanked cartridge cannot address more than 8 KiB RAM");
    ram_.resize(ram_size, 0xFF);
    ram_enabled_ = controller_ == Controller::None;
    rom_identity_ = snapshot_rom_identity(rom_);
}
std::size_t Cartridge::ram_index(std::uint16_t address) const {
    unsigned bank = 0;
    if (controller_ == Controller::Mbc1 && mode_ != 0)
        bank = high_bank_;
    else if (controller_ == Controller::Mbc3 || controller_ == Controller::Mbc5)
        bank = ram_select_;
    return (std::size_t{bank} * 0x2000 + address - 0xA000U) % ram_.size();
}
std::uint8_t Cartridge::read(std::uint16_t address) const {
    if (address < 0x8000) {
        std::size_t bank = address < 0x4000 ? 0 : 1;
        if (controller_ == Controller::Mbc1) {
            if (address < 0x4000)
                bank = mode_ != 0 ? static_cast<unsigned>(high_bank_) << 5U : 0;
            else
                bank = (static_cast<unsigned>(high_bank_) << 5U) | low_bank_;
        } else if (controller_ == Controller::Mbc3 && address >= 0x4000)
            bank = low_bank_;
        else if (controller_ == Controller::Mbc5 && address >= 0x4000)
            bank = (static_cast<unsigned>(high_bank_) << 8U) | low_bank_;
        bank %= rom_banks();
        return rom_[bank * 0x4000 + (address & 0x3FFFU)];
    }
    if (address < 0xA000 || address >= 0xC000 || !ram_enabled_)
        return 0xFF;
    if (controller_ == Controller::Mbc3 && ram_select_ >= 4) {
        if (has_rtc_ && ram_select_ >= 8 && ram_select_ <= 12)
            return latched_rtc_[ram_select_ - 8U];
        return 0xFF;
    }
    return ram_.empty() ? 0xFF : ram_[ram_index(address)];
}
void Cartridge::write(std::uint16_t address, std::uint8_t value) {
    if (address < 0x8000) {
        if (controller_ == Controller::None)
            return;
        if (address < 0x2000)
            ram_enabled_ = (value & 0x0FU) == 0x0A;
        else if (address < 0x4000) {
            if (controller_ == Controller::Mbc5) {
                if (address < 0x3000)
                    low_bank_ = value;
                else
                    high_bank_ = static_cast<std::uint8_t>(value & 1U);
                return;
            }
            low_bank_ = static_cast<std::uint8_t>(
                value & (controller_ == Controller::Mbc1 ? 0x1FU : 0x7FU));
            if (low_bank_ == 0)
                low_bank_ = 1;
        } else if (address < 0x6000) {
            if (controller_ == Controller::Mbc1)
                high_bank_ = static_cast<std::uint8_t>(value & 3U);
            else if (controller_ == Controller::Mbc5) {
                ram_select_ = static_cast<std::uint8_t>(value & (has_rumble_ ? 7U : 15U));
                rumble_active_ = has_rumble_ && (value & 8U) != 0;
            } else
                ram_select_ = value;
        } else if (controller_ == Controller::Mbc1)
            mode_ = static_cast<std::uint8_t>(value & 1U);
        else if (controller_ == Controller::Mbc3) {
            if (latch_previous_ == 0 && value == 1 && has_rtc_)
                latched_rtc_ = rtc_;
            latch_previous_ = value;
        }
        return;
    }
    if (address < 0xA000 || address >= 0xC000 || !ram_enabled_)
        return;
    if (controller_ == Controller::Mbc3 && ram_select_ >= 4) {
        if (has_rtc_ && ram_select_ >= 8 && ram_select_ <= 12) {
            constexpr std::array<std::uint8_t, 5> masks{0x3F, 0x3F, 0x1F, 0xFF, 0xC1};
            rtc_[ram_select_ - 8U] = static_cast<std::uint8_t>(value & masks[ram_select_ - 8U]);
            if (ram_select_ == 8)
                rtc_subsecond_ = 0;
        }
        return;
    }
    if (!ram_.empty())
        ram_[ram_index(address)] = value;
}
void Cartridge::rtc_second() {
    if (++rtc_[0] < 60)
        return;
    rtc_[0] = 0;
    if (++rtc_[1] < 60)
        return;
    rtc_[1] = 0;
    if (++rtc_[2] < 24)
        return;
    rtc_[2] = 0;
    unsigned day = ((static_cast<unsigned>(rtc_[4]) & 1U) << 8U) | rtc_[3];
    ++day;
    if (day > 511)
        rtc_[4] |= 0x80;
    rtc_[3] = static_cast<std::uint8_t>(day);
    rtc_[4] = static_cast<std::uint8_t>((rtc_[4] & 0xC0U) | ((day >> 8U) & 1U));
}
void Cartridge::tick() {
    if (has_rtc_ && (rtc_[4] & 0x40U) == 0 && ++rtc_subsecond_ == 4194304U) {
        rtc_subsecond_ = 0;
        rtc_second();
    }
}
} // namespace dmg

#pragma once
#include <filesystem>
namespace dmg { class Bus; }
void load_gameboy_save(const std::filesystem::path &,dmg::Bus &);
void flush_gameboy_save(const std::filesystem::path &,const dmg::Bus &);

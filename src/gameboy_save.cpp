#include "gameboy_save.hpp"
#include "dmg/mmu.hpp"
#include <fstream>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif
namespace {
bool battery(const dmg::Bus &bus){
    switch(bus.cartridge.type()){
        case 3:case 9:case 0x0F:case 0x10:case 0x13:case 0x1B:case 0x1E:return true;
        default:return false;
    }
}
std::filesystem::path save_path(std::filesystem::path rom){rom.replace_extension(".matchaboy.sav");return rom;}
std::vector<std::uint8_t> read(const std::filesystem::path &path){
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read Game Boy save: "+path.string());
    if(std::filesystem::file_size(path)>131072)throw std::runtime_error("Game Boy save file is too large.");
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file),{}};
    if(file.bad())throw std::runtime_error("Game Boy save read failed.");
    return bytes;
}
}
void load_gameboy_save(const std::filesystem::path &rom,dmg::Bus &bus){
    const auto path=save_path(rom);
    if(battery(bus)&&std::filesystem::exists(path))bus.cartridge.restore_ram(read(path));
}
void flush_gameboy_save(const std::filesystem::path &rom,const dmg::Bus &bus){
    if(!battery(bus)||bus.cartridge.ram().empty())return;
    const auto path=save_path(rom);const auto &bytes=bus.cartridge.ram();
    if(std::filesystem::exists(path)&&read(path)==bytes)return;
    auto temporary=path;temporary+=".tmp-"+std::to_string(std::random_device{}());
    try{
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
         file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));file.close();
         if(!file)throw std::runtime_error("Cannot write Game Boy save. Move the ROM to a writable folder.");}
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace Game Boy save; the previous file was preserved.");
#else
        std::filesystem::rename(temporary,path);
#endif
    }catch(...){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw;}
}

#include "arcade_library.hpp"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#include "arcade_catalog.inc"

bool matches(const std::filesystem::path &path, std::span<const char> bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char,4096> buffer{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto size = std::min(buffer.size(),bytes.size()-offset);
        if (!input.read(buffer.data(),static_cast<std::streamsize>(size)) ||
            !std::equal(buffer.begin(),buffer.begin()+size,bytes.begin()+offset)) return false;
        offset += size;
    }
    return input.peek() == std::char_traits<char>::eof();
}
std::filesystem::path materialize(const std::filesystem::path &directory,
                                 const std::string &filename, std::span<const char> bytes) {
    std::filesystem::create_directories(directory);
    const auto file = directory/filename;
    const bool cached = matches(file,bytes);
    // PID suffix lets separate player processes populate the same library.
    const auto temporary = std::filesystem::path(file.wstring()+L"."+std::to_wstring(GetCurrentProcessId())+L".tmp");
    try {
        {
            std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
            // Check directory writability even when the ROM matches: cartridge
            // saves beside it must remain writable as well.
            if (!cached) output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
            output.close();
            if (!output) throw std::runtime_error("Cannot write the bundled game.");
        }
        if (cached) {
            std::filesystem::remove(temporary);
            return file;
        }
        if (!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot prepare the bundled game file.");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary,ignored);
        throw;
    }
    return file;
}
}

std::span<const ArcadeGame> arcade_games() { return catalog; }

static std::filesystem::path prepare_resource(unsigned id, const std::string &filename) {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    const auto loaded = resource ? LoadResource(module,resource) : nullptr;
    const auto *data = loaded ? static_cast<const char *>(LockResource(loaded)) : nullptr;
    const auto length = resource ? SizeofResource(module,resource) : 0;
    if (!data || !length) throw std::runtime_error("This build is missing its arcade games.");
    const std::span<const char> bytes(data,length);
    std::vector<wchar_t> override_path(32768);
    const auto override_size = GetEnvironmentVariableW(L"MATCHA_GAME_LIBRARY", override_path.data(),
                                                       static_cast<DWORD>(override_path.size()));
    if (override_size) {
        if (override_size >= override_path.size()) throw std::runtime_error("Game library path is too long.");
        const std::filesystem::path directory(override_path.data());
        if (!directory.is_absolute()) throw std::runtime_error("MATCHA_GAME_LIBRARY must be an absolute path.");
        return materialize(directory,filename,bytes);
    }
    std::vector<wchar_t> executable(32768);
    const auto count = GetModuleFileNameW(nullptr,executable.data(),static_cast<DWORD>(executable.size()));
    if (count && count < executable.size()) {
        try {
            return materialize(std::filesystem::path(executable.data()).parent_path()/L"Matchaboy Data"/L"Library",filename,bytes);
        } catch (const std::exception &) { /* Read-only install location: use per-user storage. */ }
    }
    PWSTR user_directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&user_directory)))
        throw std::runtime_error("Move Matchaboy to a writable folder to play the arcade.");
    const auto directory = std::filesystem::path(user_directory)/L"Matchaboy"/L"Library";
    CoTaskMemFree(user_directory);
    return materialize(directory,filename,bytes);
}

std::filesystem::path arcade_rom_path(std::size_t index) {
    if (index >= catalog.size()) throw std::out_of_range("Unknown arcade game.");
    return prepare_resource(static_cast<unsigned>(201 + index), catalog[index].rom_filename);
}
std::filesystem::path arcade_credits_path() {
    return prepare_resource(300, "Game credits and licenses.txt");
}

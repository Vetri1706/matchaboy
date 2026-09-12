#include "arcade_library.hpp"
#import <Foundation/Foundation.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
#include "arcade_catalog.inc"

bool matches(const std::filesystem::path &path, std::span<const char> bytes) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    std::array<char, 4096> buffer{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = std::min(buffer.size(), bytes.size() - offset);
        if (!input.read(buffer.data(), static_cast<std::streamsize>(count)) ||
            !std::equal(buffer.begin(), buffer.begin() + count, bytes.begin() + offset)) return false;
        offset += count;
    }
    return input.peek() == std::char_traits<char>::eof();
}

std::filesystem::path materialize(const std::filesystem::path &directory,
                                 const std::string &filename, std::span<const char> bytes) {
    std::filesystem::create_directories(directory);
    const auto file = directory / filename;
    const bool cached = matches(file, bytes);
    // Always probe the user directory even when its ROM is cached: GBA saves
    // must remain writable next to the cartridge. mkstemp also permits two app
    // processes to populate this directory without sharing a temporary file.
    auto name = (directory / (filename + ".XXXXXX")).string();
    std::vector<char> temporary(name.begin(), name.end());
    temporary.push_back('\0');
    int descriptor = mkstemp(temporary.data());
    if (descriptor < 0) throw std::runtime_error("Cannot write the Matchaboy game library: " +
                                                std::string(std::strerror(errno)));
    try {
        std::size_t offset = 0;
        while (!cached && offset < bytes.size()) {
            const auto count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw std::runtime_error("Cannot prepare the bundled game.");
            offset += static_cast<std::size_t>(count);
        }
        if (!cached && fsync(descriptor) != 0)
            throw std::runtime_error("Cannot flush the bundled game.");
        const int result = close(descriptor);
        descriptor = -1;
        if (result != 0) throw std::runtime_error("Cannot close the bundled game.");
        if (cached) std::filesystem::remove(temporary.data());
        else std::filesystem::rename(temporary.data(), file);
    } catch (...) {
        if (descriptor >= 0) close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary.data(), ignored);
        throw;
    }
    return file;
}
}

std::span<const ArcadeGame> arcade_games() { return catalog; }

std::filesystem::path arcade_rom_path(std::size_t index) {
    if (index >= catalog.size()) throw std::out_of_range("Unknown arcade game.");
    @autoreleasepool {
        const auto &game = catalog[index];
        const std::string filename = game.rom_filename;
        NSString *resources = [[NSBundle mainBundle] resourcePath];
        if (!resources) throw std::runtime_error("This build is missing its arcade resources.");
        const auto source = std::filesystem::path([resources fileSystemRepresentation]) / "Arcade" / filename;
        std::ifstream input(source, std::ios::binary);
        if (!input) throw std::runtime_error("This build is missing its bundled game: " + filename);
        std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (input.bad() || bytes.empty() || bytes.size() > 32 * 1024 * 1024)
            throw std::runtime_error("Invalid bundled game: " + filename);
        if (const char *override_path = std::getenv("MATCHA_GAME_LIBRARY"); override_path && *override_path) {
            const std::filesystem::path directory(override_path);
            if (!directory.is_absolute()) throw std::runtime_error("MATCHA_GAME_LIBRARY must be an absolute path.");
            return materialize(directory, filename, bytes);
        }
        NSArray<NSString *> *locations = NSSearchPathForDirectoriesInDomains(
            NSApplicationSupportDirectory, NSUserDomainMask, YES);
        if ([locations count] == 0) throw std::runtime_error("Cannot locate your Application Support directory.");
        const auto directory = std::filesystem::path([[locations firstObject] fileSystemRepresentation]) /
            "Matchaboy" / "Library";
        // Bundled resources are read-only inputs. Every extracted cartridge and
        // its eventual .matchaboy.sav lives in the user's Application Support.
        return materialize(directory, filename, bytes);
    }
}

std::filesystem::path arcade_credits_path() {
    @autoreleasepool {
        NSString *resources = [[NSBundle mainBundle] resourcePath];
        if (!resources) throw std::runtime_error("This build is missing its game credits.");
        return std::filesystem::path([resources fileSystemRepresentation]) / "CREDITS.txt";
    }
}

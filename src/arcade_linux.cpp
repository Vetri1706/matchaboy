#include "arcade_library.hpp"
#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>
namespace {
#include "arcade_catalog.inc"
std::filesystem::path data_home() {
    if (const char *value = std::getenv("XDG_DATA_HOME");
        value && *value && std::filesystem::path(value).is_absolute())
        return value;
    if (const char *value = std::getenv("HOME"); value && *value)
        return std::filesystem::path(value) / ".local/share";
    throw std::runtime_error("Set HOME or XDG_DATA_HOME to use the bundled library.");
}
std::filesystem::path source_path(const std::string &filename) {
    std::array<char, 4096> executable{};
    const auto length = readlink("/proc/self/exe", executable.data(), executable.size() - 1);
    if (length <= 0 || static_cast<std::size_t>(length) >= executable.size() - 1)
        throw std::runtime_error("Cannot locate the Matchaboy installation.");
    const auto directory =
        std::filesystem::path(std::string(executable.data(), static_cast<std::size_t>(length)))
            .parent_path();
    for (const auto &root : std::array<std::filesystem::path, 4>{
             directory / "assets/roms", directory / "Arcade",
             directory / "../share/matchaboy/arcade", directory / "../assets/roms"}) {
        const auto candidate = root / filename;
        if (std::filesystem::is_regular_file(candidate))
            return candidate;
    }
    throw std::runtime_error("Missing bundled game: " + filename +
                             ". Keep assets beside Matchaboy.");
}
} // namespace
std::span<const ArcadeGame> arcade_games() {
    return catalog;
}
std::filesystem::path arcade_rom_path(std::size_t index) {
    if (index >= catalog.size())
        throw std::out_of_range("Unknown arcade game.");
    const auto &game = catalog[index];
    const std::string filename =
        std::string(game.id) + (std::string(game.system) == "GB" ? ".gb" : ".gba");
    std::ifstream input(source_path(filename), std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    if (input.bad() || bytes.empty() || bytes.size() > 32 * 1024 * 1024)
        throw std::runtime_error("Invalid bundled game: " + filename);
    const auto directory = data_home() / "Matchaboy/Library";
    std::filesystem::create_directories(directory);
    const auto destination = directory / filename;
    std::ifstream existing(destination, std::ios::binary);
    const std::vector<char> saved{std::istreambuf_iterator<char>(existing),
                                  std::istreambuf_iterator<char>()};
    const bool cached = saved == bytes;
    auto name = (directory / (filename + ".XXXXXX")).string();
    std::vector<char> temporary(name.begin(), name.end());
    temporary.push_back('\0');
    int descriptor = mkstemp(temporary.data());
    if (descriptor < 0)
        throw std::runtime_error("Cannot write the Matchaboy game library.");
    try {
        std::size_t offset = 0;
        while (!cached && offset < bytes.size()) {
            const auto count = write(descriptor, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Cannot extract the bundled game.");
            offset += static_cast<std::size_t>(count);
        }
        if (!cached && fsync(descriptor) != 0)
            throw std::runtime_error("Cannot flush the bundled game.");
        const auto result = close(descriptor);
        descriptor = -1;
        if (result != 0)
            throw std::runtime_error("Cannot close the bundled game.");
        if (cached)
            std::filesystem::remove(temporary.data());
        else
            std::filesystem::rename(temporary.data(), destination);
    } catch (...) {
        if (descriptor >= 0)
            close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(temporary.data(), ignored);
        throw;
    }
    return destination;
}

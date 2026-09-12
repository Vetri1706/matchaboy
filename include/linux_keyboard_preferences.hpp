#pragma once
#include "keyboard_mapping.hpp"
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace matcha::keyboard {
inline std::filesystem::path linux_preferences_path() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg && std::filesystem::path(xdg).is_absolute())
        return std::filesystem::path(xdg) / "matchaboy/keyboard-v1.conf";
    const char *home = std::getenv("HOME");
    if (home && *home)
        return std::filesystem::path(home) / ".config/matchaboy/keyboard-v1.conf";
    throw std::runtime_error("Set HOME or XDG_CONFIG_HOME to save keyboard settings.");
}
inline Mapping load_linux_mapping(const std::filesystem::path &path) {
    std::ifstream input(path);
    std::string signature;
    unsigned version = 0;
    auto result = balanced();
    if (!(input >> signature >> version) || signature != "MatchaboyKeyboard" || version != 1)
        return balanced();
    for (auto &key : result)
        if (!(input >> key))
            return balanced();
    std::string trailing;
    if (input >> trailing)
        return balanced();
    if (!validate_mapping(result).empty())
        return balanced();
    for (auto &key : result)
        key = canonical_key(key);
    return result;
}
inline void save_linux_mapping(const std::filesystem::path &path, const Mapping &mapping) {
    if (const auto error = validate_mapping(mapping); !error.empty())
        throw std::runtime_error(error);
    std::filesystem::create_directories(path.parent_path());
    std::ostringstream output;
    output << "MatchaboyKeyboard 1\n";
    for (const auto key : mapping)
        output << canonical_key(key) << '\n';
    const auto contents = output.str();
    auto pattern = path.string() + ".XXXXXX";
    const int descriptor = mkstemp(pattern.data());
    if (descriptor < 0)
        throw std::runtime_error("Cannot save keyboard settings.");
    bool open = true;
    try {
        std::size_t offset = 0;
        while (offset < contents.size()) {
            const auto count =
                write(descriptor, contents.data() + offset, contents.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Cannot write keyboard settings.");
            offset += static_cast<std::size_t>(count);
        }
        if (fsync(descriptor) != 0)
            throw std::runtime_error("Cannot flush keyboard settings.");
        const auto result = close(descriptor);
        open = false;
        if (result != 0)
            throw std::runtime_error("Cannot close keyboard settings.");
        std::filesystem::rename(pattern, path);
    } catch (...) {
        if (open)
            close(descriptor);
        std::error_code ignored;
        std::filesystem::remove(pattern, ignored);
        throw;
    }
}
} // namespace matcha::keyboard

#include "linux_keyboard_preferences.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace {
unsigned assertions{};
void require(bool value, const char *message) {
    ++assertions;
    if (!value)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path);
    file << value;
}
} // namespace
int main() {
    std::string pattern =
        (std::filesystem::temp_directory_path() / "matchaboy-keyboard-XXXXXX").string();
    const char *created = mkdtemp(pattern.data());
    if (!created) {
        std::cerr << "Cannot create isolated keyboard test directory\n";
        return 1;
    }
    const std::filesystem::path root = created;
    try {
        namespace k = matcha::keyboard;
        const auto path = root / "settings/keyboard-v1.conf";
        require(k::load_linux_mapping(path) == k::balanced(),
                "New installations must use balanced keys");
        k::save_linux_mapping(path, k::classic());
        require(k::load_linux_mapping(path) == k::classic(), "Classic preset must persist");
        auto custom = k::balanced();
        custom[9] = k::from_linux_keycode(32); // physical O
        k::save_linux_mapping(path, custom);
        require(k::load_linux_mapping(path) == custom, "User remapping must survive a new load");
        auto invalid = custom;
        invalid[4] = invalid[5];
        bool rejected = false;
        try {
            k::save_linux_mapping(path, invalid);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Duplicate mapping must never be saved");
        require(k::load_linux_mapping(path) == custom,
                "Failed validation must preserve previous settings");
        for (const auto &contents : std::array<std::string, 7>{
                 "", "MatchaboyKeyboard 2\n", "Other 1\n", "MatchaboyKeyboard 1\n2 0 13",
                 "MatchaboyKeyboard 1\n2 0 13 1 37 40 49 36 12 34 trailing",
                 "MatchaboyKeyboard 1\n2 0 13 1 37 40 49 36 12 37",
                 "MatchaboyKeyboard 1\n2 0 13 1 37 40 49 36 12 4294967295"}) {
            write(path, contents);
            require(k::load_linux_mapping(path) == k::balanced(),
                    "Corrupt, partial or future settings must safely fall back");
        }
        k::save_linux_mapping(path, custom);
        std::filesystem::create_directory(root / "not-a-file");
        rejected = false;
        try {
            k::save_linux_mapping(root / "not-a-file", custom);
        } catch (const std::exception &) {
            rejected = true;
        }
        require(rejected, "Failed atomic replacement must report an error");
        require(std::filesystem::is_directory(root / "not-a-file"),
                "Failed save must not destroy existing paths");
        unsigned entries = 0;
        for (const auto &entry : std::filesystem::directory_iterator(root)) {
            (void)entry;
            ++entries;
        }
        require(entries == 2, "Failed atomic save must clean up temporary files");
        require(k::load_linux_mapping(path) == custom,
                "Unrelated failure must not modify saved mapping");
        std::filesystem::remove_all(root);
        std::cout << assertions << " Linux keyboard persistence checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::filesystem::remove_all(root);
        std::cerr << e.what() << '\n';
        return 1;
    }
}

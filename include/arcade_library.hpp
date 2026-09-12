#pragma once
#include <cstddef>
#include <filesystem>
#include <span>

struct ArcadeGame {
    const char *id, *title, *system, *genre, *description;
    const char *controls, *learn, *inspector_hint, *source;
    const char *author, *license, *players, *rom_filename;
};
std::span<const ArcadeGame> arcade_games();
// Materialize one licensed bundled ROM. Prefer the portable app's data
// directory, with a per-user writable fallback for read-only app folders.
std::filesystem::path arcade_rom_path(std::size_t index);
// A readable local copy of the complete bundled games' notices and licenses.
std::filesystem::path arcade_credits_path();

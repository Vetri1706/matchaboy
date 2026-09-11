#pragma once
#include <cstddef>
#include <filesystem>
#include <span>

struct ArcadeGame {
    const char *id, *title, *system, *genre, *description;
    const char *controls, *learn, *inspector_hint, *source;
};
std::span<const ArcadeGame> arcade_games();
// Materialize one embedded, original ROM. Prefer the portable app's data
// directory, with a per-user writable fallback for read-only app folders.
std::filesystem::path arcade_rom_path(std::size_t index);

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
// Per-game files live in two folders, created next to the program
// (portable) or, when that folder is read-only, in the user data folder;
// screenshots go beside them.
struct Folders {
    std::filesystem::path saves;    // <root>/Sauvegardes: .sav, states, unlock journal, last capture
    std::filesystem::path covers;   // <root>/Jaquettes: cover images named after the ROM
    std::filesystem::path captures; // <root>/Captures: screenshots taken in game
};
// Uses the first root where both folders can be created and written.
// Throws std::runtime_error when no root is usable.
Folders prepare_folders(const std::vector<std::filesystem::path>& roots);
// Files are isolated by the normalized absolute ROM path, under
// <saves>/roms/<SHA-256 of path>/<ROM file name><suffix>.
// Older files are copied, never moved. A shared legacy file is assigned to
// the first ROM that migrates it, preventing reuse by another same-named ROM.
// Migration errors throw instead of falling back to a shared writable file.
std::filesystem::path save_path(const Folders& folders, const std::filesystem::path& rom,
                                const char* suffix);
// Save states: slots 1 to state_slots. Slot 1 is the file of the versions
// that had a single one, <ROM>.rstate; slot n, <ROM>.rstate<n>. Each has its
// picture, the same name with ".png" added.
inline constexpr int state_slots = 9;
std::filesystem::path state_path(const Folders& folders, const std::filesystem::path& rom, int slot);
std::filesystem::path state_picture_path(const std::filesystem::path& state);
// Shared capture location for the player and launcher, including migration
// of captures written with the older .ravenemudsk-preview.bmp suffix.
std::filesystem::path preview_path(const Folders& folders, const std::filesystem::path& rom);
// A new file for a screenshot of `title` taken at `when`, local time:
// Captures/<title> 2026-10-24 17-05-03.png, then " (2)" and so on when the
// name is taken. Characters a file name cannot hold become spaces.
std::filesystem::path screenshot_path(const Folders& folders, const std::string& title, std::time_t when);
// Cover images to try, in order: Jaquettes, then next to the ROM.
std::vector<std::filesystem::path> cover_candidates(const Folders& folders,
                                                    const std::filesystem::path& rom);
bool is_image(const std::filesystem::path& file);
// Copies an image to Jaquettes/<ROM name without extension><image extension>,
// replacing the previous cover of that game. Returns the new cover path.
std::filesystem::path install_cover(const Folders& folders, const std::filesystem::path& rom,
                                    const std::filesystem::path& image);
} // namespace ravenemudsk_ui

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <filesystem>
#include <vector>
namespace ravenemudsk_ui {
// Per-game files live in two folders, created next to the program
// (portable) or, when that folder is read-only, in the user data folder.
struct Folders {
    std::filesystem::path saves;   // <root>/Sauvegardes: .sav, states, unlock journal, last capture
    std::filesystem::path covers;  // <root>/Jaquettes: cover images named after the ROM
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
// Shared capture location for the player and launcher, including migration
// of captures written with the older .ravenemudsk-preview.bmp suffix.
std::filesystem::path preview_path(const Folders& folders, const std::filesystem::path& rom);
// Cover images to try, in order: Jaquettes, then next to the ROM.
std::vector<std::filesystem::path> cover_candidates(const Folders& folders,
                                                    const std::filesystem::path& rom);
bool is_image(const std::filesystem::path& file);
// Copies an image to Jaquettes/<ROM name without extension><image extension>,
// replacing the previous cover of that game. Returns the new cover path.
std::filesystem::path install_cover(const Folders& folders, const std::filesystem::path& rom,
                                    const std::filesystem::path& image);
} // namespace ravenemudsk_ui

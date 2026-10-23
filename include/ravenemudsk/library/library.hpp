// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
struct Game {
    std::filesystem::path path;
    std::string system;
    std::int64_t last_played = 0;
    std::uint64_t seconds_played = 0;
};
std::string utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& value);
// Appends a suffix such as ".sav" natively. Never build paths from
// path::string(): on Windows it uses the ANSI code page, not UTF-8.
std::filesystem::path sidecar(std::filesystem::path file, const char* suffix);
std::string display_title(const Game& game);
std::string detect_system(const std::filesystem::path& file);
class Library {
public:
    explicit Library(std::filesystem::path storage);
    void load();
    void save() const;
    bool add(const std::filesystem::path& file);
    // Forgets a game; the ROM file and its saves stay on disk.
    bool remove(const std::filesystem::path& file);
    std::size_t import_folder(const std::filesystem::path& folder);
    void mark_played(const std::filesystem::path& file, std::uint64_t seconds);
    const std::vector<Game>& games() const noexcept { return games_; }
    std::vector<Game>& games() noexcept { return games_; }
private:
    std::filesystem::path storage_;
    std::vector<Game> games_;
};
} // namespace ravenemudsk_ui

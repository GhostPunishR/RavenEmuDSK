// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
namespace ravenemudsk_ui {
// Preferences and local profile, stored as UTF-8 "key=value" lines.
class Settings {
public:
    Settings() = default;
    explicit Settings(std::filesystem::path file) : file_(std::move(file)) {}
    void load();
    void save() const;  // written to a temporary file, then renamed
    std::string get(const std::string& key, const std::string& fallback = {}) const;
    bool flag(const std::string& key, bool fallback) const;
    int number(const std::string& key, int fallback) const;
    // Line breaks are removed so that one value stays on one line.
    void set(const std::string& key, std::string value);
    void set_flag(const std::string& key, bool value) { set(key, value ? "1" : "0"); }
    const std::filesystem::path& file() const noexcept { return file_; }
private:
    std::filesystem::path file_;
    std::map<std::string, std::string> values_;
};

// Profile level: 10 XP per achievement point and 1 XP per minute played.
// Level n starts at 50 * n * (n - 1) XP: 0, 100, 300, 600, 1000...
struct Level {
    int level = 1;
    std::uint64_t xp = 0;
    std::uint64_t level_start = 0;
    std::uint64_t next_level = 100;
    float progress() const;
};
Level level_for(std::uint64_t points, std::uint64_t seconds_played);
// Keeps at most `max_chars` UTF-8 characters, trimmed, without control characters.
std::string clean_name(const std::string& text, std::size_t max_chars);
} // namespace ravenemudsk_ui

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/settings.hpp>
#include <ravenemudsk/ui/library.hpp>
#include <charconv>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace ravenemudsk_ui {
namespace fs = std::filesystem;

void Settings::load() {
    values_.clear();
    std::ifstream in(file_, std::ios::binary);
    std::string line;
    while (std::getline(in, line) && values_.size() < 256) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        values_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}
void Settings::save() const {
    if (file_.has_parent_path()) fs::create_directories(file_.parent_path());
    const fs::path temp = sidecar(file_, ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const auto& [key, value] : values_) out << key << '=' << value << '\n';
        out.flush();
        if (!out) throw std::runtime_error("Impossible d'enregistrer les réglages");
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Impossible de finaliser les réglages");
#else
    fs::rename(temp, file_);
#endif
}
std::string Settings::get(const std::string& key, const std::string& fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second;
}
bool Settings::flag(const std::string& key, bool fallback) const {
    const auto it = values_.find(key);
    return it == values_.end() ? fallback : it->second == "1";
}
int Settings::number(const std::string& key, int fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) return fallback;
    int value = 0;
    const auto& text = it->second;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : fallback;
}
void Settings::set(const std::string& key, std::string value) {
    for (auto& c : value) if (c == '\n' || c == '\r') c = ' ';
    values_[key] = std::move(value);
}

float Level::progress() const {
    if (next_level <= level_start) return 1.f;
    return static_cast<float>(xp - level_start) / static_cast<float>(next_level - level_start);
}
Level level_for(std::uint64_t points, std::uint64_t seconds_played) {
    Level l;
    l.xp = points * 10 + seconds_played / 60;
    auto start = [](std::uint64_t n) { return 50 * n * (n - 1); };
    while (l.level < 999 && start(static_cast<std::uint64_t>(l.level) + 1) <= l.xp) ++l.level;
    l.level_start = start(static_cast<std::uint64_t>(l.level));
    l.next_level = start(static_cast<std::uint64_t>(l.level) + 1);
    return l;
}
std::string clean_name(const std::string& text, std::size_t max_chars) {
    std::string out;
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size() && chars < max_chars;) {
        const auto c = static_cast<unsigned char>(text[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (len == 0 || i + len > text.size()) break;  // invalid UTF-8: stop here
        if (len == 1 && (c < 0x20 || c == 0x7F)) { ++i; continue; }
        out.append(text, i, len);
        i += len;
        ++chars;
    }
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    return out.substr(first, out.find_last_not_of(' ') - first + 1);
}
} // namespace ravenemudsk_ui

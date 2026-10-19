// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/library.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
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
std::string utf8(const fs::path& path) {
    const auto v = path.u8string();
    return {v.begin(), v.end()};
}
fs::path from_utf8(const std::string& value) {
    return fs::u8path(value);
}
fs::path sidecar(fs::path file, const char* suffix) {
    file += suffix;
    return file;
}
std::string detect_system(const fs::path& file) {
    auto ext = utf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gb") return "gb";
    if (ext == ".gbc") return "gbc";
    if (ext == ".gba") return "gba";
    return {};
}
std::string display_title(const Game& game) {
    std::string value = utf8(game.path.stem());
    for (auto& ch : value) if (ch == '_') ch = ' ';
    return value.empty() ? "Jeu sans titre" : value;
}
Library::Library(fs::path storage) : storage_(std::move(storage)) {}
void Library::load() {
    games_.clear();
    std::ifstream stream(storage_, std::ios::binary);
    if (!stream) return;
    std::string path, system;
    std::int64_t timestamp;
    std::uint64_t duration;
    // Quoted UTF-8 path (spaces supported), tab-separated numeric columns.
    while (stream >> std::quoted(path) >> system >> timestamp >> duration) {
        try {
            if (games_.size() >= 2000) break;
            const auto file = from_utf8(path);
            if (!fs::is_regular_file(file) || detect_system(file) != system) continue;
            auto duplicate = std::find_if(games_.begin(), games_.end(),
                [&](const Game& g) { return g.path == file; });
            if (duplicate == games_.end())
                games_.push_back({file, system, timestamp, duration});
        } catch (const fs::filesystem_error&) {}
    }
}
void Library::save() const {
    if (storage_.has_parent_path()) fs::create_directories(storage_.parent_path());
    const fs::path temp = sidecar(storage_, ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Impossible d'enregistrer la bibliotheque");
        for (const auto& game : games_)
            out << std::quoted(utf8(game.path)) << '\t' << game.system << '\t'
                << game.last_played << '\t' << game.seconds_played << '\n';
        out.flush();
        if (!out) throw std::runtime_error("Erreur lors de l'enregistrement de la bibliotheque");
    }
#ifdef _WIN32
    // Replace an existing file atomically on Windows without removing the backup first.
    if (!MoveFileExW(temp.c_str(), storage_.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Impossible de finaliser la bibliotheque");
#else
    fs::rename(temp, storage_);
#endif
}
bool Library::add(const fs::path& file) {
    if (games_.size() >= 2000) return false;
    std::error_code ec;
    const auto path = fs::absolute(file, ec).lexically_normal();
    if (ec || !fs::is_regular_file(path, ec) || ec) return false;
    const auto system = detect_system(path);
    if (system.empty()) return false;
    if (std::any_of(games_.begin(), games_.end(),
                    [&](const Game& g) { return g.path == path; })) return false;
    games_.push_back({path, system});
    return true;
}
bool Library::remove(const fs::path& file) {
    const auto it = std::find_if(games_.begin(), games_.end(),
        [&](const Game& game) { return game.path == file; });
    if (it == games_.end()) return false;
    games_.erase(it);
    return true;
}
std::size_t Library::import_folder(const fs::path& folder) {
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return 0;
    std::size_t count = 0, inspected = 0;
    fs::recursive_directory_iterator iter(folder, fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && iter != end && inspected++ < 50000 && games_.size() < 2000) {
        if (iter.depth() >= 4) iter.disable_recursion_pending();
        if (iter->is_regular_file(ec) && !ec && add(iter->path())) ++count;
        ec.clear();
        iter.increment(ec);
    }
    return count;
}
void Library::mark_played(const fs::path& file, std::uint64_t seconds) {
    const auto it = std::find_if(games_.begin(), games_.end(),
        [&](const Game& game) { return game.path == file; });
    if (it == games_.end()) return;
    it->last_played = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (std::numeric_limits<std::uint64_t>::max() - it->seconds_played < seconds)
        it->seconds_played = std::numeric_limits<std::uint64_t>::max();
    else it->seconds_played += seconds;
}
}

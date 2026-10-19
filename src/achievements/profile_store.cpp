// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/profile_store.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemu/sha256.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace ravenemudsk_ra {
namespace {
namespace fs = std::filesystem;
[[maybe_unused]] fs::path from_utf8(const char* value) { return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(value))); }
std::string normalized(std::string user) {
    for (auto& c : user) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return user;
}
std::string identity(const std::string& user) {
    const auto value = normalized(user);
    const auto hash = ravenemu::detail::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    std::string result;
    for (const auto byte : hash) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; }
    return result;
}
fs::path default_root() {
#ifdef _WIN32
    std::wstring base(32768, L'\0');
    const auto size = GetEnvironmentVariableW(L"APPDATA", base.data(), static_cast<DWORD>(base.size()));
    if (size && size < base.size()) { base.resize(size); return fs::path(base) / "RavenEmu/RetroAchievements"; }
#elif defined(__APPLE__)
    if (const auto* base = std::getenv("HOME")) return from_utf8(base) / "Library/Application Support/RavenEmu/RetroAchievements";
#else
    if (const auto* base = std::getenv("XDG_DATA_HOME"); base && *base && from_utf8(base).is_absolute())
        return from_utf8(base) / "RavenEmu/RetroAchievements";
    if (const auto* base = std::getenv("HOME")) return from_utf8(base) / ".local/share/RavenEmu/RetroAchievements";
#endif
    return {};
}
class FileProfiles final : public ProfileStore {
public:
    explicit FileProfiles(fs::path root) : root_(std::move(root)) {}
    bool load(const std::string& user, Snapshot& result) override {
        try {
            const auto file = path(user);
            if (fs::file_size(file) > 2 * 1024 * 1024) return false;
            std::ifstream in(file, std::ios::binary);
            Snapshot s;
            std::string version;
            std::size_t count = 0;
            in >> version >> std::quoted(s.username) >> std::quoted(s.display_name) >> std::quoted(s.avatar_url)
               >> s.softcore_points >> s.hardcore_points >> std::quoted(s.game_title)
               >> s.unlocked_count >> s.total_count >> s.unlocked_points >> s.total_points >> count;
            if (!in || version != "RavenEmuProfile1" || normalized(s.username) != normalized(user) || count > 2000 ||
                s.unlocked_count < 0 || s.total_count < s.unlocked_count || s.total_count > 2000 ||
                s.unlocked_points > s.total_points) return false;
            for (std::size_t i = 0; i < count; ++i) {
                Achievement a;
                in >> std::quoted(a.id) >> std::quoted(a.title) >> std::quoted(a.description)
                   >> std::quoted(a.badge_url) >> a.points >> a.unlocked;
                if (!in || a.id.size() > 32 || a.title.size() > 4096 || a.description.size() > 16384 ||
                    a.badge_url.size() > 512 || a.points < 0) return false;
                a.state = a.unlocked ? 2 : 1;
                s.achievements.push_back(std::move(a));
            }
            in >> std::ws;
            if (!in.eof()) return false;
            s.cached_profile = true;
            result = std::move(s);
            return true;
        } catch (...) { return false; }
    }
    bool save(const Snapshot& s) override {
        if (s.username.empty() || s.achievements.size() > 2000) return false;
        try {
            fs::create_directories(root_);
#ifndef _WIN32
            fs::permissions(root_, fs::perms::owner_all, fs::perm_options::replace);
#endif
            const auto file = path(s.username);
            static std::atomic<unsigned> serial{0};
            const auto temp = fs::path(file).concat("." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "." + std::to_string(serial++) + ".tmp");
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            // Whitelist: no token, password, login flags or mutable achievement runtime.
            out << "RavenEmuProfile1\n" << std::quoted(s.username) << '\n' << std::quoted(s.display_name) << '\n'
                << std::quoted(s.avatar_url) << '\n' << s.softcore_points << ' ' << s.hardcore_points << '\n'
                << std::quoted(s.game_title) << '\n' << s.unlocked_count << ' ' << s.total_count << ' '
                << s.unlocked_points << ' ' << s.total_points << '\n' << s.achievements.size() << '\n';
            for (const auto& a : s.achievements)
                out << std::quoted(a.id) << ' ' << std::quoted(a.title) << ' ' << std::quoted(a.description) << ' '
                    << std::quoted(a.badge_url) << ' ' << a.points << ' ' << a.unlocked << '\n';
            out.close();
            if (!out || fs::file_size(temp) > 2 * 1024 * 1024) { fs::remove(temp); return false; }
#ifndef _WIN32
            fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace);
            fs::rename(temp, file);
#else
            if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                fs::remove(temp); return false;
            }
#endif
            return true;
        } catch (...) { return false; }
    }
    bool clear(const std::string& user) override {
        try { fs::remove(path(user)); return true; } catch (...) { return false; }
    }
private:
    fs::path root_;
    fs::path path(const std::string& user) const { return root_ / (identity(user) + ".profile"); }
};
}
std::shared_ptr<ProfileStore> make_profile_store(const fs::path& root) {
    const auto directory = root.empty() ? default_root() : root;
    return directory.empty() ? nullptr : std::make_shared<FileProfiles>(directory);
}
}

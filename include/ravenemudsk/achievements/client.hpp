// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <ravenemudsk/achievements/profile_store.hpp>
#include <ravenemudsk/achievements/outbox.hpp>
#include <functional>
#include <ravenemudsk/achievements/session_store.hpp>
#include <memory>
#include <string>
#include <vector>
struct ravenemu_bridge;
namespace ravenemudsk_ra {
// Where an unlock stands with RetroAchievements. Server: as the service
// reports it. Pending: earned on this computer, not confirmed yet.
// Refused: the service rejected its submission.
enum class AchievementSync { Server, Pending, Refused };
struct Achievement {
    std::string id;
    std::string title;
    std::string description;
    std::string progress;
    std::string badge_url;
    int points = 0;
    int state = 0;
    float measured_percent = 0.f;
    bool unlocked = false;
    std::int64_t unlock_time = 0; // Unix seconds of the account's unlock; 0 when locked or unknown.
    int type = 0;                 // RC_CLIENT_ACHIEVEMENT_TYPE_*: standard, missable, progression, win.
    float rarity = 0.f, rarity_hardcore = 0.f; // Percent of players, as reported by the server.
    AchievementSync sync = AchievementSync::Server; // Live state; never cached.
};
struct Leaderboard { std::uint32_t id=0; std::string title,description; };
struct RankingEntry { std::uint32_t rank=0; std::string username,score; bool current_user=false; };
struct Ranking {
    std::uint32_t id=0,total=0;
    bool loading=false,around_user=false;
    std::string error;
    std::vector<RankingEntry> entries;
};
enum class GameStatus { None, Loading, Ready, NoAchievements, UnknownRom, Offline, Error };
struct Snapshot {
    std::string status = "Non connecté";
    std::string username, display_name, game_title, avatar_url;
    std::string rom_path; // Normalized UTF-8 path of the ROM that supplied this game summary.
    unsigned int softcore_points = 0, hardcore_points = 0;
    int unlocked_count = 0, total_count = 0;
    unsigned int unlocked_points = 0, total_points = 0;
    bool logged_in = false, hardcore = false;
    bool login_pending = false, can_retry = false, session_saved = false;
    bool cached_profile = false, reconnect_scheduled = false, online = false;
    GameStatus game_status = GameStatus::None;
    std::vector<Achievement> achievements;
    std::string game_hash,rich_presence;
    std::vector<Leaderboard> leaderboards;
    std::uint32_t pending_submissions=0;
    bool durable_submissions=false,hardcore_preferred=false,game_hardcore=false;
};
// Kind "hardcore_refused" carries no text: the client turned the Hardcore
// preference off itself, and the saved setting must follow it.
struct Event {
    std::string kind, title, detail;
    std::string id, badge_url;
    int points = 0;
};
struct ServerReply {
    std::string body;
    int status = -2; // SDK retryable transport failure (HTTP codes remain positive).
};
using ServerTransport = std::function<ServerReply(const std::string&, const std::string&, const std::string&)>;
class Client {
public:
    Client();
    // Injectable storage/transport let tests exercise the real SDK login flow
    // without contacting RetroAchievements or touching the user's credentials.
    using Clock = std::function<std::int64_t()>;
    Client(std::shared_ptr<SessionStore> storage, ServerTransport transport,
           std::shared_ptr<ProfileStore> profiles = {}, Clock clock = {}, std::shared_ptr<Outbox> outbox = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void login(const std::string& username, const std::string& password);
    void retry();
    void disconnect();
    void start_game(int console, const std::vector<std::uint8_t>& rom, ravenemu_bridge* core,
                    const std::string& rom_path = {});
    void end_game();
    void pump(bool frame);
    void reset();
    bool set_hardcore(bool on);
    // Launcher-only metadata refresh; never replaces a live game's runtime.
    bool refresh_game(const std::string& rom_path);
    void fetch_ranking(std::uint32_t id, bool around_user = false);
    Ranking ranking() const;
    std::vector<std::uint8_t> save_progress() const;
    bool load_progress(const std::vector<std::uint8_t>& progress);
    bool hardcore() const;
    bool can_load_state() const;
    bool can_pause() const;
    Snapshot snapshot() const;
    // Account-scoped display data for a library ROM; never identifies or executes a game.
    std::optional<Snapshot> game_snapshot(const std::string& rom_path);
    std::vector<Event> drain_events();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Client& client();
} // namespace ravenemudsk_ra

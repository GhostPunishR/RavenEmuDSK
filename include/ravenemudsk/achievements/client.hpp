// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <functional>
#include <ravenemudsk/achievements/session_store.hpp>
#include <memory>
#include <string>
#include <vector>
struct ravenemu_bridge;
namespace ravenemudsk_ra {
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
};
struct Snapshot {
    std::string status = "Non connecté";
    std::string username, display_name, game_title, avatar_url;
    unsigned int softcore_points = 0, hardcore_points = 0;
    int unlocked_count = 0, total_count = 0;
    unsigned int unlocked_points = 0, total_points = 0;
    bool logged_in = false, hardcore = false;
    bool login_pending = false, can_retry = false, session_saved = false;
    std::vector<Achievement> achievements;
};
struct Event {
    std::string kind, title, detail;
    std::string id, badge_url;
    int points = 0;
};
struct ServerReply {
    std::string body;
    int status = -1;
};
using ServerTransport = std::function<ServerReply(const std::string&, const std::string&, const std::string&)>;
class Client {
public:
    Client();
    // Injectable storage/transport let tests exercise the real SDK login flow
    // without contacting RetroAchievements or touching the user's credentials.
    Client(std::shared_ptr<SessionStore> storage, ServerTransport transport);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void login(const std::string& username, const std::string& password);
    void retry();
    void disconnect();
    void start_game(int console, const std::vector<std::uint8_t>& rom, ravenemu_bridge* core);
    void end_game();
    void pump(bool frame);
    void reset();
    void set_hardcore(bool on);
    bool hardcore() const;
    bool can_pause() const;
    Snapshot snapshot() const;
    std::vector<Event> drain_events();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Client& client();
} // namespace ravenemudsk_ra

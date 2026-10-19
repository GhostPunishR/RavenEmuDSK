// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemu/bridge.h>
#include <rc_client.h>
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace ravenemudsk_ra {
namespace {
struct Reply { std::string body; int status = -1; };
size_t write_body(char* bytes, size_t size, size_t count, void* ptr) {
    auto& body = *static_cast<std::string*>(ptr);
    const size_t n = size * count;
    if (n > 8 * 1024 * 1024 || body.size() > 8 * 1024 * 1024 - n) return 0;
    body.append(bytes, n);
    return n;
}
Reply https_request(std::string url, std::string post, std::string content_type) {
    Reply reply;
    // Never send login tokens to third-party hosts or follow redirects.
    if (url.rfind("https://retroachievements.org/", 0) != 0) return reply;
    CURL* curl = curl_easy_init();
    if (!curl) return reply;
    curl_slist* headers = nullptr;
    if (!post.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post.size()));
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "RavenEmuDSK/0.1.0 RavenCore/0.7.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        reply.status = static_cast<int>(http_code);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return reply;
}
}
// Let the SDK build official image URLs if the server did not supply one.
std::string image_url(const char* url, const rc_client_achievement_t& a, int state) {
    if (url && *url) return url;
    char buffer[256]{};
    if (rc_client_achievement_get_image_url(&a, state, buffer, sizeof(buffer)) == RC_OK)
        return buffer;
    return {};
}
std::string badge(const rc_client_achievement_t& a, bool colored = false) {
    const int state = colored ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED : a.state;
    const char* ready = state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED ? a.badge_url : a.badge_locked_url;
    return image_url(ready, a, state);
}
struct Client::Impl {
    struct Pending {
        rc_client_server_callback_t callback{};
        void* userdata{};
        std::future<Reply> future;
    };
    rc_client_t* handle = nullptr;
    ravenemu_bridge* memory = nullptr;
    int console = RAVENEMU_GB;
    std::vector<std::uint8_t> rom;
    std::vector<Pending> pending;
    std::vector<Event> events;
    Snapshot state;
    std::string retry_user, retry_token;

    static Impl& self(rc_client_t* c) {
        return *static_cast<Impl*>(rc_client_get_userdata(c));
    }
    static uint32_t read(uint32_t address, uint8_t* data, uint32_t len, rc_client_t* c) {
        const auto& impl = self(c);
        if (!impl.memory || len > 0x58000u) return 0;
        return static_cast<uint32_t>(ravenemu_read_achievement_memory(
            impl.memory, address, data, len));
    }
    static void request(const rc_api_request_t* req, rc_client_server_callback_t callback,
                        void* userdata, rc_client_t* c) {
        auto& impl = self(c);
        if (!req || !req->url) return;
        const std::string url = req->url;
        const std::string post = req->post_data ? req->post_data : "";
        const std::string type = req->content_type ? req->content_type : "application/x-www-form-urlencoded";
        impl.pending.push_back(Pending{callback, userdata, std::async(std::launch::async,
            [url, post, type] { return https_request(url, post, type); })});
    }
    static void logged(int result, const char*, rc_client_t* c, void*) {
        auto& impl = self(c);
        if (result == RC_OK) {
            impl.state.status = "Connecté à RetroAchievements";
            impl.state.logged_in = true;
            const auto* user = rc_client_get_user_info(c);
            if (user && user->username && user->token) {
                impl.retry_user = user->username;
                impl.retry_token = user->token;
            }
            impl.events.push_back({"login", "RetroAchievements connecté", ""});
            // When the ROM was opened before authentication, identify it now.
            if (impl.memory && !impl.rom.empty()) {
                const uint32_t console = impl.console == RAVENEMU_GB ? 4u :
                                         impl.console == RAVENEMU_GBC ? 6u : 5u;
                rc_client_begin_identify_and_load_game(impl.handle, console, nullptr,
                    impl.rom.data(), impl.rom.size(), loaded, nullptr);
            }
        } else {
            impl.state.status = "Connexion impossible : vérifiez votre compte et le réseau";
            impl.events.push_back({"error", "Connexion RetroAchievements impossible", ""});
        }
    }
    static void loaded(int result, const char*, rc_client_t* c, void*) {
        auto& impl = self(c);
        if (result == RC_OK && rc_client_get_game_info(c)) {
            impl.state.status = "Jeu reconnu par RetroAchievements";
            impl.events.push_back({"info", "Succès disponibles", rc_client_get_game_info(c)->title});
        } else {
            impl.state.status = "Aucun succès trouvé pour cette ROM (ou hors ligne)";
        }
    }
    static void event(const rc_client_event_t* ev, rc_client_t* c) {
        auto& impl = self(c);
        if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && ev->achievement) {
            const auto& a = *ev->achievement;
            impl.events.push_back({"achievement", a.title ? a.title : "Succès débloqué", "",
                std::to_string(a.id), badge(a, true), static_cast<int>(a.points)});
        } else if (ev->type == RC_CLIENT_EVENT_DISCONNECTED) {
            impl.state.status = "RetroAchievements hors ligne";
            impl.events.push_back({"offline", "RetroAchievements hors ligne", ""});
        } else if (ev->type == RC_CLIENT_EVENT_RECONNECTED) {
            impl.state.status = "RetroAchievements reconnecté";
            impl.events.push_back({"online", "RetroAchievements reconnecté", ""});
        } else if (ev->type == RC_CLIENT_EVENT_SERVER_ERROR) {
            impl.events.push_back({"notice", "Erreur RetroAchievements",
                ev->server_error && ev->server_error->error_message
                ? ev->server_error->error_message : "Le serveur a refusé une requête"});
        } else if (ev->type == RC_CLIENT_EVENT_GAME_COMPLETED) {
            impl.events.push_back({"notice", "Tous les succès obtenus !", ""});
        } else if (ev->type == RC_CLIENT_EVENT_RESET) {
            impl.events.push_back({"hardcore_reset", "Mode Hardcore : réinitialisation requise", ""});
        } else if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW ||
                   ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE) {
            if (!ev->achievement) return;
            const auto& a = *ev->achievement;
            const bool show = ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW;
            impl.events.push_back({show ? "challenge_show" : "challenge_hide",
                a.title ? a.title : "", a.measured_progress ? a.measured_progress : "",
                std::to_string(a.id), badge(a)});
        } else if (ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE) {
            if (!ev->leaderboard_tracker) return;
            const auto& tracker = *ev->leaderboard_tracker;
            const bool hide = ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE;
            impl.events.push_back({hide ? "tracker_hide" : "tracker_show",
                "Classement en cours", tracker.display ? tracker.display : "",
                std::to_string(tracker.id)});
        } else if (ev->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD) {
            if (!ev->leaderboard) return;
            const char* kind = ev->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED ? "leaderboard_started" :
                               ev->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED ? "leaderboard_failed" :
                               ev->type == RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED ? "leaderboard_submitted" :
                               "leaderboard_scoreboard";
            impl.events.push_back({kind, ev->leaderboard->title ? ev->leaderboard->title : "",
                ev->leaderboard->description ? ev->leaderboard->description : ""});
        }
    }

    Impl() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Initialisation HTTPS impossible");
        handle = rc_client_create(read, request);
        if (!handle) throw std::runtime_error("Initialisation rcheevos impossible");
        rc_client_set_userdata(handle, this);
        rc_client_set_host(handle, "https://retroachievements.org");
        rc_client_set_hardcore_enabled(handle, 0);
        rc_client_set_allow_background_memory_reads(handle, 0);
        rc_client_set_event_handler(handle, event);
    }
    ~Impl() {
        for (auto& job : pending) job.future.wait();
        if (handle) rc_client_destroy(handle);
        curl_global_cleanup();
    }
    void refresh() {
        const auto* user = rc_client_get_user_info(handle);
        const auto* game = rc_client_get_game_info(handle);
        if (user) {
            state.logged_in = true;
            state.username = user->username ? user->username : "";
            state.display_name = user->display_name ? user->display_name : state.username;
            if (user->avatar_url && *user->avatar_url) state.avatar_url = user->avatar_url;
            else {
                char avatar[256]{};
                state.avatar_url = rc_client_user_get_image_url(user, avatar, sizeof(avatar)) == RC_OK
                    ? std::string(avatar) : std::string();
            }
            state.softcore_points = user->score_softcore;
            state.hardcore_points = user->score;
        }
        state.hardcore = rc_client_get_hardcore_enabled(handle) != 0;
        state.game_title = game && game->title ? game->title : "";
        state.achievements.clear();
        state.unlocked_count = 0;
        state.total_count = 0;
        state.unlocked_points = 0;
        state.total_points = 0;
        if (!game) return;
        rc_client_user_game_summary_t total{};
        rc_client_get_user_game_summary(handle, &total);
        state.unlocked_count = static_cast<int>(total.num_unlocked_achievements);
        state.total_count = static_cast<int>(total.num_core_achievements);
        state.unlocked_points = total.points_unlocked;
        state.total_points = total.points_core;
        auto* list = rc_client_create_achievement_list(
            handle, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        if (list) {
            for (uint32_t i=0; i<list->num_buckets; ++i) {
                const auto& bucket = list->buckets[i];
                for (uint32_t j=0; j<bucket.num_achievements; ++j) {
                    const auto* a = bucket.achievements[j];
                    if (!a) continue;
                    state.achievements.push_back({std::to_string(a->id),
                        a->title ? a->title : "", a->description ? a->description : "",
                        a->measured_progress ? a->measured_progress : "",
                        badge(*a, true), static_cast<int>(a->points),
                        static_cast<int>(a->state), a->measured_percent,
                        a->state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED});
                }
            }
            rc_client_destroy_achievement_list(list);
        }
    }
    void pump(bool frame) {
        // Run all SDK callbacks on the owning UI/emulation thread.
        for (size_t i=0; i<pending.size();) {
            if (pending[i].future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                Pending job = std::move(pending[i]);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                Reply reply = job.future.get();
                const rc_api_server_response_t response{reply.body.c_str(), reply.body.size(), reply.status};
                job.callback(&response, job.userdata);
            } else ++i;
        }
        if (frame && memory) rc_client_do_frame(handle);
        else rc_client_idle(handle);
        refresh();
    }
};
Client::Client() : impl_(std::make_unique<Impl>()) {}
Client::~Client() = default;
void Client::login(const std::string& user, const std::string& password) {
    if (user.empty() || password.empty()) return;
    impl_->state.status = "Connexion en cours…";
    rc_client_begin_login_with_password(impl_->handle, user.c_str(), password.c_str(), Impl::logged, nullptr);
}
void Client::retry() {
    if (impl_->retry_user.empty() || impl_->retry_token.empty()) return;
    const std::string username = impl_->retry_user;
    const std::string token = impl_->retry_token;
    rc_client_logout(impl_->handle);
    impl_->state = Snapshot{};
    impl_->state.status = "Reconnexion en cours...";
    rc_client_begin_login_with_token(impl_->handle, username.c_str(), token.c_str(), Impl::logged, nullptr);
}
void Client::disconnect() {
    rc_client_logout(impl_->handle);
    impl_->retry_user.clear();
    impl_->retry_token.clear();
    impl_->state = Snapshot{};
    impl_->events.clear();
}
void Client::start_game(int console, const std::vector<std::uint8_t>& rom, ravenemu_bridge* core) {
    impl_->memory = core;
    impl_->console = console;
    impl_->rom = rom;
    impl_->state.status = "Recherche des succès officiels…";
    // RetroAchievements console IDs: GB=4, GBC=6, GBA=5.
    const uint32_t ra_console = console == RAVENEMU_GB ? 4u : console == RAVENEMU_GBC ? 6u : 5u;
    if (rc_client_get_user_info(impl_->handle))
        rc_client_begin_identify_and_load_game(impl_->handle, ra_console, nullptr,
            impl_->rom.data(), impl_->rom.size(), Impl::loaded, nullptr);
    else impl_->state.status = "Connectez votre compte pour activer les succès";
}
void Client::end_game() {
    impl_->memory = nullptr;
    impl_->rom.clear();
    // Keep the last recognized game's summary visible in the launcher.
    // Memory access is disconnected immediately when emulation stops.
}
void Client::pump(bool frame) { impl_->pump(frame); }
void Client::reset() { rc_client_reset(impl_->handle); }
void Client::set_hardcore(bool on) { rc_client_set_hardcore_enabled(impl_->handle, on ? 1 : 0); impl_->refresh(); }
bool Client::hardcore() const { return rc_client_get_hardcore_enabled(impl_->handle) != 0; }
bool Client::can_pause() const { return rc_client_can_pause(impl_->handle, nullptr) != 0; }
Snapshot Client::snapshot() const { return impl_->state; }
std::vector<Event> Client::drain_events() {
    auto events = std::move(impl_->events);
    impl_->events.clear();
    return events;
}
Client& client() { static Client c; return c; }
} // namespace ravenemudsk_ra

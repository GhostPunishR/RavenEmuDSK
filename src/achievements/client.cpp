// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemu/bridge.h>
#include <rc_client.h>
#include <curl/curl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
namespace ravenemudsk_ra {
namespace {
using Reply = ServerReply;
size_t write_body(char* bytes, size_t size, size_t count, void* ptr) {
    auto& body = *static_cast<std::string*>(ptr);
    if (size && count > (8 * 1024 * 1024) / size) return 0;
    const size_t n = size * count;
    if (n > 8 * 1024 * 1024 || body.size() > 8 * 1024 * 1024 - n) return 0;
    body.append(bytes, n);
    return n;
}
Reply https_request(std::string url, std::string post, std::string content_type) {
    Reply reply;
    // Never send login tokens to third-party hosts or follow redirects.
    if (url.rfind("https://retroachievements.org/", 0) != 0) { reply.status = -1; return reply; }
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
#ifndef RAVENEMUDSK_VERSION
#define RAVENEMUDSK_VERSION "0.1.0"
#endif
    const std::string agent = std::string("RavenEmu/") + RAVENEMUDSK_VERSION + " RavenCore/" + ravenemu_version();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
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
    std::string retry_user, retry_token, active_rom_path;
    std::unordered_map<std::string, std::optional<Snapshot>> game_cache;
    std::shared_ptr<SessionStore> storage;
    ServerTransport transport;
    bool token_login = false, profile_dirty = false, closing = false;
    std::shared_ptr<ProfileStore> profiles;
    Clock clock;
    std::int64_t login_retry_at = 0, game_retry_at = 0;
    unsigned login_failures = 0, game_failures = 0;
    rc_client_async_handle_t* game_load = nullptr;
    rc_client_async_handle_t* login_request = nullptr;
    static std::int64_t delay(unsigned& failures) {
        return std::min<std::int64_t>(60000, 5000LL << std::min(failures++, 4u));
    }
    static bool transient(int result) {
        return result == RC_NO_RESPONSE || result == RC_API_FAILURE || result == RC_INVALID_JSON;
    }
    void identify() {
        if (game_load) rc_client_abort_async(handle, game_load);
        game_load = nullptr;
        game_retry_at = 0;
        state.game_status = GameStatus::Loading;
        state.status = "Recherche des succès officiels…";
        const uint32_t id = console == RAVENEMU_GB ? 4u : console == RAVENEMU_GBC ? 6u : 5u;
        game_load = rc_client_begin_identify_and_load_game(handle, id, nullptr,
            rom.data(), rom.size(), loaded, nullptr);
    }
    void persist() {
        if (profile_dirty && state.logged_in) {
            if (!state.rom_path.empty()) {
                if (game_cache.size() >= 32) game_cache.clear();
                game_cache[state.rom_path] = state;
            }
            if (profiles) profiles->save(state);
        }
        profile_dirty = false;
    }

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
        if (impl.closing) {
            // SDK submissions are not asynchronous handles. Cancel them while
            // the handle is still alive, including retries scheduled by the SDK.
            const char message[] = "Session arrêtée";
            const rc_api_server_response_t response{message, sizeof(message)-1, RC_API_SERVER_RESPONSE_CLIENT_ERROR};
            callback(&response, userdata);
            return;
        }
        if (!req || !req->url) return;
        const std::string url = req->url;
        const std::string post = req->post_data ? req->post_data : "";
        const std::string type = req->content_type ? req->content_type : "application/x-www-form-urlencoded";
        impl.pending.push_back(Pending{callback, userdata, std::async(std::launch::async,
            [send = impl.transport, url, post, type] { return send(url, post, type); })});
    }
    static void logged(int result, const char*, rc_client_t* c, void*) {
        auto& impl = self(c);
        impl.login_request = nullptr;
        impl.state.login_pending = false;
        if (result == RC_OK) {
            impl.state.status = "Connecté à RetroAchievements";
            impl.state.logged_in = true;
            impl.state.online = true;
            impl.login_retry_at = 0;
            impl.login_failures = 0;
            impl.state.reconnect_scheduled = false;
            impl.profile_dirty = true;
            const auto* user = rc_client_get_user_info(c);
            if (user && user->username && user->token) {
                impl.retry_user = user->username;
                impl.retry_token = user->token;
                impl.state.can_retry = true;
                if (impl.storage) {
                    impl.state.session_saved = impl.storage->save({impl.retry_user, impl.retry_token});
                    if (!impl.state.session_saved)
                        impl.state.status = "Connecté, mais la session n'a pas pu être mémorisée dans le coffre du système";
                }
            }
            impl.events.push_back({"login", "RetroAchievements connecté", ""});
            // When the ROM was opened before authentication, identify it now.
            if (impl.memory && !impl.rom.empty()) {
                impl.identify();
            }
        } else {
            if (impl.token_login && (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN)) {
                const bool forgotten = !impl.storage || impl.storage->clear();
                impl.retry_user.clear();
                impl.retry_token.clear();
                impl.state.can_retry = false;
                impl.state.session_saved = false;
                impl.login_retry_at = 0;
                impl.state.reconnect_scheduled = false;
                impl.state.status = forgotten ? "Session expirée : reconnectez votre compte" :
                    "Session expirée : la session enregistrée n'a pas pu être effacée dans le coffre du système";
            } else {
                // Keep a saved token on transient network errors so Retry (or
                // the next launch) can reconnect without asking for a password.
                impl.state.status = impl.token_login ? "Reconnexion impossible : réessayez lorsque le réseau est disponible" :
                    "Connexion impossible : vérifiez votre compte et le réseau";
            }
            if (impl.token_login && !impl.retry_token.empty() && transient(result)) {
                impl.login_retry_at = impl.clock() + delay(impl.login_failures);
                impl.state.reconnect_scheduled = true;
                impl.state.status = "Hors ligne : reconnexion automatique programmée";
            }
            impl.events.push_back({"error", "Connexion RetroAchievements impossible", ""});
        }
    }
    static void loaded(int result, const char*, rc_client_t* c, void*) {
        auto& impl = self(c);
        impl.game_load = nullptr;
        const auto* game = rc_client_get_game_info(c);
        if (result == RC_OK && game && game->id) {
            rc_client_user_game_summary_t summary{};
            rc_client_get_user_game_summary(c, &summary);
            impl.state.game_status = summary.num_core_achievements ? GameStatus::Ready : GameStatus::NoAchievements;
            impl.state.status = summary.num_core_achievements ? "Jeu reconnu par RetroAchievements" :
                "Jeu reconnu, aucun succès officiel disponible";
            impl.state.online = true;
            impl.state.cached_profile = false;
            impl.state.rom_path = impl.active_rom_path;
            impl.game_failures = 0;
            impl.profile_dirty = true;
            impl.events.push_back({"info", summary.num_core_achievements ? "Succès disponibles" : "Aucun succès disponible", game->title});
        } else if (result == RC_NO_GAME_LOADED) {
            impl.state.game_status = GameStatus::UnknownRom;
            impl.state.status = "ROM non reconnue par RetroAchievements";
        } else if (transient(result)) {
            impl.state.game_status = GameStatus::Offline;
            impl.state.online = false;
            impl.state.status = "Serveur indisponible : recherche des succès à réessayer automatiquement";
            if (impl.memory && !impl.rom.empty()) impl.game_retry_at = impl.clock() + delay(impl.game_failures);
        } else {
            impl.state.game_status = GameStatus::Error;
            impl.state.status = "Impossible d'identifier le jeu : ROM ou réponse du serveur invalide";
        }
        if (impl.state.game_status != GameStatus::Ready && impl.state.game_status != GameStatus::NoAchievements)
            impl.events.push_back({"notice", impl.state.status, ""});
    }

    static void event(const rc_client_event_t* ev, rc_client_t* c) {
        auto& impl = self(c);
        if (impl.closing) return;
        if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && ev->achievement) {
            impl.profile_dirty = true;
            const auto& a = *ev->achievement;
            impl.events.push_back({"achievement", a.title ? a.title : "Succès débloqué", "",
                std::to_string(a.id), badge(a, true), static_cast<int>(a.points)});
        } else if (ev->type == RC_CLIENT_EVENT_DISCONNECTED) {
            impl.state.status = "RetroAchievements hors ligne : envois en attente, nouvelle tentative automatique";
            impl.state.online = false;
            impl.events.push_back({"offline", "RetroAchievements hors ligne", ""});
        } else if (ev->type == RC_CLIENT_EVENT_RECONNECTED) {
            impl.state.status = "RetroAchievements reconnecté";
            impl.state.online = true;
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

    void create_handle() {
        handle = rc_client_create(read, request);
        if (!handle) throw std::runtime_error("Initialisation rcheevos impossible");
        rc_client_set_userdata(handle, this);
        rc_client_set_get_time_millisecs_function(handle, [](const rc_client_t* c) -> rc_clock_t {
            const auto& impl = *static_cast<Impl*>(rc_client_get_userdata(c));
            return static_cast<rc_clock_t>(impl.clock() + (impl.closing ? 121000 : 0));
        });
        rc_client_set_host(handle, "https://retroachievements.org");
        rc_client_set_hardcore_enabled(handle, 0);
        rc_client_set_allow_background_memory_reads(handle, 0);
        rc_client_set_event_handler(handle, event);
    }
    Impl(std::shared_ptr<SessionStore> session_storage, ServerTransport send,
         std::shared_ptr<ProfileStore> public_profiles, Clock time)
        : storage(std::move(session_storage)), transport(send ? std::move(send) : ServerTransport(https_request)),
          profiles(std::move(public_profiles)), clock(time ? std::move(time) : Clock([] {
              return std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count();
          })) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Initialisation HTTPS impossible");
        try {
            create_handle();
            if (const auto saved = storage ? storage->load() : std::nullopt;
                saved && !saved->username.empty() && !saved->token.empty()) {
                retry_user = saved->username;
                retry_token = saved->token;
                if (profiles) profiles->load(retry_user, state);
                state.session_saved = true;
                begin_token_login("Reconnexion automatique en cours...");
            }
        } catch (...) {
            if (handle) rc_client_destroy(handle);
            curl_global_cleanup();
            throw;
        }
    }
    void close_handle() {
        if (!handle) return;
        closing = true;
        if (login_request) rc_client_abort_async(handle, login_request);
        if (game_load) rc_client_abort_async(handle, game_load);
        login_request = game_load = nullptr;
        // Keep the SDK alive until every transport callback has released its
        // bookkeeping. Only aborted login/game requests use SDK async handles.
        while (!pending.empty()) {
            Pending job = std::move(pending.back()); pending.pop_back();
            complete(job, true);
        }
        rc_client_idle(handle); // flush scheduled submissions with cancellation replies
        rc_client_destroy(handle);
        handle = nullptr;
        closing = false;
    }
    ~Impl() { close_handle(); curl_global_cleanup(); }
    static void complete(Pending& job, bool cancel = false) {
        Reply reply;
        try { reply = job.future.get(); } catch (...) { /* treated as a network failure */ }
        if (cancel) { reply.body = "Session arrêtée"; reply.status = RC_API_SERVER_RESPONSE_CLIENT_ERROR; }
        const rc_api_server_response_t response{reply.body.c_str(), reply.body.size(), reply.status};
        job.callback(&response, job.userdata);
    }
    void begin_token_login(const char* status) {
        const bool saved = state.session_saved;
        rc_client_logout(handle);
        state.logged_in = false;
        state.online = false;
        login_retry_at = 0;
        state.reconnect_scheduled = false;
        state.status = status;
        state.login_pending = true;
        state.can_retry = true;
        state.session_saved = saved;
        token_login = true;
        login_request = rc_client_begin_login_with_token(handle, retry_user.c_str(), retry_token.c_str(), logged, nullptr);
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
            if (state.softcore_points != user->score_softcore || state.hardcore_points != user->score) profile_dirty = true;
            state.softcore_points = user->score_softcore;
            state.hardcore_points = user->score;
        }
        state.hardcore = rc_client_get_hardcore_enabled(handle) != 0;
        if (!game || !game->id ||
            (state.game_status != GameStatus::Ready && state.game_status != GameStatus::NoAchievements)) return;
        state.game_title = game->title ? game->title : "";
        state.achievements.clear();
        state.unlocked_count = 0;
        state.total_count = 0;
        state.unlocked_points = 0;
        state.total_points = 0;
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
                        badge(*a), static_cast<int>(a->points),
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
                complete(job);
            } else ++i;
        }
        const auto now = clock();
        if (login_retry_at && now >= login_retry_at && !state.login_pending && !retry_token.empty())
            begin_token_login("Reconnexion automatique en cours...");
        if (game_retry_at && now >= game_retry_at && state.logged_in && memory && !rom.empty()) identify();
        if (frame && memory) rc_client_do_frame(handle);
        else rc_client_idle(handle);
        refresh();
        persist();
    }
};
// UI smoke tests must not load a real user's saved account or make login requests.
Client::Client() : Client(std::getenv("RAVENEMUDSK_UI_SMOKE") ? nullptr : make_session_store(), {},
    std::getenv("RAVENEMUDSK_UI_SMOKE") ? nullptr : make_profile_store()) {}
Client::Client(std::shared_ptr<SessionStore> storage, ServerTransport transport,
               std::shared_ptr<ProfileStore> profiles, Clock clock)
    : impl_(std::make_unique<Impl>(std::move(storage), std::move(transport), std::move(profiles), std::move(clock))) {}
Client::~Client() = default;
void Client::login(const std::string& user, const std::string& password) {
    if (user.empty() || password.empty() || impl_->state.login_pending || impl_->state.logged_in) return;
    // Switching accounts must never retain the previous user's public history or token.
    rc_client_logout(impl_->handle);
    impl_->login_retry_at = impl_->game_retry_at = 0;
    impl_->game_load = nullptr;
    impl_->retry_user.clear(); impl_->retry_token.clear();
    impl_->state = Snapshot{};
    impl_->game_cache.clear();
    if (impl_->profiles) impl_->profiles->load(user, impl_->state);
    impl_->state.logged_in = false;
    impl_->state.login_pending = true;
    impl_->token_login = false;
    impl_->state.status = "Connexion en cours…";
    impl_->login_request = rc_client_begin_login_with_password(impl_->handle, user.c_str(), password.c_str(), Impl::logged, nullptr);
}
void Client::retry() {
    if (impl_->state.logged_in) {
        if (impl_->memory && impl_->state.game_status == GameStatus::Offline) impl_->identify();
        return; // Preserve the SDK's pending award submissions across network outages.
    }
    if (impl_->state.login_pending || impl_->retry_user.empty() || impl_->retry_token.empty()) return;
    impl_->begin_token_login("Reconnexion en cours...");
}
void Client::disconnect() {
    const bool cache_forgotten = !impl_->profiles || impl_->state.username.empty() ||
        impl_->profiles->clear(impl_->state.username);
    impl_->login_retry_at = impl_->game_retry_at = 0;
    impl_->login_failures = impl_->game_failures = 0;
    impl_->profile_dirty = false;
    impl_->close_handle();
    impl_->create_handle();
    const bool forgotten = !impl_->storage || impl_->storage->clear();
    impl_->retry_user.clear();
    impl_->retry_token.clear();
    impl_->game_cache.clear();
    impl_->state = Snapshot{};
    if (!forgotten)
        impl_->state.status = "Déconnecté, mais le coffre du système n'a pas pu effacer la session enregistrée";
    else if (!cache_forgotten)
        impl_->state.status = "Déconnecté, mais le profil public mémorisé n'a pas pu être effacé";
    impl_->events.clear();
}
void Client::start_game(int console, const std::vector<std::uint8_t>& rom, ravenemu_bridge* core,
                        const std::string& rom_path) {
    impl_->active_rom_path = rom_path.empty() ? std::string{} :
        rom_identity(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(rom_path.data()), rom_path.size())));
    impl_->memory = core;
    impl_->console = console;
    impl_->rom = rom;
    if (impl_->game_load) rc_client_abort_async(impl_->handle, impl_->game_load);
    impl_->game_load = nullptr;
    impl_->game_retry_at = 0;
    rc_client_unload_game(impl_->handle);
    impl_->state.cached_profile = !impl_->state.game_title.empty();
    impl_->state.game_status = GameStatus::None;
    impl_->game_failures = 0;
    if (rc_client_get_user_info(impl_->handle)) impl_->identify();
    else if (!impl_->state.login_pending) impl_->state.status = "Connectez votre compte pour activer les succès";
}
void Client::end_game() {
    impl_->refresh();
    impl_->persist();
    if (impl_->game_load) rc_client_abort_async(impl_->handle, impl_->game_load);
    impl_->game_load = nullptr;
    impl_->game_retry_at = 0;
    impl_->memory = nullptr;
    impl_->rom.clear();
    rc_client_unload_game(impl_->handle);
    impl_->state.cached_profile = !impl_->state.game_title.empty();
}
void Client::pump(bool frame) { impl_->pump(frame); }
void Client::reset() { rc_client_reset(impl_->handle); }
void Client::set_hardcore(bool on) {
    if (on) {
        impl_->state.status = "Hardcore indisponible : validation de conformité encore nécessaire";
        return;
    }
    rc_client_set_hardcore_enabled(impl_->handle, 0);
    impl_->refresh();
}
bool Client::hardcore() const { return rc_client_get_hardcore_enabled(impl_->handle) != 0; }
bool Client::can_pause() const { return rc_client_can_pause(impl_->handle, nullptr) != 0; }
Snapshot Client::snapshot() const { return impl_->state; }
std::optional<Snapshot> Client::game_snapshot(const std::string& path) {
    const auto& user = impl_->state.username;
    if (user.empty() || path.empty()) return std::nullopt;
    const auto key = rom_identity(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())));
    if (key.empty()) return std::nullopt;
    if (impl_->state.rom_path == key && !impl_->state.game_title.empty()) return impl_->state;
    auto found = impl_->game_cache.find(key);
    if (found == impl_->game_cache.end()) {
        if (impl_->game_cache.size() >= 32) impl_->game_cache.clear();
        Snapshot cached;
        const bool loaded = impl_->profiles && impl_->profiles->load_game(user, key, cached);
        found = impl_->game_cache.emplace(key, loaded ? std::optional<Snapshot>(std::move(cached)) : std::nullopt).first;
    }
    auto result = found->second;
    if (result) result->cached_profile = true;
    return result;
}
std::vector<Event> Client::drain_events() {
    auto events = std::move(impl_->events);
    impl_->events.clear();
    return events;
}
Client& client() { static Client c; return c; }
} // namespace ravenemudsk_ra

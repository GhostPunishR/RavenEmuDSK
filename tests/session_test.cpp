// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace ravenemudsk_ra;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
class MemoryStore final : public SessionStore {
public:
    std::optional<SessionCredentials> session;
    bool writable = true;
    bool erasable = true;
    int loads = 0, saves = 0, clears = 0;
    std::optional<SessionCredentials> load() override { ++loads; return session; }
    bool save(const SessionCredentials& value) override {
        ++saves;
        if (!writable) return false;
        session = value;
        return true;
    }
    bool clear() override {
        ++clears;
        if (!erasable) return false;
        session.reset();
        return true;
    }
};
ServerReply success(const char* token = "FakeSessionToken") {
    return {std::string(R"({"Success":true,"User":"SessionUser","Token":")") + token +
        R"(","Score":12,"SoftcoreScore":3,"Messages":0})", 200};
}
struct Server {
    std::mutex mutex;
    ServerReply response = success();
    std::vector<std::string> posts;
    ServerTransport transport() {
        return [this](const std::string& url, const std::string& post, const std::string&) {
            require(url.rfind("https://retroachievements.org/", 0) == 0, "Unexpected login destination");
            std::lock_guard lock(mutex);
            posts.push_back(post);
            return response;
        };
    }
    void reply(ServerReply value) { std::lock_guard lock(mutex); response = std::move(value); }
    std::string last_request() { std::lock_guard lock(mutex); return posts.empty() ? "" : posts.back(); }
    std::size_t requests() { std::lock_guard lock(mutex); return posts.size(); }
};
void finish_login(Client& client) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (client.snapshot().login_pending && std::chrono::steady_clock::now() < deadline) {
        client.pump(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!client.snapshot().login_pending, "Login did not complete");
}
void saved_session_lifecycle() {
    auto store = std::make_shared<MemoryStore>();
    Server server;
    {
        Client first(store, server.transport());
        require(!first.snapshot().login_pending, "An empty store must not start a login");
        first.login("SessionUser", "FakePassword");
        first.login("SessionUser", "FakePassword");
        finish_login(first);
        require(first.snapshot().logged_in && first.snapshot().session_saved, "Successful login was not remembered");
        require(server.requests() == 1, "Duplicate clicks must not start duplicate logins");
        require(store->session && store->session->token == "FakeSessionToken", "Only the returned session token must be stored");
        require(store->session->token != "FakePassword", "A password must never be persisted");
    }
    server.reply(success("RotatedFakeToken"));
    {
        Client restarted(store, server.transport());
        require(restarted.snapshot().login_pending, "Restart must begin automatic login");
        finish_login(restarted);
        const auto request = server.last_request();
        require(request.find("&t=FakeSessionToken") != std::string::npos && request.find("&p=") == std::string::npos,
                "Automatic login must use a token, not a password");
        require(restarted.snapshot().logged_in && restarted.snapshot().username == "SessionUser",
                "Restart did not restore the account");
        require(store->session && store->session->token == "RotatedFakeToken", "Rotated tokens must replace the saved token");
        restarted.disconnect();
        require(!store->session && !restarted.snapshot().logged_in, "Explicit logout must forget the saved session");
    }
    const auto previous = server.requests();
    {
        Client signed_out(store, server.transport());
        require(!signed_out.snapshot().login_pending, "A forgotten account must not reconnect on restart");
    }
    require(server.requests() == previous, "Logout triggered an unwanted automatic login");
}
void network_failure_and_retry() {
    auto store = std::make_shared<MemoryStore>();
    store->session = SessionCredentials{"SessionUser", "FakeSessionToken"};
    Server server;
    server.reply({R"({"Success":false,"Error":"Service unavailable"})", 503});
    Client client(store, server.transport());
    finish_login(client);
    require(!client.snapshot().logged_in && client.snapshot().can_retry, "Offline login must offer retry");
    require(store->session && store->clears == 0, "A network error must not erase the session");
    server.reply(success());
    client.retry();
    finish_login(client);
    require(client.snapshot().logged_in, "Retry should reconnect with the retained token");
}
// A wrong password and an outage do not read the same: the service's reason is shown.
void password_login_reasons() {
    Server server;
    server.reply({R"({"Success":false,"Error":"Invalid User/Password combination. Please try again","Code":"invalid_credentials","Status":401})", 401});
    Client client(std::make_shared<MemoryStore>(), server.transport());
    client.login("SessionUser", "WrongPassword");
    finish_login(client);
    auto status = client.snapshot().status;
    require(!client.snapshot().logged_in && status.find("refusée") != std::string::npos &&
            status.find("Invalid User/Password combination") != std::string::npos, "A refused password must show the service's reason");
    const auto events = client.drain_events();
    require(std::any_of(events.begin(), events.end(), [](const Event& e) {
        return e.kind == "error" && e.detail.find("Invalid User/Password") != std::string::npos; }),
        "The refusal toast must carry the service's reason");
    server.reply({"", -2});
    client.login("SessionUser", "FakePassword");
    finish_login(client);
    status = client.snapshot().status;
    require(status.find("ne répond pas") != std::string::npos && status.find("refusée") == std::string::npos,
            "An outage must not read as a refused password");
    server.reply({R"({"Success":false,"Error":"Line one\nLine two","Code":"access_denied","Status":403})", 403});
    client.login("SessionUser", "FakePassword");
    finish_login(client);
    status = client.snapshot().status;
    require(status.find("Line one Line two") != std::string::npos && status.find('\n') == std::string::npos,
            "Server text must stay on one line");
}
// Without a readable reply, the status names the cause (firewall page, rate
// limit, TLS or DNS failure) instead of a bare "no response".
void transport_causes_are_named() {
    Server server;
    Client client(std::make_shared<MemoryStore>(), server.transport());
    const auto status_for = [&](ServerReply reply) {
        server.reply(std::move(reply));
        client.login("SessionUser", "FakePassword");
        finish_login(client);
        require(!client.snapshot().logged_in, "A failed transport must not sign in");
        return client.snapshot().status;
    };
    const auto has = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
    auto status = status_for({"<!DOCTYPE html><title>Just a moment...</title>", 403});
    require(has(status, "pare-feu (HTTP 403)"), "A firewall page must be named with its HTTP code");
    status = status_for({R"({"Success":false,"Error":"Too Many Attempts","Status":429})", 429});
    require(has(status, "trop de tentatives") && !has(status, "Too Many"), "A rate limit must say so in French");
    status = status_for({"", -2, "SSL peer certificate or SSH remote key was not OK"});
    require(has(status, "aucune réponse du serveur (SSL peer certificate"), "A TLS failure must be named");
    status = status_for({"<html>maintenance</html>", 503});
    require(has(status, "indisponible (HTTP 503)"), "A maintenance page must be named");
    status = status_for({"", -2});
    require(has(status, "ne répond pas"), "Without a known cause, the outage text stays");
    const auto events = client.drain_events();
    require(std::any_of(events.begin(), events.end(), [&](const Event& e) {
        return e.kind == "error" && has(e.detail, "SSL peer certificate"); }), "The failure toast must carry the cause");
    // A saved session that cannot reconnect names the cause too.
    auto store = std::make_shared<MemoryStore>();
    store->session = SessionCredentials{"SessionUser", "FakeSessionToken"};
    server.reply({"", -2, "Could not resolve hostname"});
    Client saved(store, server.transport());
    finish_login(saved);
    require(has(saved.snapshot().status, "Could not resolve hostname") && store->session,
            "A failed automatic reconnection must name its cause and keep the session");
}
void automatic_backoff_and_cached_profile() {
    auto store = std::make_shared<MemoryStore>();
    store->session = SessionCredentials{"SessionUser", "FakeSessionToken"};
    const auto root = std::filesystem::temp_directory_path() / ("raven-client-profile-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);} } cleanup{root};
    auto profiles = make_profile_store(root);
    Snapshot cached; cached.username="SessionUser";cached.display_name="Old name";cached.game_title="Last recognized game";
    cached.softcore_points=1;
    require(profiles->save(cached), "Could not prepare the public profile fixture");
    Server server; server.reply({"", -1});
    std::int64_t now=0;
    Client c(store,server.transport(),profiles,[&]{return now;});
    require(c.snapshot().cached_profile && c.snapshot().game_title==cached.game_title && !c.snapshot().logged_in,
        "Cached public data must be visible before authentication without granting login");
    finish_login(c);
    require(c.snapshot().reconnect_scheduled, "A transient token login failure must schedule automatic retry");
    now=4999;c.pump(false);require(server.requests()==1,"First retry must wait five seconds");
    now=5000;c.pump(false);finish_login(c);require(server.requests()==2,"First automatic retry missing");
    now=14999;c.pump(false);require(server.requests()==2,"Second retry must wait ten seconds");
    server.reply(success());now=15000;c.pump(false);finish_login(c);
    require(c.snapshot().logged_in && !c.snapshot().reconnect_scheduled && server.requests()==3,"Recovery must stop the backoff");
    require(c.snapshot().softcore_points==3 && c.snapshot().game_title==cached.game_title && c.snapshot().cached_profile,
        "Authentication must refresh account points while preserving the last recognized game");
    c.disconnect();Snapshot cleared;
    require(!profiles->load("SessionUser",cleared),"Logout must clear the cached profile too");
    now+=60000;c.pump(false);require(server.requests()==3,"Logout must cancel scheduled reconnection");
}
void expired_session() {
    for (const char* code : {"expired_token", "invalid_credentials"}) {
        auto store = std::make_shared<MemoryStore>();
        store->session = SessionCredentials{"SessionUser", "FakeSessionToken"};
        Server server;
        server.reply({std::string(R"({"Success":false,"Error":"Session rejected","Code":")") + code +
            R"(","Status":401})", 401});
        Client client(store, server.transport());
        finish_login(client);
        require(!client.snapshot().logged_in && !client.snapshot().can_retry, "Invalid tokens must require a fresh login");
        require(!store->session && store->clears == 1, "Invalid saved tokens must be removed");
        require(client.snapshot().status.find("Session expirée") != std::string::npos, "Token expiry must be explained");
    }
}
void logout_during_automatic_login() {
    auto store = std::make_shared<MemoryStore>();
    store->session = SessionCredentials{"SessionUser", "FakeSessionToken"};
    std::promise<void> release;
    const auto ready = release.get_future().share();
    Client client(store, [ready](const std::string&, const std::string&, const std::string&) {
        ready.wait();
        return success();
    });
    release.set_value();
    client.disconnect();
    for (int i = 0; i < 20; ++i) {
        client.pump(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(!client.snapshot().logged_in && !client.snapshot().login_pending && !store->session,
            "A late automatic login response must not undo explicit logout");
    require(store->saves == 0, "A cancelled login must not re-save a forgotten token");
}
void storage_failure_is_visible() {
    auto store = std::make_shared<MemoryStore>();
    store->writable = false;
    Server server;
    Client client(store, server.transport());
    client.login("SessionUser", "FakePassword");
    finish_login(client);
    require(client.snapshot().logged_in && !client.snapshot().session_saved, "A storage failure must not prevent this session's login");
    require(client.snapshot().status.find("mémorisée") != std::string::npos, "A failed credential write must be visible");
    store->erasable = false;
    client.disconnect();
    require(client.snapshot().status.find("effacer") != std::string::npos, "A failed credential deletion must be visible");
}
}
int main() {
    saved_session_lifecycle();
    network_failure_and_retry();
    password_login_reasons();
    transport_causes_are_named();
    expired_session();
    automatic_backoff_and_cached_profile();
    logout_during_automatic_login();
    storage_failure_is_visible();
    std::cout << "Session restart, retry, expiry and logout checks passed\n";
}

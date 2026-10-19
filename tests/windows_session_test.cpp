// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/session_store.hpp>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Cleanup {
    std::shared_ptr<ravenemudsk_ra::SessionStore> store;
    ~Cleanup() { if (store) store->clear(); }
};
}
int run_native_round_trip() {
    using namespace ravenemudsk_ra;
    const auto target = "RavenEmuTests/RetroAchievements/" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto store = make_session_store(target);
    require(bool(store), "Windows credential storage must be available");
    Cleanup cleanup{store};
    require(!store->load(), "A fresh test target must have no saved session");
    if (!store->save({"SessionTestUser", "FakeNativeToken"})) {
        const auto error = GetLastError();
        if (error == ERROR_NO_SUCH_LOGON_SESSION || error == ERROR_NOT_SUPPORTED) {
            std::cout << "Credential Manager is unavailable in this Windows logon session\n";
            return 77;
        }
        throw std::runtime_error("Could not write the native test credential");
    }
    auto restarted = make_session_store(target);
    auto saved = restarted->load();
    require(saved && saved->username == "SessionTestUser" && saved->token == "FakeNativeToken",
            "Credential Manager did not restore the saved session");
    require(restarted->save({"SessionTestUser", "RotatedFakeToken"}), "Could not replace the native session");
    require(!restarted->save({"", ""}), "Empty credentials must not overwrite a valid session");
    saved = store->load();
    require(saved && saved->token == "RotatedFakeToken", "Replacement credential was not retained");
    const std::string journal_key(64,'a');
    require(restarted->save({"SessionTestUser",journal_key}),"Could not store outbox authentication key");
    saved=store->load();require(saved&&saved->token==journal_key,"Native journal key was truncated");
    require(store->clear() && store->clear(), "Native logout must be repeatable");
    require(!restarted->load(), "The deleted native credential was restored");
    std::cout << "Native Windows session storage round trip passed\n";
    return 0;
}
int main() {
    try { return run_native_round_trip(); }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <memory>
#include <optional>
#include <string>

namespace ravenemudsk_ra {
struct SessionCredentials {
    std::string username;
    std::string token;
};
// Only an API token is retained. Passwords never enter this interface.
class SessionStore {
public:
    virtual ~SessionStore() = default;
    virtual std::optional<SessionCredentials> load() = 0;
    virtual bool save(const SessionCredentials& session) = 0;
    virtual bool clear() = 0;
};
// Windows Credential Manager, scoped to the current OS user. Other systems
// return nullptr rather than writing credentials to an unprotected file.
// A separate target can be supplied by native integration tests.
std::shared_ptr<SessionStore> make_session_store(
    const std::string& target = "RavenEmu/RetroAchievements");
} // namespace ravenemudsk_ra

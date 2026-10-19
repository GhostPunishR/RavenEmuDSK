// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/session_store.hpp>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace ravenemudsk_ra {
struct Submission {
    std::string id, username, game_hash;
    std::uint32_t object_id = 0;
    std::int32_t score = 0;
    bool leaderboard = false, hardcore = false;
    std::int64_t earned_at = 0; // Unix seconds; preserved across SDK retries.
};
// Authenticated records contain no token. The HMAC key stays in the OS vault.
class Outbox {
public:
    using VaultFactory = std::function<std::shared_ptr<SessionStore>(const std::string&)>;
    explicit Outbox(std::filesystem::path root, VaultFactory vaults = {});
    ~Outbox();
    bool available(const std::string& user);
    std::vector<Submission> entries(const std::string& user);
    std::string put(Submission submission);
    bool erase(const std::string& user, const std::string& id);
    bool clear(const std::string& user);
    bool healthy() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::shared_ptr<Outbox> make_outbox();
}

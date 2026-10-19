// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
namespace ravenemudsk_ui {
struct Version {
    int major = 0, minor = 0, patch = 0;
};
// Accepts "0.4.1" or "v0.4.1"; anything else is rejected.
std::optional<Version> parse_version(std::string_view text);
bool is_newer(const Version& candidate, const Version& current);
std::string to_string(const Version& version);
// "tag_name" of a GitHub "latest release" response, if present.
std::optional<std::string> latest_tag(std::string_view json);
// Release page built from a validated version, never from the response.
std::string release_page(const Version& version);

// Background check of the latest GitHub release. Only the public release
// number is read from api.github.com; nothing about the player is sent.
class UpdateCheck {
public:
    enum class Status { Idle, Checking, UpToDate, NoRelease, Available, Failed };
    static bool supported();  // false when built without an HTTPS client
    void start(const std::string& current_version);  // ignored while checking
    Status status() const;
    std::optional<Version> latest() const;
    std::string error() const;
private:
    // Shared with a detached worker, so quitting never waits for the network.
    struct Shared {
        std::mutex mutex;
        Status status = Status::Idle;
        std::optional<Version> latest;
        std::string error;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};
} // namespace ravenemudsk_ui

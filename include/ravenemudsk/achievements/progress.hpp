// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/client.hpp>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace ravenemudsk_ra {
struct Progress {
    std::string id, title, detail, badge_url;
    std::int64_t changed_at = 0;
};
class ProgressNotifications {
public:
    void update(const std::vector<Achievement>& entries, std::int64_t now_ms);
    void advance(std::int64_t now_ms);
    void dismiss();
    void clear();
    const std::optional<Progress>& current() const { return current_; }
    size_t queued() const { return pending_.size(); }
private:
    struct Measurement { float percent=0.f; std::string text; };
    std::unordered_map<std::string, Measurement> best_;
    std::deque<Progress> pending_;
    std::optional<Progress> current_;
    std::int64_t transition_at_ = 0;
    bool initialized_ = false;
};
} // namespace ravenemudsk_ra

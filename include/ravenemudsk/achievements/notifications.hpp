// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/progress.hpp>
#include <functional>
#include <unordered_set>
namespace ravenemudsk_ra {
struct Popup { std::string text, detail, badge; };
// Logical display time stops while the pause menu hides the cards.
class Notifications {
public:
    void set_paused(std::int64_t wall_ms, bool paused);
    void event(const Event& event);
    void update(const std::vector<Achievement>& entries, std::int64_t wall_ms);
    void advance(std::int64_t wall_ms, const std::function<bool(const std::string&)>& image_ready);
    void clear();
    double time() const { return time_ms_ / 1000.0; }
    double notice_since() const { return notice_at_ / 1000.0; }
    const std::optional<Popup>& notice() const { return notice_; }
    const std::optional<Progress>& progress() const { return progress_.current(); }
    const auto& indicators() const { return indicators_; }
    std::size_t queued() const { return queue_.size(); }
private:
    void tick(std::int64_t wall_ms);
    std::int64_t time_ms_=0, last_wall_=-1, waiting_at_=-1, notice_at_=0;
    bool paused_=false;
    ProgressNotifications progress_;
    std::deque<Popup> queue_;
    std::optional<Popup> notice_;
    std::unordered_set<std::string> unlocked_;
    std::vector<std::pair<std::string,std::string>> indicators_;
};
}

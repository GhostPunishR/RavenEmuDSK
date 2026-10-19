// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/notifications.hpp>
#include <algorithm>
namespace ravenemudsk_ra {
void Notifications::tick(std::int64_t wall) {
    if (last_wall_ >= 0 && !paused_) time_ms_ += std::max<std::int64_t>(0, wall - last_wall_);
    last_wall_ = wall;
}
void Notifications::set_paused(std::int64_t wall, bool paused) { tick(wall); paused_ = paused; }
void Notifications::event(const Event& event) {
    if(event.kind=="challenge_show"||event.kind=="tracker_show"||
       event.kind=="challenge_hide"||event.kind=="tracker_hide") {
        const auto prefix=event.kind.substr(0,event.kind.find('_'));
        const std::string key=prefix+":"+event.id;
        auto existing=std::find_if(indicators_.begin(),indicators_.end(),
            [&](const auto& item){return item.first==key;});
        if(event.kind.ends_with("_hide")) {
            if(existing!=indicators_.end())indicators_.erase(existing);
        } else {
            const std::string value=event.title+
                (event.detail.empty()?std::string{}:std::string(" \xC2\xB7 ")+event.detail);
            if(existing!=indicators_.end())existing->second=value;
            else if(indicators_.size()<16)indicators_.emplace_back(key,value);
        }
        return;
    }
    std::string label;
    std::string badge;
    if(event.kind=="achievement") {
        if (!event.id.empty() && unlocked_.contains(event.id)) return;
        if (queue_.size() >= 2048) return;
        if (!event.id.empty()) unlocked_.insert(event.id);
        label="Succès débloqué \xC2\xB7 "+event.title+
            " ("+std::to_string(event.points)+" pts)";
        badge=event.badge_url;
    } else if(event.kind=="leaderboard_started")label="Classement commencé \xC2\xB7 "+event.title;
    else if(event.kind=="leaderboard_failed")label="Classement annulé \xC2\xB7 "+event.title;
    else if(event.kind=="leaderboard_submitted")label="Classement envoyé \xC2\xB7 "+event.title;
    else if(event.kind=="leaderboard_scoreboard")label="Classement terminé \xC2\xB7 "+event.title;
    else if(event.kind=="notice"||event.kind=="info"||event.kind=="login"||event.kind=="offline"||event.kind=="online"||
            event.kind=="hardcore_reset")label=event.title;
    if(label.empty())return;
    if(queue_.size()<2048) queue_.push_back({label,
        event.kind=="achievement"||event.kind.starts_with("leaderboard_")?std::string{}:event.detail,
        badge});
}
void Notifications::update(const std::vector<Achievement>& entries, std::int64_t wall) {
    tick(wall);
    if (!paused_) progress_.update(entries, time_ms_);
}
void Notifications::advance(std::int64_t wall, const std::function<bool(const std::string&)>& ready) {
    tick(wall);
    if (paused_) return;
    progress_.advance(time_ms_);
    if (notice_ && time_ms_ - notice_at_ >= 5200) notice_.reset();
    if (!notice_ && !queue_.empty()) {
        const auto& next = queue_.front();
        if (waiting_at_ < 0) waiting_at_ = time_ms_;
        if (next.badge.empty() || ready(next.badge) || time_ms_ - waiting_at_ >= 2000) {
            notice_ = std::move(queue_.front()); queue_.pop_front();
            notice_at_ = time_ms_; waiting_at_ = -1;
        }
    }
}
void Notifications::clear() {
    progress_.clear(); queue_.clear(); notice_.reset(); indicators_.clear(); unlocked_.clear(); waiting_at_ = -1;
}
}

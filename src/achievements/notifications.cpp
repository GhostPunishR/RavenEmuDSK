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
    std::string heading;
    std::string badge;
    bool trophy=false;
    if(event.kind=="achievement") {
        if (!event.id.empty() && unlocked_.contains(event.id)) return;
        if (queue_.size() >= 2048) return;
        if (!event.id.empty()) unlocked_.insert(event.id);
        heading="Succès débloqué \xC2\xB7 "+std::to_string(event.points)+(event.points>1?" points":" point");
        badge=event.badge_url;
        trophy=true;
    } else if(event.kind=="mastered"||event.kind=="completed") {
        heading=event.kind=="mastered"?"Jeu maîtrisé":"Jeu terminé";
        badge=event.badge_url;
        trophy=true;
    } else if(event.kind=="leaderboard_started")heading="Classement commencé";
    else if(event.kind=="leaderboard_failed")heading="Classement annulé";
    else if(event.kind=="leaderboard_submitted")heading="Classement envoyé";
    else if(event.kind=="leaderboard_scoreboard")heading="Classement terminé";
    else if(event.kind=="notice"||event.kind=="info"||event.kind=="login"||event.kind=="offline"||event.kind=="online"||
            event.kind=="hardcore_reset") {
        if(event.title.empty())return;
        // A notice is its own heading; its detail is the second line.
        if(queue_.size()<2048) queue_.push_back({event.title,event.detail,"",event.title,event.detail,false});
        return;
    }
    if(heading.empty())return;
    // Desktop card: one line naming the award, the points in parentheses.
    std::string text=event.kind=="achievement"?
        "Succès débloqué \xC2\xB7 "+event.title+" ("+std::to_string(event.points)+" pts)":
        heading+" \xC2\xB7 "+event.title;
    if(queue_.size()<2048) queue_.push_back({std::move(text),"",badge,heading,event.title,trophy});
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

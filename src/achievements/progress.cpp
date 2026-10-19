// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/progress.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace ravenemudsk_ra {
namespace {
constexpr std::int64_t DISPLAY_MS=2000;
constexpr std::int64_t MAX_AGE_MS=10000;
constexpr std::size_t MAX_PENDING=20;
bool eligible(const Achievement& a) {
    return a.state==1 && !a.progress.empty() && std::isfinite(a.measured_percent) &&
           a.measured_percent>0.f && a.measured_percent<=100.f;
}
}
void ProgressNotifications::update(const std::vector<Achievement>& entries,std::int64_t now_ms) {
    const bool baseline=!initialized_;
    if(!entries.empty())initialized_=true;
    std::unordered_set<std::string> present;
    for(const auto& a:entries)if(eligible(a))present.insert(a.id);
    pending_.erase(std::remove_if(pending_.begin(),pending_.end(),
        [&](const Progress& p){return !present.contains(p.id);}),pending_.end());
    if(current_ && !present.contains(current_->id)) {
        current_.reset();transition_at_=0;
    }
    for(const auto& a:entries){
        const auto percent=a.measured_percent;
        if(!std::isfinite(percent)||percent<0.f||percent>100.f)continue;
        const auto it=best_.find(a.id);
        const float previous=it==best_.end()?0.f:it->second.percent;
        const std::string old_text=it==best_.end()?std::string{}:it->second.text;
        if(it==best_.end()||percent>previous)best_[a.id]={percent,a.progress};
        if(percent<previous) {
            pending_.erase(std::remove_if(pending_.begin(),pending_.end(),
                [&](const Progress& p){return p.id==a.id;}),pending_.end());
            best_[a.id]={percent,a.progress};
            if(current_ && current_->id==a.id) { current_.reset(); transition_at_=0; }
            continue;
        }
        if(!eligible(a))continue;
        Progress next{a.id,a.title,a.progress,a.badge_url,now_ms};
        if(current_ && current_->id==a.id && now_ms<transition_at_) {
            current_->title=next.title;
            current_->detail=next.detail;
            current_->badge_url=next.badge_url;
        } else if(!baseline && percent>previous && a.progress!=old_text) {
            auto existing=std::find_if(pending_.begin(),pending_.end(),
                [&](const Progress& p){return p.id==a.id;});
            if(existing!=pending_.end()) {
                const auto originally=existing->changed_at;
                *existing=next;existing->changed_at=originally;
            } else if(pending_.size()<MAX_PENDING)pending_.push_back(next);
        }
    }
    advance(now_ms);
}
void ProgressNotifications::advance(std::int64_t now_ms) {
    if(current_ && now_ms<transition_at_)return;
    current_.reset();transition_at_=0;
    while(!pending_.empty()){
        auto next=std::move(pending_.front());
        pending_.pop_front();
        if(now_ms-next.changed_at>MAX_AGE_MS)continue;
        current_=std::move(next);transition_at_=now_ms+DISPLAY_MS;
        break;
    }
}
void ProgressNotifications::dismiss(){
    current_.reset();pending_.clear();transition_at_=0;
}
void ProgressNotifications::clear(){
    dismiss();best_.clear();initialized_=false;
}
} // namespace ravenemudsk_ra

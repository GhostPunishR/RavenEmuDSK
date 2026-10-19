// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/notifications.hpp>
#include <stdexcept>
#include <iostream>
using namespace ravenemudsk_ra;
void require(bool ok) { if (!ok) throw std::runtime_error("Notification regression"); }
int main() {
    Notifications n;
    const auto ready = [](const std::string&) { return true; };
    n.advance(0, ready);
    for(int i=0;i<32;++i) n.event({"achievement", "Award " + std::to_string(i), "", std::to_string(i), "badge", 5});
    n.event({"achievement", "Duplicate", "", "0", "badge", 5});
    n.advance(100, ready);
    require(n.queued()==31 && n.notice()->text.find("Award 0")!=std::string::npos);
    n.set_paused(1100, true);
    n.advance(601100, ready);
    require(n.notice() && n.time()==1.1);
    n.set_paused(601100, false);
    n.advance(605300, ready);
    require(n.notice() && n.notice()->text.find("Award 1 (")!=std::string::npos);
    for(int i=2;i<32;++i) {
        n.advance(605300+(i-1)*5200,ready);
        require(n.notice() && n.notice()->text.find("Award "+std::to_string(i)+" (")!=std::string::npos);
    }
    n.event({"challenge_show","Challenge","","7"});
    n.event({"tracker_show","Board","01:00","7"});
    n.event({"tracker_show","Board","02:00","7"});
    require(n.indicators().size()==2 && n.indicators()[1].second.find("02:00")!=std::string::npos);
    n.event({"challenge_hide","","","7"}); require(n.indicators().size()==1);
    n.clear(); require(!n.notice() && n.queued()==0 && n.indicators().empty());
    n.event({"achievement","After reset","","0","",5});
    n.advance(900000,ready);require(n.notice().has_value());
    n.clear();
    n.event({"achievement","No badge yet","","42","badge",5});
    const auto missing = [](const std::string&) { return false; };
    n.advance(900000,missing);require(!n.notice());
    n.advance(902000,missing);require(n.notice().has_value());
    n.clear();
    Achievement a; a.id="x";a.title="Progress";a.state=1;a.progress="10/100";a.measured_percent=10;
    n.update({a},903000);
    a.progress="20/100";a.measured_percent=20;n.update({a},903100);require(n.progress().has_value());
    n.set_paused(903500,true);n.advance(999999,ready);require(n.progress().has_value());
    n.set_paused(999999,false);n.advance(1000100,ready);require(n.progress().has_value());
    a.progress="1/100";a.measured_percent=1;n.update({a},1000200);require(!n.progress());
    a.progress="2/100";a.measured_percent=2;n.update({a},1000300);require(n.progress() && n.progress()->detail=="2/100");
    std::cout << "FIFO unlocks, deduplication, indicators, badge timeout and menu pause passed\n";
}

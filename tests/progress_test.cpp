// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/progress.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
using ravenemudsk_ra::Achievement;
using ravenemudsk_ra::ProgressNotifications;
Achievement measured(const char* id,float percent,const char* text) {
    Achievement a;
    a.id=id;a.title=std::string("Success ")+id;a.progress=text;
    a.measured_percent=percent;a.state=1;a.badge_url="https://media.retroachievements.org/Badge/123.png";
    return a;
}
int main(){
    ProgressNotifications p;
    auto a=measured("1",10.f,"10 / 100");
    auto b=measured("2",40.f,"40 / 100");
    p.update({a,b},1000);
    assert(!p.current());
    a.measured_percent=15.f;a.progress="15 / 100";
    b.measured_percent=45.f;b.progress="45 / 100";
    p.update({a,b},1200);
    assert(p.current() && p.current()->id=="1" && p.queued()==1);
    a.measured_percent=20.f;a.progress="20 / 100";
    p.update({a,b},1600);
    assert(p.current() && p.current()->detail=="20 / 100");
    p.advance(3200);
    assert(p.current() && p.current()->id=="2");
    p.advance(5200);
    assert(!p.current());
    a.measured_percent=2.f;a.progress="2 / 100";
    p.update({a,b},5300);
    assert(!p.current());
    a.measured_percent=3.f;a.progress="3 / 100";
    p.update({a,b},5400);
    assert(p.current() && p.current()->detail=="3 / 100");
    p.clear();
    p.update({a},6000);
    assert(!p.current());
    a.state=2;
    p.update({a},6200);
    assert(!p.current());
}

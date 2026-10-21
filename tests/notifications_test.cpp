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
    // The TV banner's two lines: what happened, then what it is about.
    n.clear();
    n.event({"achievement","Maître de la Ligue","","77","badge",25});
    n.advance(1100000,ready);
    require(n.notice() && n.notice()->heading=="Succès débloqué \xC2\xB7 25 points" &&
            n.notice()->title=="Maître de la Ligue" && n.notice()->trophy &&
            n.notice()->text=="Succès débloqué \xC2\xB7 Maître de la Ligue (25 pts)");
    n.clear();
    n.event({"achievement","Premier pas","","78","",1});
    n.advance(1200000,ready);
    require(n.notice() && n.notice()->heading=="Succès débloqué \xC2\xB7 1 point");
    n.clear();
    n.event({"mastered","Pokémon Jaune","","","game.png"});
    n.advance(1300000,ready);
    require(n.notice() && n.notice()->heading=="Jeu maîtrisé" && n.notice()->title=="Pokémon Jaune" &&
            n.notice()->badge=="game.png" && n.notice()->trophy && n.notice()->text=="Jeu maîtrisé \xC2\xB7 Pokémon Jaune");
    n.clear();
    n.event({"completed","Pokémon Jaune","","",""});
    n.advance(1400000,ready);
    require(n.notice() && n.notice()->heading=="Jeu terminé");
    n.clear();
    n.event({"leaderboard_started","Course","","9"});
    n.advance(1500000,ready);
    require(n.notice() && n.notice()->heading=="Classement commencé" && n.notice()->title=="Course" && !n.notice()->trophy &&
            n.notice()->text=="Classement commencé \xC2\xB7 Course");
    n.clear();
    n.event({"offline","RetroAchievements hors ligne","Envois en attente"});
    n.advance(1600000,ready);
    require(n.notice() && n.notice()->heading=="RetroAchievements hors ligne" && n.notice()->title=="Envois en attente" &&
            n.notice()->text=="RetroAchievements hors ligne" && n.notice()->detail=="Envois en attente");
    std::cout << "FIFO unlocks, deduplication, indicators, badge timeout, menu pause and banner lines passed\n";
}

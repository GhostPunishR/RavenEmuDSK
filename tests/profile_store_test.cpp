// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ra;
namespace fs=std::filesystem;
void require(bool ok) { if(!ok)throw std::runtime_error("Profile cache regression"); }
int main() {
    const auto root=fs::temp_directory_path()/("raven-profile-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{root};
    auto store=make_profile_store(root);
    Snapshot s;s.username="TestUser";s.display_name="Test \"User\"";s.game_title="Last Game";
    s.softcore_points=23;s.total_count=1;s.total_points=5;s.logged_in=true;s.login_pending=true;s.session_saved=true;
    s.hardcore=true;s.game_hardcore=true;s.game_hash="0123456789abcdef0123456789abcdef";
    s.rich_presence="At the final boss";s.leaderboards.push_back({4401,"High score","Record points"});
    s.achievements.push_back({"123","An award","A description\nwith newline","10/10","https://media.retroachievements.org/Badge/123_lock.png",5,1,10,false});
    require(store->save(s));
    Snapshot loaded; require(store->load("testuser",loaded));
    require(loaded.cached_profile && !loaded.logged_in && !loaded.login_pending && !loaded.session_saved);
    require(loaded.game_title==s.game_title && loaded.achievements.size()==1 && loaded.achievements[0].description==s.achievements[0].description);
    require(loaded.achievements[0].progress.empty() && loaded.softcore_points==23);
    require(loaded.game_hardcore&&!loaded.hardcore&&loaded.game_hash==s.game_hash&&loaded.rich_presence==s.rich_presence);
    require(loaded.leaderboards.size()==1&&loaded.leaderboards[0].id==4401&&!loaded.hardcore_preferred);
    // Version 4 keeps each achievement's unlock date, type and rarity.
    s.achievements.push_back({"124","Missable","","","",10,2,0.f,true,1700000000,1,12.5f,3.25f});
    require(store->save(s)&&store->load("TestUser",loaded)&&loaded.achievements.size()==2);
    const auto& kept=loaded.achievements[1];
    require(kept.unlocked&&kept.unlock_time==1700000000&&kept.type==1&&kept.rarity==12.5f&&kept.rarity_hardcore==3.25f);
    require(!store->load("AnotherUser",loaded));
    s.total_count=0;s.achievements.clear();s.game_title="Changed";require(store->save(s));
    require(store->load("TestUser",loaded) && loaded.game_title=="Changed");
    s.rom_path=rom_identity(root / "one" / "Game.gba");s.game_title="Edition one";
    require(store->save(s));
    const auto first=s.rom_path;
    s.rom_path=rom_identity(root / "two" / "Game.gba");s.game_title="Edition two";
    require(store->save(s));
    const auto second=s.rom_path;
    store=make_profile_store(root); // Recreate storage as on a restart.
    require(store->load_game("testuser",first,loaded) && loaded.game_title=="Edition one");
    require(store->load_game("TESTUSER",second,loaded) && loaded.game_title=="Edition two");
    require(!store->load_game("AnotherUser",first,loaded));
    require(!store->load_game("TestUser",rom_identity(root / "unknown.gba"),loaded));
#ifndef _WIN32
    // Account case is ignored; case-sensitive ROM paths must remain distinct.
    s.rom_path=rom_identity(root / "one" / "game.gba");s.game_title="Lowercase file";require(store->save(s));
    require(store->load_game("TestUser",first,loaded) && loaded.game_title=="Edition one");
#endif
    fs::path file;
    for(const auto& entry:fs::directory_iterator(root)) if(entry.is_regular_file())file=entry.path();
    {std::ofstream out(file,std::ios::trunc);out<<"RavenEmuProfile1\n\"TestUser\" \"Legacy\" \"\" 0 0 \"Legacy game\" 0 0 0 0 0\n";}
    require(store->load("TestUser",loaded) && loaded.game_title=="Legacy game" && loaded.rom_path.empty());
    {std::ofstream out(file,std::ios::trunc);out<<"RavenEmuProfile3\n\"TestUser\" \"Legacy\" \"\" 0 0 \"Legacy game\" 0 1 0 5 1 \"\" 0 \"\" \"\" 0\n"
        "\"7\" \"Old\" \"Desc\" \"\" 5 0\n";}
    require(store->load("TestUser",loaded) && loaded.achievements.size()==1 && loaded.achievements[0].unlock_time==0 &&
        loaded.achievements[0].type==0 && loaded.achievements[0].rarity==0.f);
    {std::ofstream out(file,std::ios::trunc);out<<"RavenEmuProfile4\n\"TestUser\" \"Bad\" \"\" 0 0 \"Game\" 0 1 0 5 1 \"\" 0 \"\" \"\" 0\n"
        "\"7\" \"Old\" \"Desc\" \"\" 5 0 0 9 0 0\n";}
    require(!store->load("TestUser",loaded)); // Unknown achievement type.
    {std::ofstream out(file,std::ios::trunc);out<<"RavenEmuProfile1\n\"OtherUser\"";}
    require(!store->load("TestUser",loaded));
    require(store->clear("TestUser") && store->clear("TestUser"));
    require(!store->load_game("TestUser",first,loaded) && !store->load_game("TestUser",second,loaded));
    std::cout<<"Public profile roundtrip, isolation, replacement and corrupt cache passed\n";
}

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
    s.achievements.push_back({"123","An award","A description\nwith newline","10/10","https://media.retroachievements.org/Badge/123_lock.png",5,1,10,false});
    require(store->save(s));
    Snapshot loaded; require(store->load("testuser",loaded));
    require(loaded.cached_profile && !loaded.logged_in && !loaded.login_pending && !loaded.session_saved);
    require(loaded.game_title==s.game_title && loaded.achievements.size()==1 && loaded.achievements[0].description==s.achievements[0].description);
    require(loaded.achievements[0].progress.empty() && loaded.softcore_points==23);
    require(!store->load("AnotherUser",loaded));
    s.total_count=0;s.achievements.clear();s.game_title="Changed";require(store->save(s));
    require(store->load("TestUser",loaded) && loaded.game_title=="Changed");
    const auto file=fs::directory_iterator(root)->path();
    {std::ofstream out(file,std::ios::trunc);out<<"RavenEmuProfile1\n\"OtherUser\"";}
    require(!store->load("TestUser",loaded));
    require(store->clear("TestUser") && store->clear("TestUser"));
    std::cout<<"Public profile roundtrip, isolation, replacement and corrupt cache passed\n";
}

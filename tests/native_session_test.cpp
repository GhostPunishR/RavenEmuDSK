// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/session_store.hpp>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ra;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main() {
    const auto target="RavenEmuTests/RetroAchievements/"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto store=make_session_store(target);
    require(store!=nullptr,"Native credential backend missing");
    struct Cleanup { std::shared_ptr<SessionStore> store;~Cleanup(){store->clear();} } cleanup{store};
    require(!store->load(),"Fresh target must be empty");
    require(store->clear(),"Deleting a missing target must succeed");
    require(store->save({"NativeTestUser","SyntheticTokenA"}),"Native credential write failed");
    store=make_session_store(target);
    auto session=store->load();
    require(session && session->username=="NativeTestUser" && session->token=="SyntheticTokenA","Native restart roundtrip failed");
    require(store->save({"UpdatedUser","SyntheticTokenB"}),"Native credential update failed");
    session=store->load();require(session && session->username=="UpdatedUser" && session->token=="SyntheticTokenB","Native update not visible");
    require(!store->save({"","Invalid"}),"Invalid credentials must not overwrite the item");
    const std::string journal_key(64,'a');
    require(store->save({"UpdatedUser",journal_key}),"Native outbox key write failed");
    session=make_session_store(target)->load();
    require(session&&session->token==journal_key,"Native vault did not retain a full outbox authentication key");
    require(store->clear() && !store->load() && store->clear(),"Native explicit logout failed");
    require(!make_session_store(std::string("bad\0target",10)),"Invalid native target accepted");
    std::cout<<"Native credential restart, update and logout passed\n";
}

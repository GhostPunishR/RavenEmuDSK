// SPDX-License-Identifier: AGPL-3.0-only
#include "ra_fixture.hpp"
void unlock_on_real_core(int system) {
    const auto rom=rom_for(system);Core core(system,rom);TestServer server;server.console=system==RAVENEMU_GBA?5:system==RAVENEMU_GBC?6:4;
    std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
    c.login("GameUser","SyntheticPassword");wait(c,[&]{return !c.snapshot().login_pending;});
    require(c.snapshot().logged_in,"Synthetic login failed");
    c.set_hardcore(true);require(!c.hardcore(),"Hardcore must require a protected offline journal");
    const auto path=(std::filesystem::temp_directory_path() / ("raven-game-"+std::to_string(system)) / "Game.gba").generic_string();
    require(!c.game_snapshot(path),"An unsynchronized ROM must not borrow the last game's summary");
    c.start_game(system,rom,core.handle,path);wait(c,[&]{return c.snapshot().game_status!=GameStatus::Loading;});
    require(c.snapshot().game_status==GameStatus::Ready && c.snapshot().total_count==1,"Achievement set not loaded");
    require(c.snapshot().achievements[0].badge_url.find("_lock")!=std::string::npos,"A locked achievement must use its locked badge");
    require(c.game_snapshot(path) && c.game_snapshot(path)->unlocked_count==0 && c.game_snapshot(path)->total_count==1,
        "The library summary must appear when the SDK recognizes this ROM");
    c.drain_events();c.pump(true); // initial false memory exits SDK waiting state
    std::vector<std::uint32_t> pixels(ravenemu_video_width(core.handle)*ravenemu_video_height(core.handle));
    require(ravenemu_run_frame(core.handle,pixels.data(),pixels.size()),"Synthetic ROM frame failed");
    std::uint8_t memory=0;const auto address=system==RAVENEMU_GBA?0x8000u:0xc000u;
    require(ravenemu_read_achievement_memory(core.handle,address,&memory,1)==1 && memory==0x5a,"Synthetic ROM did not write the RA address space");
    server.award_offline=true;c.pump(true);
    require(c.snapshot().unlocked_count==1,"SDK did not detect the real core's memory write");
    require(c.game_snapshot(path)->unlocked_count==1,"The library summary must update immediately after an unlock");
    require(c.snapshot().achievements[0].badge_url.find("_lock")==std::string::npos,"Unlocked badge must be colored");
    auto events=c.drain_events();
    require(std::count_if(events.begin(),events.end(),[](const Event& e){return e.kind=="achievement";})==1,"Unlock notification missing or duplicated");
    wait(c,[&]{return server.awards>=2;}); // SDK performs one immediate retry before scheduling backoff.
    now+=1000;c.pump(false);
    for(int i=0;i<20;++i){c.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    const auto before=server.awards.load();require(before>=2 && server.accepted==0,"Outage must retain unsent awards");
    server.award_offline=false;c.retry(); // Must preserve SDK pending submissions.
    for(int i=0;i<10 && !server.accepted;++i){now+=60000;for(int j=0;j<10;++j){c.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
    wait(c,[&]{return server.accepted==1 && c.snapshot().online;});
    require(server.awards>before && c.snapshot().softcore_points==8,"Queued unlock was not sent after recovery");
    events=c.drain_events();require(std::none_of(events.begin(),events.end(),[](const Event& e){return e.kind=="achievement";}),"Retry must not duplicate the unlock popup");
    c.end_game();require(c.snapshot().game_title=="Synthetic game" && c.snapshot().cached_profile,"Last game must remain visible in launcher");
    require(c.game_snapshot(path) && c.game_snapshot(path)->unlocked_count==1,"Stopping emulation must retain the library summary");
    require(!c.game_snapshot(path+".other"),"A different ROM must not inherit the latest game's achievements");
    std::cout<<"GB/GBC/GBA system "<<system<<": real memory, unlock, badges and offline resubmission passed\n";
}
void game_status_and_retry() {
    auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);TestServer server;
    std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
    c.login("GameUser","SyntheticPassword");wait(c,[&]{return !c.snapshot().login_pending;});
    for(int mode:{1,2,3}){
        rom[0x200]=static_cast<std::uint8_t>(mode);
        server.mode=mode;c.start_game(RAVENEMU_GB,rom,core.handle);
        wait(c,[&]{return c.snapshot().game_status!=GameStatus::Loading;});
        const auto expected=mode==1?GameStatus::NoAchievements:mode==2?GameStatus::UnknownRom:GameStatus::Offline;
        require(c.snapshot().game_status==expected,"Unknown ROM, empty set and outage must be distinct (mode " + std::to_string(mode) + ", status " + c.snapshot().status + ")");
        const auto requests=server.lookups.load();now+=4999;c.pump(false);
        require(server.lookups==requests,"Identification retry must back off");
        if(mode!=3){now+=60000;for(int i=0;i<10;++i)c.pump(false);require(server.lookups==requests,"Unknown ROMs and empty sets must not be retried");}
        else {server.mode=0;now+=1;c.pump(false);wait(c,[&]{return c.snapshot().game_status==GameStatus::Ready;});require(server.lookups==requests+1,"Transient lookup must recover automatically");}
        c.end_game();
    }
}
void close_with_pending_awards() {
    for (int scenario : {0,1,2}) {
        auto rom=rom_for(RAVENEMU_GB); Core core(RAVENEMU_GB,rom);TestServer server;
        std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
        c.login("GameUser","SyntheticPassword");wait(c,[&]{return !c.snapshot().login_pending;});
        c.start_game(RAVENEMU_GB,rom,core.handle);wait(c,[&]{return c.snapshot().game_status==GameStatus::Ready;});
        c.pump(true);
        std::vector<std::uint32_t> pixels(ravenemu_video_width(core.handle)*ravenemu_video_height(core.handle));
        require(ravenemu_run_frame(core.handle,pixels.data(),pixels.size()),"Lifecycle ROM frame failed");
        server.award_offline=scenario==2;c.pump(true);
        if(scenario==0) {
            c.end_game(); // response has not been dispatched to the SDK yet
            wait(c,[&]{return c.snapshot().softcore_points==8;});
            require(c.snapshot().cached_profile,"Late award must not discard the remembered game");
        } else if (scenario==1) {
            c.disconnect(); // response ready/in flight while the SDK client is retired
            require(!c.snapshot().logged_in,"Pending award must not undo logout");
        } else {
            wait(c,[&]{return !c.snapshot().online;});
            c.disconnect(); // SDK has a retry scheduled without an active transport future
            require(!c.snapshot().logged_in,"Scheduled submission must be cancelled on logout");
        }
    }
    std::cout<<"Closing games and signing out with active/scheduled award submissions passed\n";
}
// rcheevos safety fix 1 (cmake/rcheevos_safety.cmake): the SDK raises a
// pending mastery only on the next frame. Closing the game before that frame
// raised it after clearing client->game, and the unpatched SDK crashed there.
void mastery_pending_when_closing() {
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);TestServer server;
    std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
    c.login("GameUser","SyntheticPassword");wait(c,[&]{return c.snapshot().logged_in;});
    c.start_game(RAVENEMU_GB,rom,core.handle);wait(c,[&]{return c.snapshot().game_status==GameStatus::Ready;});
    c.pump(true);
    std::vector<std::uint32_t> pixels(ravenemu_video_width(core.handle)*ravenemu_video_height(core.handle));
    require(ravenemu_run_frame(core.handle,pixels.data(),pixels.size()),"Mastery ROM frame failed");
    c.pump(true); // the unlock sends its award; the reply says no achievement remains
    wait(c,[&]{return server.accepted==1;});
    for(int i=0;i<50;++i){c.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));} // reply handled, no frame since
    c.drain_events();
    c.end_game();
    const auto events=c.drain_events();
    require(std::any_of(events.begin(),events.end(),[](const auto& e){return e.title=="Tous les succès obtenus !";}),
        "The pending mastery must be raised for the game being closed");
    std::cout<<"A mastery still pending when the game closes is raised for that game\n";
}
// rcheevos safety fix 2 (cmake/rcheevos_safety.cmake): an award reply saying
// no achievement remains may arrive after its game was closed, or after
// another ROM was opened. It must neither crash on the missing game nor mark
// the other game as mastered.
void late_mastery_reply() {
    for (bool other_rom : {true, false}) {
        auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);TestServer server;
        std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
        c.login("GameUser","SyntheticPassword");wait(c,[&]{return c.snapshot().logged_in;});
        c.start_game(RAVENEMU_GB,rom,core.handle);wait(c,[&]{return c.snapshot().game_status==GameStatus::Ready;});
        c.pump(true);
        std::vector<std::uint32_t> pixels(ravenemu_video_width(core.handle)*ravenemu_video_height(core.handle));
        require(ravenemu_run_frame(core.handle,pixels.data(),pixels.size()),"Late reply ROM frame failed");
        c.pump(true); // the unlock sends its award; its reply is not handled yet
        if (other_rom) {
            auto other=rom;other[0x150]^=0xff; // same set on the server, another hash
            Core next(RAVENEMU_GB,other);
            c.start_game(RAVENEMU_GB,other,next.handle);
            wait(c,[&]{return server.accepted==1&&c.snapshot().game_status==GameStatus::Ready;});
            for(int i=0;i<50;++i){c.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            c.drain_events();
            c.pump(true);
            const auto events=c.drain_events();
            require(std::none_of(events.begin(),events.end(),[](const auto& e){return e.title=="Tous les succès obtenus !";}),
                "A late reply must not master the game opened since");
            c.end_game();
        } else {
            c.end_game();
            wait(c,[&]{return server.accepted==1;});
            for(int i=0;i<50;++i){c.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        }
    }
    std::cout<<"A late reply saying no achievement remains neither crashes nor masters another game\n";
}
int main(){for(int system:{RAVENEMU_GB,RAVENEMU_GBC,RAVENEMU_GBA})unlock_on_real_core(system);game_status_and_retry();close_with_pending_awards();mastery_pending_when_closing();late_mastery_reply();}

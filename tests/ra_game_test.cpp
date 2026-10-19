// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemu/bridge.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ravenemudsk_ra;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
template<class Predicate> void wait(Client& c, Predicate done) {
    for(int i=0;i<2000;++i){c.pump(false);if(done())return;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    throw std::runtime_error("SDK operation timed out: "+c.snapshot().status);
}
std::vector<std::uint8_t> rom_for(int system) {
    if(system!=RAVENEMU_GBA){
        std::vector<std::uint8_t> rom(0x8000);rom[0x143]=system==RAVENEMU_GBC?0xc0:0;
        const std::uint8_t program[]={0x3e,0x5a,0xea,0x00,0xc0,0x18,0xfe};
        std::copy(std::begin(program),std::end(program),rom.begin()+0x100);return rom;
    }
    std::vector<std::uint8_t> rom(0x200);rom[0xb2]=0x96;
    const std::uint32_t program[]={0xe59f0008,0xe3a0105a,0xe5c01000,0xeafffffe,0x02000000};
    for(std::size_t i=0;i<5;++i)for(unsigned b=0;b<4;++b)rom[i*4+b]=program[i]>>(b*8);
    return rom;
}
struct TestServer {
    std::atomic<int> mode{0},lookups{0},awards{0},accepted{0}; // 0=ready, 1=no achievements, 2=unknown, 3=offline
    std::atomic<bool> award_offline{false};
    int console=4;
    ServerTransport transport(){return [this](const std::string&,const std::string& post,const std::string&)->ServerReply {
        if(post.starts_with("r=login"))return {R"({"Success":true,"User":"GameUser","Token":"SyntheticToken","Score":12,"SoftcoreScore":3,"Messages":0})",200};
        if(post.starts_with("r=achievementsets")){
            ++lookups;
            if(mode==2)return {R"({"Success":false,"Error":"Unknown game","Code":"not_found","Status":404})",404};
            if(mode==3)return {"",-2};
            const auto condition=console==5?"0xH008000=90":"0xH00c000=90";
            const std::string a=mode==1?"":std::string(R"({"ID":123,"Title":"Synthetic unlock","Description":"Written by our test ROM","Flags":3,"Points":5,"MemAddr":")")+condition+R"(","Author":"Test","BadgeName":"12345","Created":1367266583,"Modified":1376929305})";
            return {std::string(R"({"Success":true,"GameId":1234,"Title":"Synthetic game","ConsoleId":)")+std::to_string(console)+
                R"(,"ImageIconUrl":"https://media.retroachievements.org/Images/1.png","RichPresenceGameId":1234,"RichPresencePatch":"Display:\nSynthetic test","Sets":[{"AchievementSetId":1111,"GameId":1234,"Title":null,"Type":"core","ImageIconUrl":"https://media.retroachievements.org/Images/1.png","Achievements":[)"+a+R"(],"Leaderboards":[]}]})",200};
        }
        if(post.starts_with("r=startsession"))return {R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[]})",200};
        if(post.starts_with("r=awardachievement")){
            ++awards;require(post.find("&h=0")!=std::string::npos,"Public client must stay in Softcore");
            if(award_offline)return {"",-2};
            ++accepted;return {R"({"Success":true,"Score":12,"SoftcoreScore":8,"AchievementID":123,"AchievementsRemaining":0})",200};
        }
        return {R"({"Success":true})",200};
    };}
};
struct Core {
    ravenemu_bridge* handle;
    Core(int system,const std::vector<std::uint8_t>& rom):handle(ravenemu_create(system)){require(handle && ravenemu_load(handle,rom.data(),rom.size(),nullptr,0),"Synthetic core load failed");}
    ~Core(){ravenemu_destroy(handle);}
};
void unlock_on_real_core(int system) {
    const auto rom=rom_for(system);Core core(system,rom);TestServer server;server.console=system==RAVENEMU_GBA?5:system==RAVENEMU_GBC?6:4;
    std::int64_t now=1000;Client c(nullptr,server.transport(),{},[&]{return now;});
    c.login("GameUser","SyntheticPassword");wait(c,[&]{return !c.snapshot().login_pending;});
    require(c.snapshot().logged_in,"Synthetic login failed");
    c.set_hardcore(true);require(!c.hardcore(),"Unvalidated Hardcore must not be enabled through the API");
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
int main(){for(int system:{RAVENEMU_GB,RAVENEMU_GBC,RAVENEMU_GBA})unlock_on_real_core(system);game_status_and_retry();close_with_pending_awards();}

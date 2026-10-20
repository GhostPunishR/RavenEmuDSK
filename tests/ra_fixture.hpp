// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
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
        if(post.starts_with("r=unlocks"))return {R"({"Success":true,"UserUnlocks":[]})",200};
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

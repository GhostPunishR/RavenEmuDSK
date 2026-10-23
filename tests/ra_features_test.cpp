// SPDX-License-Identifier: AGPL-3.0-only
#include "ra_fixture.hpp"
#include "retro_test_storage.hpp"
#include <ravenemudsk/saves/game_state.hpp>
#include <fstream>
#include <mutex>
namespace ui=ravenemudsk_ui;
struct FeatureServer : TestServer {
    std::mutex mutex;
    std::vector<std::string> posts;
    std::string token="SyntheticToken",condition;
    bool boards=false;
    std::atomic<bool> remote_unlock{false},ranking_offline{false},expire_once{false};
    std::atomic<bool> session_omits_unlocks{false},unlock_fetch_offline{false},remote_hardcore{false};
    std::atomic<int> expected_hardcore{0},scores{0};
    // 1: 401 invalid_credentials, 2: 403 access_denied, 3: Success:false without code, 4: 503 outage.
    std::atomic<int> session_refusal{0};
    std::atomic<bool> award_forbidden{false},token_login_refused{false},award_rejected{false};
    ServerTransport transport(){auto basic=TestServer::transport();return [this,basic](const auto& url,const std::string& post,const auto& type)->ServerReply{
        std::lock_guard guard(mutex);posts.push_back(post);
        if(post.starts_with("r=login")&&post.find("&t=")!=std::string::npos&&token_login_refused)
            return {R"({"Success":false,"Error":"Invalid user/token combination.","Code":"invalid_credentials","Status":401})",401};
        if(post.starts_with("r=login"))return {std::string(R"({"Success":true,"User":"GameUser","Token":")")+token+
            R"(","Score":12,"SoftcoreScore":)"+(remote_unlock||accepted?"8":"3")+R"(,"Messages":0})",200};
        if(post.starts_with("r=achievementsets")){
            auto reply=basic(url,post,type);
            if(!condition.empty()){
                const std::string old=console==5?"0xH008000=90":"0xH00c000=90";
                if(const auto at=reply.body.find(old);at!=std::string::npos)reply.body.replace(at,old.size(),condition);
            }
            if(boards){
                const std::string old=R"("Leaderboards":[])";
                const std::string address=console==5?"0xH008000":"0xH00c000";
                const std::string board=std::string(R"("Leaderboards":[{"ID":4401,"Title":"Synthetic ranking","Description":"Test high score","Mem":"STA:)")+
                    address+"=90::CAN:0=1::SUB:1=1::VAL:"+address+R"(","Format":"SCORE"}])";
                if(const auto at=reply.body.find(old);at!=std::string::npos)reply.body.replace(at,old.size(),board);
            }
            return reply;
        }
        if(post.starts_with("r=unlocks")){
            if(unlock_fetch_offline)return {"",-2};
            const bool hard=post.find("&h=1")!=std::string::npos;
            const bool obtained=remote_unlock||accepted;
            return {obtained&&(!hard||remote_hardcore||(accepted&&expected_hardcore))?
                R"({"Success":true,"UserUnlocks":[123]})":R"({"Success":true,"UserUnlocks":[]})",200};
        }
        if(post.starts_with("r=startsession")&&session_refusal){
            switch(session_refusal.load()){
            case 1:return {R"({"Success":false,"Error":"Invalid user/token combination.","Code":"invalid_credentials","Status":401})",401};
            case 2:return {R"({"Success":false,"Error":"Access denied.","Code":"access_denied","Status":403})",403};
            case 3:return {R"({"Success":false,"Error":"Game session refused."})",200};
            default:return {R"({"Success":false,"Error":"Service unavailable."})",503};
            }
        }
        if(post.starts_with("r=startsession"))return {!session_omits_unlocks&&(remote_unlock||accepted)?
            (expected_hardcore?R"({"Success":true,"HardcoreUnlocks":[{"ID":123,"When":1700000000}]})":
            R"({"Success":true,"Unlocks":[{"ID":123,"When":1700000000}],"HardcoreUnlocks":[]})"):
            R"({"Success":true,"Unlocks":[],"HardcoreUnlocks":[]})",200};
        if(post.starts_with("r=awardachievement")){
            ++awards;require(post.find(expected_hardcore?"&h=1":"&h=0")!=std::string::npos,"Submission mode mismatch");
            if(expire_once.exchange(false)){
                token="RenewedSyntheticToken";return {R"({"Success":false,"Error":"Session expired"})",401};
            }
            if(award_forbidden)return {R"({"Success":false,"Error":"Access denied.","Code":"access_denied","Status":403})",403};
            if(award_rejected)return {R"({"Success":false,"Error":"Achievement definition has changed."})",200};
            if(award_offline)return {"",-2};
            ++accepted;return {R"({"Success":true,"Score":12,"SoftcoreScore":8,"AchievementID":123,"AchievementsRemaining":0})",200};
        }
        if(post.starts_with("r=submitlbentry")){
            ++scores;if(award_offline)return {"",-2};
            return {R"({"Success":true,"Response":{"Score":90,"BestScore":90,"TopEntries":[{"User":"GameUser","Score":90,"Rank":2}],"RankInfo":{"Rank":2,"NumEntries":10}}})",200};
        }
        if(post.starts_with("r=lbinfo")){
            if(ranking_offline)return {"",-2};
            return {R"({"Success":true,"LeaderboardData":{"LBID":4401,"GameID":1234,"LowerIsBetter":0,"LBTitle":"Synthetic ranking","LBDesc":"Test high score","LBFormat":"SCORE","LBMem":"STA:0=1::CAN:0=1::SUB:1=1::VAL:0","LBAuthor":null,"LBCreated":"2013-10-20 22:12:21","LBUpdated":"2021-06-14 08:18:19","TotalEntries":10,"Entries":[{"User":"OtherPlayer","Score":100,"Rank":1,"Index":1,"DateSubmitted":1700000000},{"User":"gameuser","Score":90,"Rank":2,"Index":2,"DateSubmitted":1700000001}]}})",200};
        }
        return basic(url,post,type);
    };}
    std::vector<std::string> requests(){std::lock_guard guard(mutex);return posts;}
};
void frame(Core& core){
    std::vector<std::uint32_t> pixels(ravenemu_video_width(core.handle)*ravenemu_video_height(core.handle));
    require(ravenemu_run_frame(core.handle,pixels.data(),pixels.size()),"ROM frame failed");
}
std::uint8_t memory(Core& core,int system=RAVENEMU_GB){
    std::uint8_t value=0;require(ravenemu_read_achievement_memory(core.handle,system==RAVENEMU_GBA?0x8000:0xc000,&value,1)==1,"Memory read failed");return value;
}
std::size_t count(const std::vector<std::string>& posts,const char* operation){
    return static_cast<std::size_t>(std::count_if(posts.begin(),posts.end(),[&](const auto& p){return p.starts_with(operation);}));
}
void login(Client& client){client.login("GameUser","SyntheticPassword");wait(client,[&]{return client.snapshot().logged_in;});}
void start(Client& client,Core& core,const std::vector<std::uint8_t>& rom,int system=RAVENEMU_GB,const std::string& path={}){
    client.start_game(system,rom,core.handle,path);wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
}
void restart_with_pending_award(){
    retro_test::Directory root;retro_test::Vaults keys;auto credentials=std::make_shared<retro_test::Vault>();
    auto queue=std::make_shared<Outbox>(root.path/"pending",keys.factory());
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);FeatureServer offline;offline.award_offline=true;
    const auto path=(root.path/"Game.gb").generic_string();
    {
        Client client(credentials,offline.transport(),{}, {},queue);login(client);start(client,core,rom,RAVENEMU_GB,path);
        client.pump(true);frame(core);client.pump(true);wait(client,[&]{return offline.awards>=2;});
        require(client.snapshot().pending_submissions==1&&queue->entries("GameUser").size()==1,"Offline award was not journaled before transmission");
    }
    auto entry=queue->entries("GameUser").at(0);entry.earned_at-=600;
    require(queue->erase("GameUser",entry.id)&&!queue->put(entry).empty(),"Could not age the synthetic submission");
    queue=std::make_shared<Outbox>(root.path/"pending",keys.factory());
    FeatureServer restored;restored.token="RotatedSyntheticToken";
    {
        Client client(credentials,restored.transport(),{}, {},queue);
        require(!client.snapshot().logged_in&&client.snapshot().pending_submissions==1&&client.snapshot().durable_submissions,
            "Saved outbox must be visible before online authentication succeeds");
        client.start_game(RAVENEMU_GB,rom,core.handle,path);
        wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready&&client.snapshot().pending_submissions==0;});
        require(restored.accepted==1&&client.snapshot().softcore_points==8,"Restart did not submit the durable award");
        require(client.snapshot().unlocked_count==1,"Game was loaded before replaying its pending award");
        const auto posts=restored.requests();std::size_t award=posts.size(),lookup=posts.size();
        for(std::size_t i=0;i<posts.size();++i){
            if(posts[i].starts_with("r=awardachievement")){
                award=i;require(posts[i].find("&t=RotatedSyntheticToken")!=std::string::npos&&posts[i].find("&o=")!=std::string::npos,
                    "Replay must use the current token and original unlock timestamp");
            }
            if(posts[i].starts_with("r=achievementsets"))lookup=i;
        }
        require(award<lookup,"Replay must precede loading unlocks from the service");
        const auto events=client.drain_events();require(std::none_of(events.begin(),events.end(),[](const auto& e){return e.kind=="achievement";}),"Replay duplicated the unlock notification");
        client.disconnect();require(queue->entries("GameUser").empty()&&!credentials->value,"Logout did not forget the account");
    }
    std::cout<<"Durable offline award survives destruction, rotates tokens and refreshes remote unlocks without duplicate notification\n";
}
void softcore_states(){
    FeatureServer server;server.condition="M:0xH00c000=90.5.";
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);Client client(nullptr,server.transport());login(client);start(client,core,rom);
    client.pump(true);frame(core);client.pump(true);client.pump(true);
    require(client.snapshot().unlocked_count==0,"Measured achievement triggered prematurely");
    const auto state=ui::capture_game_state(core.handle,client);require(!state.achievements.empty(),"Softcore state omitted achievement hit counts");
    const auto encoded=ui::encode_game_state(state);
    {
        FeatureServer delayed;delayed.condition=server.condition;Core second(RAVENEMU_GB,rom);Client reconnecting(nullptr,delayed.transport());
        reconnecting.login("GameUser","SyntheticPassword");reconnecting.start_game(RAVENEMU_GB,rom,second.handle);
        require(ui::restore_game_state(second.handle,reconnecting,encoded),"Condition counters were dropped during authentication");
        wait(reconnecting,[&]{return reconnecting.snapshot().game_status==GameStatus::Ready;});
        reconnecting.pump(true);reconnecting.pump(true);require(reconnecting.snapshot().unlocked_count==0,"Deferred restore changed its hit count");
        reconnecting.pump(true);require(reconnecting.snapshot().unlocked_count==1,"Deferred progress was not applied once authentication/game loading completed");
    }
    auto corrupt=encoded;corrupt.back()^=1;bool rejected=false;
    try{ui::restore_game_state(core.handle,client,corrupt);}catch(const std::exception&){rejected=true;}
    require(rejected,"Corrupt combined state accepted");
    client.reset();require(ravenemu_reset(core.handle),"Core reset failed");
    require(ui::restore_game_state(core.handle,client,encoded)&&memory(core)==90,"Core/achievement state not restored together");
    client.pump(true);client.pump(true);require(client.snapshot().unlocked_count==0,"Restored hit count exceeded its saved value");
    client.pump(true);require(client.snapshot().unlocked_count==1,"Restored hit count was lost; achievement did not unlock on fifth hit");
    wait(client,[&]{return server.accepted==1;});
    auto other=state.achievements;const std::string name="gameuser";
    const auto at=std::search(other.begin(),other.end(),name.begin(),name.end());require(at!=other.end(),"State account missing");*at='x';
    require(!client.load_progress(other),"Another account's condition counters were restored");
    other=state.achievements;const auto hash=client.snapshot().game_hash;
    const auto hash_at=std::search(other.begin(),other.end(),hash.begin(),hash.end());require(hash_at!=other.end(),"State game hash missing");*hash_at=(*hash_at=='0'?'1':'0');
    require(!client.load_progress(other),"Another ROM's condition counters were restored");
    const auto legacy=ui::decode_game_state(state.core);require(legacy.core==state.core&&legacy.achievements.empty(),"Legacy states lost compatibility");
    require(!ui::restore_game_state(core.handle,client,state.core),"Legacy state must reset achievement conditions");
    for(auto incomplete:{std::vector<std::uint8_t>{},std::vector<std::uint8_t>(encoded.begin(),encoded.begin()+20),std::vector<std::uint8_t>(encoded.begin(),encoded.end()-1)}){
        rejected=false;try{ui::decode_game_state(incomplete);}catch(const std::exception&){rejected=true;}require(rejected,"Truncated state accepted");
    }
    std::cout<<"Softcore restores actual SDK hit counts, rejects corruption/account/ROM mismatch and supports legacy core states\n";
}
void restart_with_pending_score(){
    retro_test::Directory root;retro_test::Vaults keys;auto credentials=std::make_shared<retro_test::Vault>();
    auto queue=std::make_shared<Outbox>(root.path,keys.factory());
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
    FeatureServer offline;offline.award_offline=true;offline.boards=true;offline.condition="0=1";offline.expected_hardcore=1;
    {
        Client client(credentials,offline.transport(),{}, {},queue);login(client);start(client,core,rom);
        require(client.set_hardcore(true),"Hardcore score fixture not enabled");
        client.pump(true);frame(core);client.pump(true);wait(client,[&]{return offline.scores>=2;});
        const auto entries=queue->entries("GameUser");
        require(entries.size()==1&&entries[0].leaderboard&&entries[0].score==90,"Offline score not journaled with its value");
    }
    queue=std::make_shared<Outbox>(root.path,keys.factory());FeatureServer restored;restored.token="ScoreRecoveryToken";
    Client client(credentials,restored.transport(),{}, {},queue);
    wait(client,[&]{return client.snapshot().logged_in&&restored.scores==1&&client.snapshot().pending_submissions==0;});
    const auto posts=restored.requests();
    require(std::any_of(posts.begin(),posts.end(),[](const auto& p){return p.starts_with("r=submitlbentry")&&p.find("&s=90")!=std::string::npos&&p.find("&t=ScoreRecoveryToken")!=std::string::npos;}),
        "Pending leaderboard score did not survive restart with the current token");
    std::cout<<"Durable leaderboard score survives restart independently of a running game\n";
}
void hardcore_transitions(int system){
    retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
    FeatureServer server;server.expected_hardcore=1;server.boards=true;server.console=system==RAVENEMU_GBA?5:system==RAVENEMU_GBC?6:4;
    const auto rom=rom_for(system);Core core(system,rom);Client client(nullptr,server.transport(),{}, {},queue);login(client);start(client,core,rom,system);
    const auto state=ui::encode_game_state(ui::capture_game_state(core.handle,client));
    frame(core);require(memory(core,system)==90,"Softcore core did not advance");
    require(client.set_hardcore(true)&&client.hardcore(),"Hardcore did not activate with a protected journal");
    require(memory(core,system)==0,"Softcore to Hardcore did not reset console memory synchronously");
    require(!client.can_load_state()&&client.save_progress().empty()&&!client.load_progress({1,2,3}),"Hardcore allowed condition state manipulation");
    client.pump(true);frame(core);require(memory(core,system)==90,"Hardcore ROM did not restart");
    bool blocked=false;try{ui::restore_game_state(core.handle,client,state);}catch(const std::exception&){blocked=true;}
    require(blocked&&memory(core,system)==90,"Hardcore state load changed live core memory");
    require(!ui::capture_game_state(core.handle,client).core.empty(),"Hardcore debug state saving must remain available");
    client.pump(true);wait(client,[&]{return server.accepted==1&&server.scores==1&&client.snapshot().pending_submissions==0;});
    const auto events=client.drain_events();
    require(std::count_if(events.begin(),events.end(),[](const auto& e){return e.kind=="hardcore_reset";})==1,"Hardcore reset event missing or duplicated");
    require(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind=="leaderboard_scoreboard"&&e.detail.find("Rang : 2 / 10")!=std::string::npos;}),"Leaderboard personal result/rank was discarded");
    require(client.snapshot().rich_presence=="Synthetic test","Hardcore activity was disabled");
    require(client.set_hardcore(false)&&client.can_load_state()&&memory(core,system)==90,"Hardcore to Softcore must preserve the running console");
    require(ui::restore_game_state(core.handle,client,state)&&memory(core,system)==0,"Softcore state loading did not become available again");
    std::cout<<"System "<<system<<": Hardcore reset, guarded state loading, debug saving, real unlock, ranking submission and return to Softcore passed\n";
}
void launcher_refresh_rankings_activity(){
    retro_test::Directory root;FeatureServer server;server.boards=true;auto credentials=std::make_shared<retro_test::Vault>();
    const auto rom=rom_for(RAVENEMU_GB);const auto file=root.path/"Pokemon - Test.gb";
    {std::ofstream out(file,std::ios::binary);out.write(reinterpret_cast<const char*>(rom.data()),rom.size());}
    auto profiles=make_profile_store(root.path/"profiles");
    Client client(credentials,server.transport(),profiles);login(client);
    server.remote_unlock=true;require(client.refresh_game(file.generic_string()),"Launcher metadata refresh refused");
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready&&!client.snapshot().login_pending;});
    require(client.snapshot().unlocked_count==1&&client.snapshot().softcore_points==8,"Launcher refresh did not retrieve remote unlocks/profile points");
    require(client.snapshot().achievements.size()==1&&client.snapshot().achievements[0].unlocked&&
        client.snapshot().achievements[0].badge_url.find("_lock")==std::string::npos,
        "Already-earned achievement must remain marked as unlocked after a launcher-only refresh");
    require(client.snapshot().leaderboards.size()==1&&client.snapshot().leaderboards[0].id==4401,"Leaderboard list missing from game metadata");
    {const auto posts=server.requests();
    require(count(posts,"r=startsession")==0&&count(posts,"r=ping")==0&&count(posts,"r=unlocks")==2,
        "A launcher refresh must read the account's unlocks without opening a game session");}
    require(client.snapshot().achievements[0].unlock_time==0,"An unlock read without a session has no known date");
    client.fetch_ranking(4401);wait(client,[&]{return !client.ranking().loading;});
    auto ranking=client.ranking();require(ranking.error.empty()&&ranking.total==10&&ranking.entries.size()==2,"Leaderboard records failed to load");
    require(ranking.entries[1].current_user&&ranking.entries[1].rank==2&&!ranking.entries[1].score.empty(),"Personal score/rank not highlighted case insensitively");
    client.fetch_ranking(4401,true);wait(client,[&]{return !client.ranking().loading;});
    const auto posts=server.requests();require(std::any_of(posts.begin(),posts.end(),[](const auto& p){return p.starts_with("r=lbinfo")&&p.find("&u=GameUser")!=std::string::npos;}),"Around-user ranking did not use SDK's account query");
    server.ranking_offline=true;client.fetch_ranking(4401);wait(client,[&]{return !client.ranking().loading;});
    require(!client.ranking().error.empty()&&client.ranking().entries.empty(),"Offline ranking displayed stale rows as a current result");
    server.ranking_offline=false;client.fetch_ranking(4401);client.end_game();
    for(int i=0;i<10;++i){client.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    require(client.ranking().id==0,"Late ranking callback revived a closed game's leaderboard");
    Core core(RAVENEMU_GB,rom);start(client,core,rom,RAVENEMU_GB,file.generic_string());client.pump(true);frame(core);client.pump(true);
    require(client.snapshot().rich_presence=="Synthetic test","Evaluated Rich Presence missing");
    require(client.snapshot().achievements[0].unlock_time==1700000000,"A game session's unlock date was lost");
    require(!client.refresh_game(file.generic_string()),"Launcher refresh replaced a live runtime");
    client.end_game();
    const auto sessions=count(server.requests(),"r=startsession");
    require(client.refresh_game(file.generic_string()),"Second launcher refresh refused");
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready&&!client.snapshot().login_pending;});
    require(count(server.requests(),"r=startsession")==sessions&&client.snapshot().achievements[0].unlock_time==1700000000,
        "A launcher refresh must keep the known unlock date without opening a session");
    client.end_game();Snapshot cached;
    require(profiles->load_game("GameUser",rom_identity(file),cached)&&cached.leaderboards.size()==1&&cached.rich_presence=="Synthetic test"&&!cached.logged_in,
        "Public metadata cache lost rankings/activity or granted authentication");
    client.drain_events();
    client.set_hardcore(true);require(!client.hardcore()&&!client.snapshot().hardcore_preferred,"Hardcore must explain/refuse unavailable durable protection");
    const auto refusal=client.drain_events();
    require(std::any_of(refusal.begin(),refusal.end(),[](const auto& e){return e.kind=="hardcore_refused";}),
        "A refused Hardcore preference must be reported so the setting follows");
    std::cout<<"Launcher-only refresh, rankings/top/around-user/error/cancellation, Rich Presence and public metadata persistence passed\n";
}
void hardcore_during_loading(){
    for(bool before_login:{false,true}){
        retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
        FeatureServer server;const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
        Client client(nullptr,server.transport(),{}, {},queue);
        if(!before_login)login(client);
        else{client.set_hardcore(true);require(!client.can_load_state()&&!client.hardcore()&&client.snapshot().hardcore_preferred,
            "Hardcore preference must block state loading before authentication");}
        client.start_game(RAVENEMU_GB,rom,core.handle);frame(core);require(memory(core)==90,"Loading test fixture did not advance");
        if(!before_login){require(client.set_hardcore(true),"Hardcore failed while SDK game load was pending");require(memory(core)==0,"Loading runtime bypassed the Hardcore full reset");}
        else login(client);
        wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
        require(client.hardcore()&&memory(core)==0&&!client.can_load_state(),"Delayed authentication bypassed Hardcore reset/guards");
        require(client.snapshot().game_hardcore,"Snapshot lost the mode of its achievement counts");
    }
    std::cout<<"Hardcore transitions remain guarded while metadata/authentication is still pending\n";
}
// The game loop's Hardcore gates: frames wait until the session can evaluate
// them, and pauses follow the SDK (refused right after one, with the frames
// left to play, allowed again once played). Softcore never waits nor throttles.
void hardcore_game_loop_gates(){
    retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
    FeatureServer server;server.expected_hardcore=1;const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
    Client client(nullptr,server.transport(),{}, {},queue);
    require(!client.frames_on_hold()&&client.may_pause(),"Softcore without an account neither waits nor throttles");
    client.set_hardcore(true);
    require(client.frames_on_hold(),"Hardcore without an account must hold frames");
    client.set_hardcore(false);
    require(!client.frames_on_hold(),"Back to Softcore while waiting, the game runs");
    client.set_hardcore(true);
    login(client);
    require(client.frames_on_hold(),"Hardcore without a game session must hold frames");
    client.start_game(RAVENEMU_GB,rom,core.handle);
    require(client.frames_on_hold(),"Hardcore must hold frames while the game loads");
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
    require(!client.frames_on_hold()&&client.hardcore(),"An identified Hardcore game runs");
    std::uint32_t left=99;
    require(client.may_pause(&left)&&left==0,"The first Hardcore pause is allowed");
    frame(core);client.pump(true);
    require(!client.may_pause(&left)&&left>0,"A pause right after another is refused, with the frames still to play");
    for(std::uint32_t i=0;i<left;++i){frame(core);client.pump(true);}
    require(client.may_pause(),"Once those frames are played, the next pause is allowed");
    require(client.set_hardcore(false)&&!client.frames_on_hold()&&client.may_pause(),"Back in Softcore, nothing waits or throttles");
    std::cout<<"Hardcore holds frames until the game session is ready and throttles pauses as the SDK requires\n";
}
// A Hardcore award that cannot be written to the protected journal stops
// Hardcore: the game goes on in Softcore and says why.
void hardcore_journal_write_failure(){
    retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
    FeatureServer server;server.expected_hardcore=1;const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
    Client client(nullptr,server.transport(),{}, {},queue);login(client);start(client,core,rom);
    require(client.set_hardcore(true)&&client.hardcore(),"Hardcore did not start with a protected journal");
    client.drain_events();
    // The journal's folder becomes a file: the next write fails.
    std::filesystem::remove_all(root.path);std::ofstream(root.path)<<"not a folder";
    client.pump(true);frame(core);client.pump(true);
    wait(client,[&]{return !client.hardcore();});
    require(!client.snapshot().hardcore_preferred&&client.can_load_state(),"A failed journal write must leave Hardcore");
    const auto events=client.drain_events();
    require(std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind=="hardcore_refused";})&&
            std::any_of(events.begin(),events.end(),[](const auto& e){return e.kind=="error"&&e.title=="Hardcore interrompu";}),
            "Leaving Hardcore after a failed write must be reported");
    std::cout<<"A Hardcore award the journal cannot keep switches the game to Softcore and says so\n";
}
void expired_submission_session(){
    for(bool hardcore:{false,true}){
        retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
        FeatureServer server;server.expire_once=true;server.expected_hardcore=hardcore?1:0;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);Client client(nullptr,server.transport(),{}, {},queue);login(client);start(client,core,rom);
        if(hardcore)require(client.set_hardcore(true),"Could not enter Hardcore for renewal test");
        client.pump(true);frame(core);client.pump(true);
        wait(client,[&]{return server.accepted==1&&client.snapshot().pending_submissions==0&&client.snapshot().game_status==GameStatus::Ready;});
        require(client.snapshot().logged_in&&client.snapshot().unlocked_count==1,"Expired submission did not renew its token and retain the award");
        const auto posts=server.requests();require(std::count_if(posts.begin(),posts.end(),[](const auto& p){return p.starts_with("r=login");})==2,"Unauthorized submission was not reauthenticated exactly once");
        require(std::any_of(posts.begin(),posts.end(),[](const auto& p){return p.starts_with("r=awardachievement")&&p.find("&t=RenewedSyntheticToken")!=std::string::npos;}),"Renewal did not submit with the new token");
        if(hardcore)require(client.hardcore()&&memory(core)==0,"Hardcore resumed after reauthentication without a full reset");
    }
    std::cout<<"Expired award token is renewed without losing the submission; Hardcore resets before resuming\n";
}
void previously_earned_achievements(){
    for(bool hard_earned:{false,true}){
        retro_test::Directory root;FeatureServer server;server.remote_unlock=true;
        server.remote_hardcore=hard_earned;server.session_omits_unlocks=true;
        auto profiles=make_profile_store(root.path/"profiles");
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
        Client client(nullptr,server.transport(),profiles);login(client);
        const auto path=(root.path/"Previously played.gb").generic_string();
        start(client,core,rom,RAVENEMU_GB,path);
        require(client.snapshot().unlocked_count==1&&client.snapshot().achievements[0].unlocked,
            "Dedicated account unlocks were not applied before gameplay");
        client.drain_events();client.pump(true);frame(core);for(int i=0;i<10;++i)client.pump(true);
        require(server.awards==0,"A previously earned achievement was submitted again for zero points");
        const auto events=client.drain_events();require(std::none_of(events.begin(),events.end(),[](const auto& e){return e.kind=="achievement";}),
            "Previously earned achievement produced a fresh unlock notification");
        client.end_game();Snapshot cached;require(profiles->load_game("GameUser",rom_identity(path),cached)&&cached.achievements[0].unlocked,
            "Account unlock status was lost in the launcher cache");
        start(client,core,rom,RAVENEMU_GB,path);require(client.snapshot().achievements[0].unlocked,"Reopening a game discarded account progress");
    }
    // If the empty history could not be verified, do not activate a false-zero runtime.
    FeatureServer offline;offline.remote_unlock=true;offline.session_omits_unlocks=true;offline.unlock_fetch_offline=true;
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);std::int64_t now=1000;
    Client client(nullptr,offline.transport(),{},[&]{return now;});login(client);
    client.start_game(RAVENEMU_GB,rom,core.handle);wait(client,[&]{return client.snapshot().game_status==GameStatus::Offline;});
    client.pump(true);frame(core);client.pump(true);require(offline.awards==0,"Unverified history must not trigger duplicate unlocks during a sync outage");
    offline.unlock_fetch_offline=false;now+=5000;client.pump(false);
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
    require(client.snapshot().achievements[0].unlocked&&offline.awards==0,"Account history did not recover after the outage");
    std::cout<<"Existing Softcore/Hardcore awards load before gameplay, never re-award, persist and recover from a sync outage\n";
}
void refused_replay_does_not_block_games(){
    for(bool started_first:{true,false}){
        retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
        Submission refused;refused.username="GameUser";refused.game_hash=std::string(32,'a');refused.object_id=999;refused.earned_at=1700000000;
        require(!queue->put(refused).empty(),"Could not queue the refused submission");
        FeatureServer server;server.award_forbidden=true;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);Client client(nullptr,server.transport(),{}, {},queue);
        if(started_first){client.start_game(RAVENEMU_GB,rom,core.handle);login(client);}
        else{
            login(client);client.start_game(RAVENEMU_GB,rom,core.handle);
            require(client.snapshot().game_status==GameStatus::Loading&&client.snapshot().status.find("Connectez")==std::string::npos,
                "A signed-in player waiting for queued unlocks was asked to sign in");
        }
        wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
        require(client.snapshot().pending_submissions==1&&queue->entries("GameUser").size()==1,"A refused submission must stay queued");
        const auto posts=server.requests();std::size_t award=posts.size(),lookup=posts.size();
        for(std::size_t i=0;i<posts.size();++i){
            if(posts[i].starts_with("r=awardachievement")&&award==posts.size())award=i;
            if(posts[i].starts_with("r=achievementsets"))lookup=i;
        }
        require(award<lookup,"Queued unlocks must still replay before the game loads");
    }
    std::cout<<"A replay refused by the service stays queued and no longer blocks game identification\n";
}
void refused_game_session_is_not_an_outage(){
    struct Case{int refusal;bool token_refused;GameStatus status;const char* text;std::size_t logins,sessions;bool logged_in;};
    for(const Case scenario:{Case{1,true,GameStatus::None,"Session expirée",2,1,false},
                             Case{1,false,GameStatus::Error,"Session refusée",2,2,true},
                             Case{2,false,GameStatus::Error,"Accès refusé",1,1,true},
                             Case{3,false,GameStatus::Error,"refusé de charger",1,1,true}}){
        FeatureServer server;server.session_refusal=scenario.refusal;server.token_login_refused=scenario.token_refused;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);std::int64_t now=1000;
        Client client(nullptr,server.transport(),{},[&]{return now;});login(client);
        client.start_game(RAVENEMU_GB,rom,core.handle);
        wait(client,[&]{return !client.snapshot().login_pending&&client.snapshot().game_status==scenario.status&&
            client.snapshot().status.find(scenario.text)!=std::string::npos;});
        for(int i=0;i<5;++i){now+=120000;for(int j=0;j<5;++j){client.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
        const auto posts=server.requests();
        require(count(posts,"r=login")==scenario.logins&&count(posts,"r=startsession")==scenario.sessions,
            "A refused game session must not be retried as an outage (refusal "+std::to_string(scenario.refusal)+")");
        require(client.snapshot().logged_in==scenario.logged_in&&client.snapshot().status.find(scenario.text)!=std::string::npos,
            "Refused game session ended in the wrong state: "+client.snapshot().status);
    }
    // A real outage keeps its automatic retry.
    FeatureServer server;server.session_refusal=4;const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);std::int64_t now=1000;
    Client client(nullptr,server.transport(),{},[&]{return now;});login(client);client.start_game(RAVENEMU_GB,rom,core.handle);
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Offline;});
    server.session_refusal=0;now+=5000;client.pump(false);wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
    std::cout<<"Refused game sessions renew once, expire or fail without retry loops; outages still retry\n";
}
// Different bytes, so the SDK's hash cache does not reuse the first game's identity.
std::vector<std::uint8_t> other_rom(){auto rom=rom_for(RAVENEMU_GB);rom[0x150]=1;return rom;}
void previous_game_is_not_shown(){
    retro_test::Directory root;FeatureServer server;auto profiles=make_profile_store(root.path/"profiles");
    const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
    Client client(nullptr,server.transport(),profiles);login(client);
    const auto known=(root.path/"Known.gb").generic_string(),unknown=(root.path/"Unknown.gb").generic_string();
    start(client,core,rom,RAVENEMU_GB,known);client.pump(true);frame(core);client.pump(true);
    require(client.snapshot().game_title=="Synthetic game"&&!client.snapshot().rich_presence.empty(),"Fixture game did not load");
    client.end_game();
    const auto second=other_rom();Core other(RAVENEMU_GB,second);
    server.mode=2;client.start_game(RAVENEMU_GB,second,other.handle,unknown);
    auto shown=client.snapshot();
    require(shown.game_title.empty()&&shown.achievements.empty()&&shown.rich_presence.empty()&&
        shown.leaderboards.empty()&&shown.total_count==0&&!shown.cached_profile,"A loading ROM showed the previous game's data");
    wait(client,[&]{return client.snapshot().game_status==GameStatus::UnknownRom;});
    shown=client.snapshot();
    require(shown.game_title.empty()&&shown.achievements.empty(),"An unknown ROM showed the previous game's data");
    client.end_game();server.mode=0;
    client.start_game(RAVENEMU_GB,rom,core.handle,known);shown=client.snapshot();
    require(shown.game_title=="Synthetic game"&&shown.cached_profile&&shown.achievements.size()==1,
        "A known ROM must show its own cached summary while it loads");
    wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
    std::cout<<"A loading or unknown ROM never shows the previous game; a known ROM shows its own cache\n";
}
void achievement_sync_states(){
    {
        // Without a journal, an unsent unlock stays pending until the SDK's retry is accepted.
        FeatureServer server;server.award_offline=true;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);std::int64_t now=1000;
        Client client(nullptr,server.transport(),{},[&]{return now;});login(client);
        client.start_game(RAVENEMU_GB,rom,core.handle);wait(client,[&]{return client.snapshot().game_status==GameStatus::Ready;});
        require(client.snapshot().achievements[0].sync==AchievementSync::Server,"A locked achievement has nothing to send");
        client.pump(true);frame(core);client.pump(true);
        wait(client,[&]{return server.awards>=1&&client.snapshot().achievements[0].unlocked;});
        require(client.snapshot().achievements[0].sync==AchievementSync::Pending,"An unsent unlock must read as pending");
        server.award_offline=false;
        for(int i=0;i<20&&server.accepted==0;++i){now+=120000;for(int j=0;j<5;++j){client.pump(false);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
        wait(client,[&]{return server.accepted==1&&client.snapshot().achievements[0].sync==AchievementSync::Server;});
    }
    {
        // With a journal, a refused submission leaves the queue and reads as refused.
        retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path,keys.factory());
        FeatureServer server;server.award_rejected=true;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);
        Client client(nullptr,server.transport(),{},{},queue);login(client);start(client,core,rom);
        client.pump(true);frame(core);client.pump(true);
        wait(client,[&]{return server.awards>=1&&client.snapshot().achievements[0].sync==AchievementSync::Refused;});
        require(queue->entries("GameUser").empty(),"A refused submission must leave the journal");
    }
    {
        // A queued unlock is pending, on the live game and on a cached one.
        retro_test::Directory root;retro_test::Vaults keys;auto queue=std::make_shared<Outbox>(root.path/"pending",keys.factory());
        auto profiles=make_profile_store(root.path/"profiles");
        FeatureServer server;server.award_offline=true;
        const auto rom=rom_for(RAVENEMU_GB);Core core(RAVENEMU_GB,rom);const auto path=(root.path/"Game.gb").generic_string();
        Client client(nullptr,server.transport(),profiles,{},queue);login(client);start(client,core,rom,RAVENEMU_GB,path);
        client.pump(true);frame(core);client.pump(true);
        wait(client,[&]{return client.snapshot().pending_submissions==1&&client.snapshot().achievements[0].sync==AchievementSync::Pending;});
        const auto second=other_rom();Core other(RAVENEMU_GB,second);
        client.end_game();server.mode=2;client.start_game(RAVENEMU_GB,second,other.handle,(root.path/"Other.gb").generic_string());
        const auto cached=client.game_snapshot(path);
        require(cached&&cached->achievements.size()==1&&cached->achievements[0].sync==AchievementSync::Pending,
            "A cached game must show its queued unlock as pending");
    }
    std::cout<<"Unlocks read as pending until accepted, refused when rejected, and pending while queued\n";
}
int main(){
    restart_with_pending_award();restart_with_pending_score();softcore_states();
    for(const auto system:{RAVENEMU_GB,RAVENEMU_GBC,RAVENEMU_GBA})hardcore_transitions(system);
    launcher_refresh_rankings_activity();hardcore_during_loading();hardcore_game_loop_gates();hardcore_journal_write_failure();expired_submission_session();previously_earned_achievements();
    refused_replay_does_not_block_games();refused_game_session_is_not_an_outage();
    previous_game_is_not_shown();achievement_sync_states();
}

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/achievements/user_agent.hpp>
#include <ravenemu/bridge.h>
#include <rc_client.h>
#include <rc_api_runtime.h>
#include <rc_api_user.h>
#include <curl/curl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <future>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>
#include <iomanip>
namespace ravenemudsk_ra {
namespace {
using Reply = ServerReply;
size_t write_body(char* bytes, size_t size, size_t count, void* ptr) {
    auto& body = *static_cast<std::string*>(ptr);
    if (size && count > (8 * 1024 * 1024) / size) return 0;
    const size_t n = size * count;
    if (n > 8 * 1024 * 1024 || body.size() > 8 * 1024 * 1024 - n) return 0;
    body.append(bytes, n);
    return n;
}
Reply https_request(std::string url, std::string post, std::string content_type) {
    Reply reply;
    // Never send login tokens to third-party hosts or follow redirects.
    if (url.rfind("https://retroachievements.org/", 0) != 0) { reply.status = -1; return reply; }
    CURL* curl = curl_easy_init();
    if (!curl) return reply;
    curl_slist* headers = nullptr;
    if (!post.empty()) {
        headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post.size()));
    }
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    const std::string agent = user_agent();
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &reply.body);
    const CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        reply.status = static_cast<int>(http_code);
    } else {
        reply.error = curl_easy_strerror(result);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return reply;
}
std::string lower_user(std::string value) {
    for(auto& c:value)if(c>='A'&&c<='Z')c+='a'-'A';
    return value;
}
std::map<std::string,std::string> form(const std::string& post) {
    std::map<std::string,std::string> fields;
    std::size_t start=0;
    while(start<post.size()){
        const auto end=post.find('&',start),equal=post.find('=',start);
        if(equal==std::string::npos || (end!=std::string::npos && equal>end))break;
        const auto encoded=post.substr(equal+1,(end==std::string::npos?post.size():end)-equal-1);
        int size=0;char* decoded=curl_easy_unescape(nullptr,encoded.c_str(),static_cast<int>(encoded.size()),&size);
        if(decoded){fields[post.substr(start,equal-start)]={decoded,static_cast<std::size_t>(size)};curl_free(decoded);}
        if(end==std::string::npos)break;
        start=end+1;
    }
    return fields;
}
template<typename T> T number(const std::string& value,T fallback={}) {
    T result{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    return parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()?result:fallback;
}
std::int64_t wall_time(){return std::chrono::duration_cast<std::chrono::seconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();}
using UnlockIds=std::array<std::vector<std::uint32_t>,2>; // Softcore, then Hardcore.
// The account's unlocks for a game, read from the dedicated endpoint, which
// opens no game session. On failure, `failure` holds the reply to report.
bool account_unlocks(const ServerTransport& send,const std::map<std::string,std::string>& fields,
                     UnlockIds& unlocked,Reply& failure){
    if(!fields.contains("u")||!fields.contains("t")||!fields.contains("g")){failure={"",-2};return false;}
    for(unsigned mode=0;mode<2;++mode){
        rc_api_fetch_user_unlocks_request_t params{};
        params.username=fields.at("u").c_str();params.api_token=fields.at("t").c_str();
        params.game_id=number<std::uint32_t>(fields.at("g"));params.hardcore=mode;
        rc_api_request_t req{};
        const auto built=rc_api_init_fetch_user_unlocks_request(&req,&params);
        if(built!=RC_OK){rc_api_destroy_request(&req);failure={"",-2};return false;}
        const std::string target=req.url,payload=req.post_data,content=req.content_type;
        rc_api_destroy_request(&req);
        const auto account=send(target,payload,content);
        const rc_api_server_response_t account_response{account.body.c_str(),account.body.size(),account.status};
        rc_api_fetch_user_unlocks_response_t parsed{};
        const auto parsed_result=rc_api_process_fetch_user_unlocks_server_response(&parsed,&account_response);
        const bool ok=account.status>=200&&account.status<300&&parsed_result==RC_OK&&parsed.response.succeeded;
        if(ok&&parsed.num_achievement_ids)unlocked[mode].assign(parsed.achievement_ids,parsed.achievement_ids+parsed.num_achievement_ids);
        rc_api_destroy_fetch_user_unlocks_response(&parsed);
        if(!ok){failure=account;return false;}
    }
    return true;
}
// A start-session reply listing these unlocks. Their dates are unknown (0).
std::string session_body(std::int64_t server_now,const UnlockIds& unlocked){
    std::ostringstream body;body<<"{\"Success\":true,\"ServerNow\":"<<server_now;
    for(unsigned mode=0;mode<2;++mode){
        body<<(mode?",\"HardcoreUnlocks\":[":",\"Unlocks\":[");bool comma=false;
        for(const auto id:unlocked[mode]){if(comma)body<<',';comma=true;body<<"{\"ID\":"<<id<<",\"When\":0}";}
        body<<']';
    }
    body<<'}';return body.str();
}
Reply session_reply(const ServerTransport& send,const std::string& url,const std::string& post,const std::string& type){
    auto reply=send(url,post,type);
    if(reply.status<200||reply.status>=300)return reply;
    const rc_api_server_response_t response{reply.body.c_str(),reply.body.size(),reply.status};
    rc_api_start_session_response_t session{};
    const auto result=rc_api_process_start_session_server_response(&session,&response);
    const bool empty=result==RC_OK&&session.response.succeeded&&!session.num_unlocks&&!session.num_hardcore_unlocks;
    const auto server_now=session.server_now;rc_api_destroy_start_session_response(&session);
    if(!empty)return reply;
    // Confirm an empty session against the account's dedicated unlock endpoint
    // before the SDK activates triggers. Only server-reported IDs are applied.
    const auto fields=form(post);
    if(!fields.contains("u")||!fields.contains("t")||!fields.contains("g"))return reply;
    UnlockIds unlocked;Reply failure;
    // Never turn an unverified outage into an empty account history.
    if(!account_unlocks(send,fields,unlocked,failure))return {"",-2};
    if(unlocked[0].empty()&&unlocked[1].empty())return reply;
    return {session_body(server_now,unlocked),reply.status};
}
// A launcher refresh only reads the account's unlocks: opening a game session
// would list the game as being played, in Hardcore when it is selected.
Reply browse_reply(const ServerTransport& send,const std::string& post){
    UnlockIds unlocked;Reply failure;
    if(!account_unlocks(send,form(post),unlocked,failure))
        return failure.status>0?failure:Reply{"",-2}; // A refused token still renews or expires.
    return {session_body(wall_time(),unlocked),200};
}
// Why a sign-in reply cannot be used, in the player's words. The SDK reads a
// refused TLS link, a firewall page and a rate limit all as "no response".
std::string reply_problem(const Reply& reply){
    const auto code=std::to_string(reply.status);
    if(reply.status==429)return "trop de tentatives, RetroAchievements bloque les connexions pendant une minute";
    if(reply.status<0)return reply.error.empty()?std::string{}:"aucune réponse du serveur ("+reply.error+")";
    const auto start=reply.body.find_first_not_of(" \t\r\n");
    if(start!=std::string::npos&&reply.body[start]=='{')return {};
    if(reply.status==403)return "accès refusé par RetroAchievements ou son pare-feu (HTTP 403)";
    if(reply.status>=500)return "service RetroAchievements indisponible (HTTP "+code+")";
    return "réponse inattendue de RetroAchievements (HTTP "+code+")";
}
// Server text shown in the interface: one line, bounded, never cut inside a character.
std::string server_message(const char* message){
    std::string text=message?message:"";
    for(auto& c:text)if(static_cast<unsigned char>(c)<0x20||c==0x7f)c=' ';
    if(text.size()>200){
        std::size_t cut=200;
        while(cut&&(static_cast<unsigned char>(text[cut])&0xC0)==0x80)--cut;
        text=text.substr(0,cut)+"…";
    }
    const auto first=text.find_first_not_of(' ');
    if(first==std::string::npos)return {};
    return text.substr(first,text.find_last_not_of(' ')-first+1);
}
bool loads_game(const std::string& operation) {
    return operation=="achievementsets"||operation=="startsession"||operation=="gameid"||operation=="patch";
}
// rcheevos ignores the server's error code on some game-load calls, so a
// final refusal would look like an outage and be retried forever. The login
// parser reads the common Success/Error/Code envelope of any reply.
int refusal(const Reply& reply) {
    if(reply.status<0||reply.status==408||reply.status==429||reply.status>=500)return RC_OK;
    const rc_api_server_response_t response{reply.body.c_str(),reply.body.size(),reply.status};
    rc_api_login_response_t parsed{};
    const int result=rc_api_process_login_server_response(&parsed,&response);
    const bool failed=!parsed.response.succeeded;
    rc_api_destroy_login_response(&parsed);
    if(result==RC_INVALID_CREDENTIALS||result==RC_EXPIRED_TOKEN||result==RC_ACCESS_DENIED)return result;
    if(reply.status==401)return RC_INVALID_CREDENTIALS;
    if(reply.status==403)return RC_ACCESS_DENIED;
    if(reply.status>=400||(failed&&result!=RC_INVALID_JSON&&result!=RC_NO_RESPONSE))return RC_API_FAILURE;
    return RC_OK;
}
bool expired(int result){return result==RC_INVALID_CREDENTIALS||result==RC_EXPIRED_TOKEN;}
}
// Let the SDK build official image URLs if the server did not supply one.
std::string image_url(const char* url, const rc_client_achievement_t& a, int state) {
    if (url && *url) return url;
    char buffer[256]{};
    if (rc_client_achievement_get_image_url(&a, state, buffer, sizeof(buffer)) == RC_OK)
        return buffer;
    return {};
}
std::string badge(const rc_client_achievement_t& a, bool colored = false) {
    const int state = colored ? RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED : a.state;
    const char* ready = state == RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED ? a.badge_url : a.badge_locked_url;
    return image_url(ready, a, state);
}
struct Client::Impl {
    struct Pending {
        rc_client_server_callback_t callback{};
        void* userdata{};
        std::future<Reply> future;
        std::string submission_id,username,operation,achievement;
        bool replay=false,leaderboard=false;
    };
    rc_client_t* handle = nullptr;
    ravenemu_bridge* memory = nullptr;
    int console = RAVENEMU_GB;
    std::vector<std::uint8_t> rom;
    std::vector<Pending> pending;
    std::vector<Event> events;
    Snapshot state;
    std::string retry_user, retry_token, active_rom_path;
    std::unordered_map<std::string, std::optional<Snapshot>> game_cache;
    std::shared_ptr<SessionStore> storage;
    ServerTransport transport;
    bool token_login = false, profile_dirty = false, closing = false;
    std::shared_ptr<ProfileStore> profiles;
    Clock clock;
    std::int64_t login_retry_at = 0, game_retry_at = 0;
    unsigned login_failures = 0, game_failures = 0;
    rc_client_async_handle_t* game_load = nullptr;
    rc_client_async_handle_t* login_request = nullptr;
    std::string login_problem; // transport cause of the last sign-in failure, if any
    rc_client_async_handle_t* ranking_request = nullptr;
    Ranking ranks;
    std::shared_ptr<Outbox> outbox;
    std::unordered_set<std::string> sdk_submissions,replaying;
    // Achievements of the journal, and SDK submissions not yet accepted.
    std::unordered_set<std::string> queued_ids;
    std::unordered_map<std::string,AchievementSync> unconfirmed;
    std::optional<std::pair<unsigned,unsigned>> award_scores;
    bool browsing=false,awaiting_outbox=false,preferred_hardcore=false;
    bool renew_session=false,hardcore_reset_required=false,hardcore_storage_failed=false;
    // One token renewal per refusal streak: a token that still logs in but is
    // refused again must not unload and reload the game forever.
    bool renewal_spent=false;
    int load_refusal=RC_OK;
    std::int64_t outbox_retry_at=0;
    unsigned outbox_failures=0;
    std::vector<std::uint8_t> deferred_progress;
    bool restore_progress(const std::vector<std::uint8_t>& bytes) {
        if(preferred_hardcore || rc_client_get_hardcore_enabled(handle))return false;
        const auto* game=rc_client_get_game_info(handle);
        if(!game||!state.logged_in||bytes.empty()||bytes.size()>2u*1024u*1024u){rc_client_reset(handle);return false;}
        std::istringstream input(std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size()));
        std::string version,user,hash;
        std::getline(input,version);input>>std::quoted(user)>>std::quoted(hash);input.get();
        const auto offset=input.tellg();
        if(version!="RavenEmuRA1"||!input||offset<0||static_cast<std::size_t>(offset)>=bytes.size()||
           lower_user(user)!=lower_user(state.username)||!game->hash||hash!=game->hash){rc_client_reset(handle);return false;}
        const auto at=static_cast<std::size_t>(offset);
        const bool restored=rc_client_deserialize_progress_sized(handle,bytes.data()+at,bytes.size()-at)==RC_OK;
        if(!restored)rc_client_reset(handle);
        return restored;
    }
    static std::int64_t delay(unsigned& failures) {
        return std::min<std::int64_t>(60000, 5000LL << std::min(failures++, 4u));
    }
    static bool transient(int result) {
        return result == RC_NO_RESPONSE || result == RC_API_FAILURE || result == RC_INVALID_JSON;
    }
    bool durable() { return outbox && !state.username.empty() && outbox->available(state.username) && outbox->healthy(); }
    bool reset_hardcore_runtime() {
        if(memory && !ravenemu_reset(memory)) {
            rc_client_set_hardcore_enabled(handle,0);
            preferred_hardcore=false;state.hardcore_preferred=false;
            events.push_back({"error","Hardcore non activé","La réinitialisation de la console a échoué. Le réglage repasse en Softcore."});
            events.push_back({"hardcore_refused","",""});
            return false;
        }
        rc_client_reset(handle);
        if(memory)events.push_back({"hardcore_reset","Mode Hardcore : console réinitialisée",""});
        return true;
    }
    void apply_mode(bool on) {
        if(on && !state.logged_in){rc_client_set_hardcore_enabled(handle,0);return;}
        if(on && state.logged_in && !durable()) {
            preferred_hardcore=false;state.hardcore_preferred=false;
            state.status="Hardcore non activé : un coffre système est nécessaire pour protéger les envois hors ligne";
            events.push_back({"error",state.status,"Le réglage repasse en Softcore."});
            events.push_back({"hardcore_refused","",""});
            rc_client_set_hardcore_enabled(handle,0);
            return;
        }
        // A loading game has no SDK runtime yet, so it cannot raise RESET.
        // A token renewal can also retain the SDK's previous Hardcore flag.
        if(on && (hardcore_reset_required || (memory&&!rc_client_get_game_info(handle)&&!rc_client_get_hardcore_enabled(handle)))){
            hardcore_reset_required=false;
            if(!reset_hardcore_runtime())return;
        }
        rc_client_set_hardcore_enabled(handle,on?1:0);
        state.hardcore=rc_client_get_hardcore_enabled(handle)!=0;
    }
    static void ranked(int result,const char*,rc_client_leaderboard_entry_list_t* list,rc_client_t* c,void*) {
        auto& impl=self(c);impl.ranking_request=nullptr;impl.ranks.loading=false;impl.ranks.entries.clear();
        if(result==RC_OK && list) {
            impl.ranks.total=list->total_entries;
            for(std::uint32_t i=0;i<list->num_entries;++i){const auto& e=list->entries[i];
                impl.ranks.entries.push_back({e.rank,e.user?e.user:"",e.display,
                    list->user_index==static_cast<std::int32_t>(i)||
                    (e.user&&(lower_user(e.user)==lower_user(impl.state.username)||
                              lower_user(e.user)==lower_user(impl.state.display_name)))});}
        } else impl.ranks.error="Classement indisponible. Réessayez lorsque le réseau est disponible.";
        if(list)rc_client_destroy_leaderboard_entry_list(list);
    }
    void cancel_ranking(){if(ranking_request)rc_client_abort_async(handle,ranking_request);ranking_request=nullptr;ranks={};}
    void note_queue(const std::vector<Submission>& records) {
        queued_ids.clear();
        for(const auto& s:records)if(!s.leaderboard)queued_ids.insert(std::to_string(s.object_id));
    }
    // The live sync state of each listed achievement.
    void mark_sync(std::vector<Achievement>& list) const {
        for(auto& a:list){
            const auto found=unconfirmed.find(a.id);
            a.sync=queued_ids.contains(a.id)?AchievementSync::Pending:
                found!=unconfirmed.end()?found->second:AchievementSync::Server;
        }
    }
    // Until a ROM's own data loads, show its cached summary or nothing:
    // never the title, achievements or activity of the previous game.
    void show_game(const std::string& key) {
        if(!key.empty()&&key==state.rom_path)return;
        Snapshot cached;
        if(!key.empty()&&!state.username.empty()){
            if(const auto found=game_cache.find(key);found!=game_cache.end()){if(found->second)cached=*found->second;}
            else if(profiles)profiles->load_game(state.username,key,cached);
        }
        state.rom_path=std::move(cached.rom_path);state.game_title=std::move(cached.game_title);
        state.unlocked_count=cached.unlocked_count;state.total_count=cached.total_count;
        state.unlocked_points=cached.unlocked_points;state.total_points=cached.total_points;
        state.achievements=std::move(cached.achievements);state.game_hash=std::move(cached.game_hash);
        state.rich_presence=std::move(cached.rich_presence);state.leaderboards=std::move(cached.leaderboards);
        state.game_hardcore=cached.game_hardcore;
    }
    void recover_outbox() {
        if(!state.logged_in || !outbox || !outbox->available(state.username))return;
        const auto records=outbox->entries(state.username);
        note_queue(records);
        state.pending_submissions=static_cast<std::uint32_t>(records.size());
        state.durable_submissions=outbox->healthy();
        if(records.empty()){release_outbox_wait();return;}
        if(clock()<outbox_retry_at || replaying.size()>=2)return;
        const auto* user=rc_client_get_user_info(handle);
        if(!user||!user->token)return;
        for(const auto& s:records) {
            if(sdk_submissions.contains(s.id)||replaying.contains(s.id))continue;
            rc_api_request_t req{};int result=RC_INVALID_STATE;
            const auto elapsed=static_cast<std::uint32_t>(std::clamp<std::int64_t>(wall_time()-s.earned_at,0,UINT32_MAX));
            if(s.leaderboard){
                rc_api_submit_lboard_entry_request_t p{};p.username=user->username;p.api_token=user->token;
                p.leaderboard_id=s.object_id;p.score=s.score;p.game_hash=s.game_hash.c_str();p.seconds_since_completion=elapsed;
                result=rc_api_init_submit_lboard_entry_request(&req,&p);
            }else{
                rc_api_award_achievement_request_t p{};p.username=user->username;p.api_token=user->token;
                p.achievement_id=s.object_id;p.hardcore=s.hardcore;p.game_hash=s.game_hash.c_str();p.seconds_since_unlock=elapsed;
                result=rc_api_init_award_achievement_request(&req,&p);
            }
            if(result==RC_OK&&req.post_data){
                const std::string url=req.url,post=req.post_data,type=req.content_type?req.content_type:"application/x-www-form-urlencoded";
                Pending job{};job.submission_id=s.id;job.username=s.username;job.replay=true;job.leaderboard=s.leaderboard;
                if(!s.leaderboard)job.achievement=std::to_string(s.object_id);
                job.future=std::async(std::launch::async,[send=transport,url,post,type]{return send(url,post,type);});
                replaying.insert(s.id);pending.push_back(std::move(job));
            }
            rc_api_destroy_request(&req);
            if(replaying.size()>=2)break;
        }
    }
    // Queued unlocks replay before the game loads, so its session lists them.
    void identify_after_outbox() {
        if (!awaiting_outbox) { identify(); return; }
        state.game_status = GameStatus::Loading;
        state.status = "Envoi des succès en attente avant la recherche du jeu…";
    }
    // A replay the service did not accept keeps retrying in the background:
    // it must not keep the game's achievements unloaded meanwhile.
    void release_outbox_wait() {
        if (!awaiting_outbox) return;
        awaiting_outbox = false;
        if (!rom.empty() && (memory || browsing)) identify();
    }
    void identify() {
        if (game_load) rc_client_abort_async(handle, game_load);
        game_load = nullptr;
        game_retry_at = 0;
        load_refusal = RC_OK;
        state.game_status = GameStatus::Loading;
        state.status = "Recherche des succès officiels…";
        const uint32_t id = console == RAVENEMU_GB ? 4u : console == RAVENEMU_GBC ? 6u : 5u;
        game_load = rc_client_begin_identify_and_load_game(handle, id, nullptr,
            rom.data(), rom.size(), loaded, nullptr);
    }
    void persist() {
        if (profile_dirty && state.logged_in) {
            if (!state.rom_path.empty()) {
                if (game_cache.size() >= 32) game_cache.clear();
                game_cache[state.rom_path] = state;
            }
            if (profiles) profiles->save(state);
        }
        profile_dirty = false;
    }

    static Impl& self(rc_client_t* c) {
        return *static_cast<Impl*>(rc_client_get_userdata(c));
    }
    static uint32_t read(uint32_t address, uint8_t* data, uint32_t len, rc_client_t* c) {
        const auto& impl = self(c);
        if (!impl.memory || len > 0x58000u) return 0;
        return static_cast<uint32_t>(ravenemu_read_achievement_memory(
            impl.memory, address, data, len));
    }
    static void request(const rc_api_request_t* req, rc_client_server_callback_t callback,
                        void* userdata, rc_client_t* c) {
        auto& impl = self(c);
        if (impl.closing) {
            // SDK submissions are not asynchronous handles. Cancel them while
            // the handle is still alive, including retries scheduled by the SDK.
            const char message[] = "Session arrêtée";
            const rc_api_server_response_t response{message, sizeof(message)-1, RC_API_SERVER_RESPONSE_CLIENT_ERROR};
            callback(&response, userdata);
            return;
        }
        if (!req || !req->url) return;
        const std::string url = req->url;
        const std::string post = req->post_data ? req->post_data : "";
        const std::string type = req->content_type ? req->content_type : "application/x-www-form-urlencoded";
        Pending job{};job.callback=callback;job.userdata=userdata;
        const auto fields=form(post);
        const auto field=[&](const char* name){const auto at=fields.find(name);return at==fields.end()?std::string{}:at->second;};
        const auto operation=field("r");job.operation=operation;
        if(impl.outbox&&(operation=="awardachievement"||operation=="submitlbentry")){
            Submission s;s.username=impl.state.username;s.leaderboard=operation=="submitlbentry";
            s.object_id=number<std::uint32_t>(field(s.leaderboard?"i":"a"));s.score=number<std::int32_t>(field("s"));
            s.hardcore=s.leaderboard||field("h")=="1";s.game_hash=field("m");
            s.earned_at=wall_time()-number<std::uint32_t>(field("o"));
            job.submission_id=impl.outbox->put(s);job.username=s.username;job.leaderboard=s.leaderboard;
            if(!job.submission_id.empty())impl.sdk_submissions.insert(job.submission_id);
            else {
                impl.hardcore_storage_failed=rc_client_get_hardcore_enabled(c)!=0;
                impl.events.push_back({"notice","Envoi non mémorisé","Gardez l’application ouverte jusqu’à la fin de la synchronisation."});
            }
        }
        if(operation=="awardachievement"){
            job.achievement=field("a");
            if(!job.achievement.empty())impl.unconfirmed[job.achievement]=AchievementSync::Pending;
        }
        const bool browse=operation=="startsession"&&impl.browsing&&!impl.memory;
        job.future=std::async(std::launch::async,[send=impl.transport,url,post,type,session=operation=="startsession",browse]{
            return browse?browse_reply(send,post):session?session_reply(send,url,post,type):send(url,post,type);
        });
        impl.pending.push_back(std::move(job));
    }
    static void logged(int result, const char* message, rc_client_t* c, void*) {
        auto& impl = self(c);
        impl.login_request = nullptr;
        impl.state.login_pending = false;
        if (result == RC_OK) {
            impl.state.status = "Connecté à RetroAchievements";
            impl.state.logged_in = true;
            impl.state.online = true;
            impl.login_retry_at = 0;
            impl.login_failures = 0;
            impl.state.reconnect_scheduled = false;
            impl.profile_dirty = true;
            impl.award_scores.reset();
            const auto* user = rc_client_get_user_info(c);
            if (user && user->username && user->token) {
                impl.state.username = user->username;
                impl.retry_user = user->username;
                impl.retry_token = user->token;
                impl.state.can_retry = true;
                if (impl.storage) {
                    impl.state.session_saved = impl.storage->save({impl.retry_user, impl.retry_token});
                    if (!impl.state.session_saved)
                        impl.state.status = "Connecté, mais la session n'a pas pu être mémorisée dans le coffre du système";
                }
            }
            impl.events.push_back({"login", "RetroAchievements connecté", ""});
            impl.apply_mode(impl.preferred_hardcore);
            impl.state.durable_submissions=impl.durable();
            impl.awaiting_outbox=impl.outbox&&!impl.outbox->entries(impl.state.username).empty();
            // When the ROM was opened before authentication, identify it now.
            if ((impl.memory||impl.browsing) && !impl.rom.empty()) impl.identify_after_outbox();
        } else {
            const std::string problem = std::exchange(impl.login_problem, {});
            if (impl.token_login && (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN)) {
                const bool forgotten = !impl.storage || impl.storage->clear();
                impl.retry_user.clear();
                impl.retry_token.clear();
                impl.state.can_retry = false;
                impl.state.session_saved = false;
                impl.login_retry_at = 0;
                impl.state.reconnect_scheduled = false;
                impl.state.status = forgotten ? "Session expirée : reconnectez votre compte" :
                    "Session expirée : la session enregistrée n'a pas pu être effacée dans le coffre du système";
                // A game waiting for this renewal cannot load without a new sign-in.
                if (impl.state.game_status == GameStatus::Loading) impl.state.game_status = GameStatus::None;
            } else if (impl.token_login) {
                // Keep a saved token on transient network errors so Retry (or
                // the next launch) can reconnect without asking for a password.
                impl.state.status = "Reconnexion impossible : " + (problem.empty() ?
                    std::string("réessayez lorsque le réseau est disponible") : problem);
            } else if (result == RC_NO_RESPONSE || result == RC_INVALID_JSON) {
                impl.state.status = "Connexion impossible : " + (problem.empty() ?
                    std::string("RetroAchievements ne répond pas, vérifiez le réseau") : problem);
            } else {
                // A wrong password and an outage must not read the same: show the service's reason.
                const auto reason = problem.empty() ? server_message(message) : problem;
                const bool refused = result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED;
                impl.state.status = std::string(refused ? "Connexion refusée par RetroAchievements" : "Connexion impossible") +
                    (reason.empty() ? refused ? " : identifiant ou mot de passe incorrect" : " : vérifiez votre compte et le réseau" :
                     " : " + reason);
            }
            if (impl.token_login && !impl.retry_token.empty() && transient(result)) {
                impl.login_retry_at = impl.clock() + delay(impl.login_failures);
                impl.state.reconnect_scheduled = true;
                impl.state.status = problem.empty() ? "Hors ligne : reconnexion automatique programmée" :
                    "Reconnexion automatique programmée : " + problem;
            }
            impl.events.push_back({"error", "Connexion RetroAchievements impossible",
                                   problem.empty() ? server_message(message) : problem});
        }
    }
    static void loaded(int result, const char* message, rc_client_t* c, void*) {
        auto& impl = self(c);
        impl.game_load = nullptr;
        const int refused = std::exchange(impl.load_refusal, RC_OK);
        const auto* game = rc_client_get_game_info(c);
        if (result == RC_OK && game && game->id) {
            impl.renewal_spent = false;
            rc_client_user_game_summary_t summary{};
            rc_client_get_user_game_summary(c, &summary);
            impl.state.game_status = summary.num_core_achievements ? GameStatus::Ready : GameStatus::NoAchievements;
            impl.state.status = summary.num_core_achievements ? "Jeu reconnu par RetroAchievements" :
                "Jeu reconnu, aucun succès officiel disponible";
            impl.state.online = true;
            impl.state.cached_profile = false;
            impl.state.rom_path = impl.active_rom_path;
            impl.game_failures = 0;
            impl.profile_dirty = true;
            impl.events.push_back({"info", summary.num_core_achievements ? "Succès disponibles" : "Aucun succès disponible", game->title});
            if(!impl.deferred_progress.empty()){
                impl.restore_progress(impl.deferred_progress);impl.deferred_progress.clear();
            }
        } else if (result == RC_NO_GAME_LOADED) {
            impl.state.game_status = GameStatus::UnknownRom;
            impl.state.status = "ROM non reconnue par RetroAchievements";
        } else if (expired(result) || expired(refused)) {
            // The token no longer opens game sessions. Renew it once, as for a
            // refused submission: a dead token then asks to sign in again.
            if (!impl.renewal_spent) {
                impl.renew_session = true;
                impl.state.game_status = GameStatus::Loading;
                impl.state.status = "Session à renouveler avant la recherche des succès…";
                return;
            }
            impl.state.game_status = GameStatus::Error;
            impl.state.status = "Session refusée par RetroAchievements : reconnectez votre compte";
        } else if (transient(result) && refused == RC_OK) {
            impl.state.game_status = GameStatus::Offline;
            impl.state.online = false;
            impl.state.status = "Serveur indisponible : recherche des succès à réessayer automatiquement";
            if ((impl.memory || impl.browsing) && !impl.rom.empty()) impl.game_retry_at = impl.clock() + delay(impl.game_failures);
        } else {
            impl.state.game_status = GameStatus::Error;
            impl.state.status = result == RC_ACCESS_DENIED || refused == RC_ACCESS_DENIED ?
                "Accès refusé par RetroAchievements pour ce jeu" :
                refused != RC_OK ? "RetroAchievements a refusé de charger les succès de ce jeu" :
                "Impossible d'identifier le jeu : ROM ou réponse du serveur invalide";
        }
        if (impl.state.game_status != GameStatus::Ready && impl.state.game_status != GameStatus::NoAchievements)
            impl.events.push_back({"notice", impl.state.status,
                impl.state.game_status == GameStatus::Error && message ? message : ""});
    }

    static void event(const rc_client_event_t* ev, rc_client_t* c) {
        auto& impl = self(c);
        if (impl.closing) return;
        if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED && ev->achievement) {
            impl.profile_dirty = true;
            const auto& a = *ev->achievement;
            impl.events.push_back({"achievement", a.title ? a.title : "Succès débloqué", "",
                std::to_string(a.id), badge(a, true), static_cast<int>(a.points)});
        } else if (ev->type == RC_CLIENT_EVENT_DISCONNECTED) {
            impl.state.status = "RetroAchievements hors ligne : envois en attente, nouvelle tentative automatique";
            impl.state.online = false;
            impl.events.push_back({"offline", "RetroAchievements hors ligne", ""});
        } else if (ev->type == RC_CLIENT_EVENT_RECONNECTED) {
            impl.state.status = "RetroAchievements reconnecté";
            impl.state.online = true;
            impl.events.push_back({"online", "RetroAchievements reconnecté", ""});
        } else if (ev->type == RC_CLIENT_EVENT_SERVER_ERROR) {
            impl.events.push_back({"notice", "Erreur RetroAchievements",
                ev->server_error && ev->server_error->error_message
                ? ev->server_error->error_message : "Le serveur a refusé une requête"});
        } else if (ev->type == RC_CLIENT_EVENT_GAME_COMPLETED) {
            impl.events.push_back({"notice", "Tous les succès obtenus !", ""});
        } else if (ev->type == RC_CLIENT_EVENT_RESET) {
            impl.reset_hardcore_runtime();
        } else if (ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW ||
                   ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE) {
            if (!ev->achievement) return;
            const auto& a = *ev->achievement;
            const bool show = ev->type == RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW;
            impl.events.push_back({show ? "challenge_show" : "challenge_hide",
                a.title ? a.title : "", a.measured_progress ? a.measured_progress : "",
                std::to_string(a.id), badge(a)});
        } else if (ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE) {
            if (!ev->leaderboard_tracker) return;
            const auto& tracker = *ev->leaderboard_tracker;
            const bool hide = ev->type == RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE;
            impl.events.push_back({hide ? "tracker_hide" : "tracker_show",
                "Classement en cours", tracker.display ? tracker.display : "",
                std::to_string(tracker.id)});
        } else if(ev->type==RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD && ev->leaderboard_scoreboard) {
            const auto& score=*ev->leaderboard_scoreboard;
            impl.events.push_back({"leaderboard_scoreboard","Résultat du classement",
                "Score : "+std::string(score.submitted_score)+" · Record : "+score.best_score+
                " · Rang : "+std::to_string(score.new_rank)+" / "+std::to_string(score.num_entries)});
        } else if (ev->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED ||
                   ev->type == RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED) {
            if (!ev->leaderboard) return;
            const char* kind = ev->type == RC_CLIENT_EVENT_LEADERBOARD_STARTED ? "leaderboard_started" :
                               ev->type == RC_CLIENT_EVENT_LEADERBOARD_FAILED ? "leaderboard_failed" :
                               ev->type == RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED ? "leaderboard_submitted" :
                               "leaderboard_scoreboard";
            impl.events.push_back({kind, ev->leaderboard->title ? ev->leaderboard->title : "",
                ev->leaderboard->description ? ev->leaderboard->description : ""});
        }
    }

    void create_handle() {
        handle = rc_client_create(read, request);
        if (!handle) throw std::runtime_error("Initialisation rcheevos impossible");
        rc_client_set_userdata(handle, this);
        rc_client_set_get_time_millisecs_function(handle, [](const rc_client_t* c) -> rc_clock_t {
            const auto& impl = *static_cast<Impl*>(rc_client_get_userdata(c));
            return static_cast<rc_clock_t>(impl.clock() + (impl.closing ? 121000 : 0));
        });
        rc_client_set_host(handle, "https://retroachievements.org");
        rc_client_set_hardcore_enabled(handle, 0);
        rc_client_set_allow_background_memory_reads(handle, 0);
        rc_client_set_event_handler(handle, event);
    }
    Impl(std::shared_ptr<SessionStore> session_storage, ServerTransport send,
         std::shared_ptr<ProfileStore> public_profiles, Clock time,std::shared_ptr<Outbox> durable_outbox)
        : storage(std::move(session_storage)), transport(send ? std::move(send) : ServerTransport(https_request)),
          profiles(std::move(public_profiles)), clock(time ? std::move(time) : Clock([] {
              return std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count();
          })),outbox(std::move(durable_outbox)) {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
            throw std::runtime_error("Initialisation HTTPS impossible");
        try {
            create_handle();
            if (const auto saved = storage ? storage->load() : std::nullopt;
                saved && !saved->username.empty() && !saved->token.empty()) {
                retry_user = saved->username;
                retry_token = saved->token;
                if (profiles) profiles->load(retry_user, state);
                if(state.username.empty())state.username=retry_user;
                if(outbox){
                    const auto records=outbox->entries(retry_user);note_queue(records);
                    state.pending_submissions=static_cast<std::uint32_t>(records.size());
                    state.durable_submissions=outbox->available(retry_user)&&outbox->healthy();
                }
                state.session_saved = true;
                begin_token_login("Reconnexion automatique en cours...");
            }
        } catch (...) {
            if (handle) rc_client_destroy(handle);
            curl_global_cleanup();
            throw;
        }
    }
    void close_handle() {
        if (!handle) return;
        closing = true;
        if (login_request) rc_client_abort_async(handle, login_request);
        if (game_load) rc_client_abort_async(handle, game_load);
        cancel_ranking();
        login_request = game_load = nullptr;
        // Keep the SDK alive until every transport callback has released its
        // bookkeeping. Only aborted login/game requests use SDK async handles.
        while (!pending.empty()) {
            Pending job = std::move(pending.back()); pending.pop_back();
            complete(job, true);
        }
        rc_client_idle(handle); // flush scheduled submissions with cancellation replies
        rc_client_destroy(handle);
        replaying.clear();sdk_submissions.clear();
        handle = nullptr;
        closing = false;
    }
    ~Impl() { close_handle(); curl_global_cleanup(); }
    void complete(Pending& job, bool cancel = false) {
        Reply reply;
        try { reply = job.future.get(); } catch (...) { /* treated as a network failure */ }
        if (cancel) { reply.body = "Session arrêtée"; reply.status = RC_API_SERVER_RESPONSE_CLIENT_ERROR; }
        if (job.operation == "login2") login_problem = cancel ? std::string{} : reply_problem(reply);
        const rc_api_server_response_t response{reply.body.c_str(), reply.body.size(), reply.status};
        bool accepted=false,permanent=false;
        if(!cancel && (!job.submission_id.empty()||job.replay)) {
            int result=RC_INVALID_STATE;
            if(job.leaderboard){
                rc_api_submit_lboard_entry_response_t parsed{};
                result=rc_api_process_submit_lboard_entry_server_response(&parsed,&response);
                accepted=result==RC_OK&&parsed.response.succeeded;
                permanent=!parsed.response.succeeded&&parsed.response.error_message&&
                    result!=RC_INVALID_JSON&&result!=RC_NO_RESPONSE;
                rc_api_destroy_submit_lboard_entry_response(&parsed);
            }else{
                rc_api_award_achievement_response_t parsed{};
                result=rc_api_process_award_achievement_server_response(&parsed,&response);
                accepted=result==RC_OK&&parsed.response.succeeded;
                permanent=!parsed.response.succeeded&&parsed.response.error_message&&
                    result!=RC_INVALID_JSON&&result!=RC_NO_RESPONSE;
                if(accepted && reply.status>=200&&reply.status<300 &&
                   (parsed.awarded_achievement_id || parsed.new_player_score_softcore || parsed.new_player_score))
                    award_scores=std::pair{parsed.new_player_score_softcore,parsed.new_player_score};
                rc_api_destroy_award_achievement_response(&parsed);
            }
            if(reply.status<200||reply.status>=300)accepted=false;
            if(reply.status<0||reply.status==429||reply.status>=500||reply.status==401||reply.status==403)permanent=false;
            if(reply.status==401||reply.status==403){
                // The SDK treats an explicit authorization error as terminal.
                // The durable journal must take ownership of its next attempt.
                sdk_submissions.erase(job.submission_id);
                if(reply.status==401)renew_session=!renewal_spent;
                state.status=reply.status==403?"Envoi non autorisé par RetroAchievements : les données restent conservées.":
                    renewal_spent?"Session refusée par RetroAchievements : reconnectez votre compte, les envois restent conservés.":
                    "Session à renouveler : les envois restent conservés.";
            }
            if(accepted)renewal_spent=false;
            if(accepted||permanent){
                if(outbox)outbox->erase(job.username,job.submission_id);
                sdk_submissions.erase(job.submission_id);profile_dirty=true;
                if(permanent)events.push_back({"notice","Envoi refusé par RetroAchievements","Le serveur n’a pas validé cet envoi."});
            }
            if(job.replay){
                replaying.erase(job.submission_id);
                if(!accepted&&!permanent){
                    outbox_retry_at=clock()+delay(outbox_failures);
                    // A renewal replays the queue with the new token first.
                    if(!renew_session)release_outbox_wait();
                }
                else{outbox_failures=0;outbox_retry_at=0;}
            }
        }
        if(!cancel&&!job.achievement.empty()) {
            // Same verdict as the journal: retryable failures stay pending.
            rc_api_award_achievement_response_t parsed{};
            const int result=rc_api_process_award_achievement_server_response(&parsed,&response);
            const bool ok=result==RC_OK&&parsed.response.succeeded&&reply.status>=200&&reply.status<300;
            const bool refused=!parsed.response.succeeded&&parsed.response.error_message&&
                result!=RC_INVALID_JSON&&result!=RC_NO_RESPONSE&&reply.status>0&&reply.status!=401&&
                reply.status!=403&&reply.status!=429&&reply.status<500;
            rc_api_destroy_award_achievement_response(&parsed);
            if(ok)unconfirmed.erase(job.achievement);
            else if(refused)unconfirmed[job.achievement]=AchievementSync::Refused;
        }
        if(!cancel&&loads_game(job.operation))load_refusal=refusal(reply);
        if(job.callback)job.callback(&response,job.userdata);
    }
    void begin_token_login(const char* status) {
        const bool saved = state.session_saved;
        cancel_ranking();
        if(game_load)rc_client_abort_async(handle,game_load);
        game_load=nullptr;
        rc_client_logout(handle);
        hardcore_reset_required=memory&&preferred_hardcore;
        state.logged_in = false;
        state.online = false;
        login_retry_at = 0;
        state.reconnect_scheduled = false;
        state.status = status;
        state.login_pending = true;
        state.can_retry = true;
        state.session_saved = saved;
        token_login = true;
        login_request = rc_client_begin_login_with_token(handle, retry_user.c_str(), retry_token.c_str(), logged, nullptr);
    }
    void refresh() {
        const auto* user = rc_client_get_user_info(handle);
        const auto* game = rc_client_get_game_info(handle);
        if (user) {
            state.logged_in = true;
            state.username = user->username ? user->username : "";
            state.display_name = user->display_name ? user->display_name : state.username;
            if (user->avatar_url && *user->avatar_url) state.avatar_url = user->avatar_url;
            else {
                char avatar[256]{};
                state.avatar_url = rc_client_user_get_image_url(user, avatar, sizeof(avatar)) == RC_OK
                    ? std::string(avatar) : std::string();
            }
            const auto softcore=award_scores?award_scores->first:user->score_softcore;
            const auto hardcore=award_scores?award_scores->second:user->score;
            if (state.softcore_points != softcore || state.hardcore_points != hardcore) profile_dirty = true;
            state.softcore_points=softcore;state.hardcore_points=hardcore;
        }
        state.hardcore = rc_client_get_hardcore_enabled(handle) != 0;
        if (!game || !game->id ||
            (state.game_status != GameStatus::Ready && state.game_status != GameStatus::NoAchievements)) return;
        state.game_title = game->title ? game->title : "";
        if(state.game_hardcore!=state.hardcore)profile_dirty=true;
        state.game_hardcore=state.hardcore;
        state.game_hash=game->hash?game->hash:"";
        char presence[1024]{};
        state.rich_presence=rc_client_has_rich_presence(handle)?
            (rc_client_get_rich_presence_message(handle,presence,sizeof(presence)),std::string(presence)):std::string{};
        state.leaderboards.clear();
        if(auto* leaderboards=rc_client_create_leaderboard_list(handle,RC_CLIENT_LEADERBOARD_LIST_GROUPING_NONE)){
            for(std::uint32_t i=0;i<leaderboards->num_buckets;++i){const auto& bucket=leaderboards->buckets[i];
                for(std::uint32_t j=0;j<bucket.num_leaderboards;++j){const auto* l=bucket.leaderboards[j];
                    if(l)state.leaderboards.push_back({l->id,l->title?l->title:"",l->description?l->description:""});}}
            rc_client_destroy_leaderboard_list(leaderboards);
        }
        // Unlocks read without a game session carry no date: keep the ones
        // already known. Achievement IDs are unique across all games.
        std::unordered_map<std::string,std::int64_t> dates;
        for(const auto& a:state.achievements)if(a.unlocked&&a.unlock_time>0)dates.emplace(a.id,a.unlock_time);
        state.achievements.clear();
        state.unlocked_count = 0;
        state.total_count = 0;
        state.unlocked_points = 0;
        state.total_points = 0;
        rc_client_user_game_summary_t total{};
        rc_client_get_user_game_summary(handle, &total);
        state.unlocked_count = static_cast<int>(total.num_unlocked_achievements);
        state.total_count = static_cast<int>(total.num_core_achievements);
        state.unlocked_points = total.points_unlocked;
        state.total_points = total.points_core;
        auto* list = rc_client_create_achievement_list(
            handle, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
        if (list) {
            for (uint32_t i=0; i<list->num_buckets; ++i) {
                const auto& bucket = list->buckets[i];
                for (uint32_t j=0; j<bucket.num_achievements; ++j) {
                    const auto* a = bucket.achievements[j];
                    if (!a) continue;
                    // Runtime state can be DISABLED while browsing without a core.
                    // Earned status belongs to the account flags, not the runtime.
                    const bool unlocked=(a->unlocked & (state.hardcore?
                        RC_CLIENT_ACHIEVEMENT_UNLOCKED_HARDCORE:RC_CLIENT_ACHIEVEMENT_UNLOCKED_SOFTCORE))!=0;
                    const auto id=std::to_string(a->id);
                    std::int64_t when=unlocked?static_cast<std::int64_t>(a->unlock_time):0;
                    if(unlocked&&when<=0)if(const auto known=dates.find(id);known!=dates.end())when=known->second;
                    state.achievements.push_back({id,
                        a->title ? a->title : "", a->description ? a->description : "",
                        a->measured_progress ? a->measured_progress : "",
                        badge(*a,unlocked), static_cast<int>(a->points),
                        unlocked?RC_CLIENT_ACHIEVEMENT_STATE_UNLOCKED:static_cast<int>(a->state), a->measured_percent,
                        unlocked, when, static_cast<int>(a->type), a->rarity, a->rarity_hardcore});
                }
            }
            rc_client_destroy_achievement_list(list);
        }
    }
    void pump(bool frame) {
        // Run all SDK callbacks on the owning UI/emulation thread.
        for (size_t i=0; i<pending.size();) {
            if (pending[i].future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                Pending job = std::move(pending[i]);
                pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(i));
                complete(job);
            } else ++i;
        }
        if(renew_session&&!state.login_pending&&pending.empty()&&sdk_submissions.empty()&&!retry_token.empty()){
            renew_session=false;renewal_spent=true;
            begin_token_login("Renouvellement de la session RetroAchievements…");
        }
        const auto now = clock();
        if (login_retry_at && now >= login_retry_at && !state.login_pending && !retry_token.empty())
            begin_token_login("Reconnexion automatique en cours...");
        if (game_retry_at && now >= game_retry_at && state.logged_in && (memory||browsing) && !rom.empty()) identify();
        recover_outbox();
        if (frame && memory) rc_client_do_frame(handle);
        else rc_client_idle(handle);
        if(hardcore_storage_failed){
            hardcore_storage_failed=false;preferred_hardcore=false;state.hardcore_preferred=false;
            rc_client_set_hardcore_enabled(handle,0);
            events.push_back({"error","Hardcore interrompu","La file hors ligne n’a pas pu être protégée. La partie continue en Softcore."});
            events.push_back({"hardcore_refused","",""});
        }
        refresh();
        mark_sync(state.achievements);
        persist();
    }
};
// UI smoke tests must not load a real user's saved account or make login requests.
Client::Client() : Client(std::getenv("RAVENEMUDSK_UI_SMOKE") ? nullptr : make_session_store(), {},
    std::getenv("RAVENEMUDSK_UI_SMOKE") ? nullptr : make_profile_store(),{},
    std::getenv("RAVENEMUDSK_UI_SMOKE") ? nullptr : make_outbox()) {}
Client::Client(std::shared_ptr<SessionStore> storage, ServerTransport transport,
               std::shared_ptr<ProfileStore> profiles, Clock clock,std::shared_ptr<Outbox> outbox)
    : impl_(std::make_unique<Impl>(std::move(storage),std::move(transport),std::move(profiles),std::move(clock),std::move(outbox))) {}
Client::~Client() = default;
void Client::login(const std::string& user, const std::string& password) {
    if (user.empty() || password.empty() || impl_->state.login_pending || impl_->state.logged_in) return;
    // Switching accounts must never retain the previous user's public history or token.
    rc_client_logout(impl_->handle);
    impl_->login_retry_at = impl_->game_retry_at = 0;
    impl_->game_load = nullptr;
    impl_->retry_user.clear(); impl_->retry_token.clear();
    impl_->state = Snapshot{};
    impl_->state.hardcore_preferred=impl_->preferred_hardcore;
    impl_->game_cache.clear();impl_->queued_ids.clear();impl_->unconfirmed.clear();
    if (impl_->profiles) impl_->profiles->load(user, impl_->state);
    impl_->state.logged_in = false;
    impl_->state.login_pending = true;
    impl_->token_login = false;
    impl_->renewal_spent = false;
    impl_->state.status = "Connexion en cours…";
    impl_->login_request = rc_client_begin_login_with_password(impl_->handle, user.c_str(), password.c_str(), Impl::logged, nullptr);
}
void Client::retry() {
    impl_->outbox_retry_at=0;
    if (impl_->state.logged_in) {
        if ((impl_->memory || impl_->browsing) && impl_->state.game_status == GameStatus::Offline) impl_->identify();
        return; // Preserve the SDK's pending award submissions across network outages.
    }
    if (impl_->state.login_pending || impl_->retry_user.empty() || impl_->retry_token.empty()) return;
    impl_->begin_token_login("Reconnexion en cours...");
}
void Client::disconnect() {
    const auto user=impl_->state.username.empty()?impl_->retry_user:impl_->state.username;
    const bool cache_forgotten = !impl_->profiles || impl_->state.username.empty() ||
        impl_->profiles->clear(impl_->state.username);
    impl_->login_retry_at = impl_->game_retry_at = 0;
    impl_->login_failures = impl_->game_failures = 0;
    impl_->profile_dirty = false;
    impl_->close_handle();
    const bool queue_forgotten=!impl_->outbox||user.empty()||impl_->outbox->clear(user);
    impl_->create_handle();
    const bool forgotten = !impl_->storage || impl_->storage->clear();
    impl_->retry_user.clear();
    impl_->retry_token.clear();
    impl_->game_cache.clear();impl_->queued_ids.clear();impl_->unconfirmed.clear();
    impl_->state = Snapshot{};
    impl_->award_scores.reset();impl_->deferred_progress.clear();impl_->browsing=false;impl_->awaiting_outbox=false;
    impl_->renew_session=false;impl_->hardcore_reset_required=false;impl_->hardcore_storage_failed=false;
    impl_->renewal_spent=false;impl_->load_refusal=RC_OK;
    impl_->state.hardcore_preferred=impl_->preferred_hardcore;
    if (!forgotten)
        impl_->state.status = "Déconnecté, mais le coffre du système n'a pas pu effacer la session enregistrée";
    else if (!cache_forgotten)
        impl_->state.status = "Déconnecté, mais le profil public mémorisé n'a pas pu être effacé";
    else if(!queue_forgotten)
        impl_->state.status="Déconnecté, mais la file d’envoi n’a pas pu être effacée";
    impl_->events.clear();
}
void Client::start_game(int console, const std::vector<std::uint8_t>& rom, ravenemu_bridge* core,
                        const std::string& rom_path) {
    impl_->cancel_ranking();impl_->deferred_progress.clear();impl_->browsing=core==nullptr;
    impl_->active_rom_path = rom_path.empty() ? std::string{} :
        rom_identity(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(rom_path.data()), rom_path.size())));
    impl_->memory = core;
    impl_->console = console;
    impl_->rom = rom;
    if (impl_->game_load) rc_client_abort_async(impl_->handle, impl_->game_load);
    impl_->game_load = nullptr;
    impl_->game_retry_at = 0;
    rc_client_unload_game(impl_->handle);
    impl_->show_game(impl_->active_rom_path);
    impl_->state.cached_profile = !impl_->state.game_title.empty();
    impl_->state.game_status = GameStatus::None;
    impl_->game_failures = 0;
    if (rc_client_get_user_info(impl_->handle)) impl_->identify_after_outbox();
    else if (!impl_->state.login_pending) impl_->state.status = "Connectez votre compte pour activer les succès";
}
void Client::end_game() {
    impl_->cancel_ranking();impl_->deferred_progress.clear();impl_->browsing=false;
    impl_->refresh();
    impl_->profile_dirty=true;
    impl_->persist();
    if (impl_->game_load) rc_client_abort_async(impl_->handle, impl_->game_load);
    impl_->game_load = nullptr;
    impl_->game_retry_at = 0;
    impl_->memory = nullptr;
    impl_->rom.clear();
    rc_client_unload_game(impl_->handle);
    impl_->state.cached_profile = !impl_->state.game_title.empty();
}
void Client::pump(bool frame) { impl_->pump(frame); }
void Client::reset() { rc_client_reset(impl_->handle); }
bool Client::set_hardcore(bool on) {
    impl_->preferred_hardcore=on;impl_->state.hardcore_preferred=on;
    if(on)impl_->deferred_progress.clear();
    impl_->apply_mode(on);
    impl_->refresh();
    return !on||hardcore();
}
bool Client::refresh_game(const std::string& value) {
    if(impl_->memory||impl_->state.login_pending)return false;
    if(!impl_->pending.empty() || (impl_->outbox&&!impl_->outbox->entries(impl_->state.username).empty())){
        impl_->state.status="Attendez la fin des envois avant d’actualiser le profil.";return false;
    }
    auto path=value.empty()?impl_->state.rom_path:value;
    if(!path.empty()){
        try{
            const auto file=std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()),path.size()));
            auto ext=file.extension().u8string();const std::string type=lower_user({ext.begin(),ext.end()});
            const int console=type==".gb"?RAVENEMU_GB:type==".gbc"?RAVENEMU_GBC:type==".gba"?RAVENEMU_GBA:-1;
            if(console<0||std::filesystem::file_size(file)>64u*1024u*1024u)throw std::runtime_error("ROM invalide");
            std::ifstream input(file,std::ios::binary);
            std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),{}};
            if(bytes.empty()||input.bad())throw std::runtime_error("ROM illisible");
            impl_->rom=std::move(bytes);impl_->console=console;impl_->active_rom_path=rom_identity(file);impl_->browsing=true;
        }catch(...){impl_->state.status="Actualisation impossible : la ROM est introuvable ou invalide.";return false;}
    }
    if(impl_->retry_user.empty()||impl_->retry_token.empty()){
        impl_->state.status="Connectez votre compte pour actualiser les succès.";return false;
    }
    impl_->show_game(impl_->active_rom_path);
    impl_->state.cached_profile=!impl_->state.game_title.empty();
    impl_->begin_token_login("Actualisation du profil et des succès…");return true;
}
void Client::fetch_ranking(std::uint32_t id,bool around_user) {
    impl_->cancel_ranking();impl_->ranks.id=id;impl_->ranks.around_user=around_user;
    if(!impl_->state.logged_in||!rc_client_get_leaderboard_info(impl_->handle,id)){
        impl_->ranks.error="Actualisez les succès de ce jeu pour consulter ses classements.";return;
    }
    impl_->ranks.loading=true;
    impl_->ranking_request=around_user?
        rc_client_begin_fetch_leaderboard_entries_around_user(impl_->handle,id,10,Impl::ranked,nullptr):
        rc_client_begin_fetch_leaderboard_entries(impl_->handle,id,1,20,Impl::ranked,nullptr);
}
Ranking Client::ranking()const{return impl_->ranks;}
std::vector<std::uint8_t> Client::save_progress()const{
    const auto* game=rc_client_get_game_info(impl_->handle);
    if(impl_->preferred_hardcore||hardcore()||!game||!game->hash||!impl_->state.logged_in)return {};
    const auto size=rc_client_progress_size(impl_->handle);
    if(!size||size>2u*1024u*1024u-1024)return {};
    std::ostringstream header;header<<"RavenEmuRA1\n"<<std::quoted(lower_user(impl_->state.username))<<' '<<std::quoted(game->hash)<<'\n';
    const auto text=header.str();std::vector<std::uint8_t> bytes(text.begin(),text.end());bytes.resize(text.size()+size);
    if(rc_client_serialize_progress_sized(impl_->handle,bytes.data()+text.size(),size)!=RC_OK)return {};
    return bytes;
}
bool Client::load_progress(const std::vector<std::uint8_t>& bytes){
    if(impl_->preferred_hardcore||hardcore())return false;
    impl_->deferred_progress.clear();
    if(!bytes.empty()&&bytes.size()<=2u*1024u*1024u &&
       (impl_->state.game_status==GameStatus::Loading||impl_->state.login_pending||
        (impl_->memory&&!impl_->rom.empty()&&!rc_client_get_game_info(impl_->handle)&&
         impl_->state.game_status!=GameStatus::UnknownRom&&impl_->state.game_status!=GameStatus::Error))){
        impl_->deferred_progress=bytes;return true;
    }
    return impl_->restore_progress(bytes);
}
bool Client::hardcore() const { return rc_client_get_hardcore_enabled(impl_->handle) != 0; }
bool Client::can_load_state() const { return !impl_->preferred_hardcore && !hardcore(); }
bool Client::frames_on_hold() const {
    const auto& s = impl_->state;
    return s.hardcore_preferred &&
           (!s.logged_in || (s.game_status != GameStatus::Ready && s.game_status != GameStatus::NoAchievements));
}
bool Client::may_pause(std::uint32_t* frames_left) const {
    if (frames_left) *frames_left = 0;
    if (!hardcore()) return true;
    // Without a running game session there is nothing to evaluate, so nothing to throttle.
    if (impl_->state.game_status != GameStatus::Ready && impl_->state.game_status != GameStatus::NoAchievements) return true;
    return rc_client_can_pause(impl_->handle, frames_left) != 0;
}
Snapshot Client::snapshot() const { return impl_->state; }
std::optional<Snapshot> Client::game_snapshot(const std::string& path) {
    const auto& user = impl_->state.username;
    if (user.empty() || path.empty()) return std::nullopt;
    const auto key = rom_identity(std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size())));
    if (key.empty()) return std::nullopt;
    if (impl_->state.rom_path == key && !impl_->state.game_title.empty()) return impl_->state;
    auto found = impl_->game_cache.find(key);
    if (found == impl_->game_cache.end()) {
        if (impl_->game_cache.size() >= 32) impl_->game_cache.clear();
        Snapshot cached;
        const bool loaded = impl_->profiles && impl_->profiles->load_game(user, key, cached);
        found = impl_->game_cache.emplace(key, loaded ? std::optional<Snapshot>(std::move(cached)) : std::nullopt).first;
    }
    auto result = found->second;
    if (result) { result->cached_profile = true; impl_->mark_sync(result->achievements); }
    return result;
}
std::vector<Event> Client::drain_events() {
    auto events = std::move(impl_->events);
    impl_->events.clear();
    return events;
}
Client& client() { static Client c; return c; }
} // namespace ravenemudsk_ra

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/outbox.hpp>
#include <ravenemudsk/achievements/profile_store.hpp>
#include <ravenemu/sha256.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <map>
#include <random>
#include <sstream>
#include <span>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace ravenemudsk_ra {
namespace {
namespace fs = std::filesystem;
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string digest(const std::string& value) {
    const auto bytes = ravenemu::detail::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    std::string hex;
    for (const auto b : bytes) { hex += "0123456789abcdef"[b >> 4]; hex += "0123456789abcdef"[b & 15]; }
    return hex;
}
std::string hmac(const std::string& key, const std::string& value) {
    std::string inner(64, '\x36'), outer(64, '\x5c');
    for (std::size_t i=0;i<key.size();++i) { inner[i] ^= key[i]; outer[i] ^= key[i]; }
    inner += value;
    const auto hash = ravenemu::detail::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(inner.data()),inner.size()));
    outer.append(reinterpret_cast<const char*>(hash.data()), hash.size());
    return digest(outer);
}
bool hex(const std::string& value, std::size_t size) {
    return value.size()==size && std::all_of(value.begin(),value.end(),[](char c){
        return (c>='0' && c<='9') || (c>='a' && c<='f'); });
}
bool same(const std::string& a, const std::string& b) {
    if(a.size()!=b.size()) return false;
    unsigned difference=0;
    for(std::size_t i=0;i<a.size();++i) difference |= static_cast<unsigned char>(a[i]^b[i]);
    return difference==0;
}
std::string serialize(const Submission& s) {
    std::ostringstream out;
    out << std::quoted(s.id) << ' ' << std::quoted(s.username) << ' ' << std::quoted(s.game_hash)
        << ' ' << s.object_id << ' ' << s.score << ' ' << s.leaderboard << ' ' << s.hardcore << ' ' << s.earned_at;
    return out.str();
}
}
struct Outbox::Impl {
    struct Account { std::string key; std::vector<Submission> records; bool loaded=false, valid=true; };
    fs::path root;
    VaultFactory vaults;
    std::map<std::string,Account> accounts;
    bool ok=true;
    fs::path path(const std::string& user) const { return root / (digest(lower(user))+".outbox"); }
    Account& account(const std::string& user) {
        auto& a=accounts[lower(user)];
        if(a.loaded) return a;
        a.loaded=true;
        if(root.empty() || user.empty() || user.size()>256) { a.valid=false;ok=false;return a; }
        auto vault=vaults("RavenEmu/RetroAchievements/Outbox/"+digest(lower(user)));
        if(!vault) { a.valid=false;ok=false;return a; }
        auto secret=vault->load();
        if(!secret) {
            if(fs::exists(path(user))) { a.valid=false;ok=false;return a; }
            std::random_device random;
            std::string key;
            for(int i=0;i<32;++i){const auto byte=random()&255;key+="0123456789abcdef"[byte>>4];key+="0123456789abcdef"[byte&15];}
            if(!vault->save({lower(user),key})) { a.valid=false;ok=false;return a; }
            secret=SessionCredentials{lower(user),key};
        }
        if(lower(secret->username)!=lower(user) || !hex(secret->token,64)){a.valid=false;ok=false;return a;}
        a.key=secret->token;
        // Decode the vault's hex string into the actual 32-byte HMAC key.
        a.key.clear();
        for(std::size_t i=0;i<64;i+=2) a.key+=static_cast<char>(std::stoi(secret->token.substr(i,2),nullptr,16));
        const auto file=path(user);
        if(!fs::exists(file)){if(!write(user,a))a.valid=false;return a;}
        if(fs::file_size(file)>1024*1024){a.valid=false;ok=false;return a;}
        std::ifstream in(file,std::ios::binary);
        std::string version,line,signature;
        std::getline(in,version);
        if(version!="RavenEmuOutbox1"){a.valid=false;ok=false;return a;}
        std::getline(in,signature);
        const std::string body{std::istreambuf_iterator<char>(in),{}};
        if(!same(hmac(a.key,body),signature)){a.valid=false;ok=false;return a;}
        std::istringstream data(body);
        std::size_t count=0;
        std::string owner;
        data>>std::quoted(owner)>>count;
        if(!data || owner!=lower(user) || count>1024){a.valid=false;ok=false;return a;}
        std::getline(data,line);
        for(std::size_t i=0;i<count;++i) {
            if(!std::getline(data,line)){a.valid=false;break;}
            Submission s;
            std::istringstream row(line);
            row>>std::quoted(s.id)>>std::quoted(s.username)>>std::quoted(s.game_hash)
               >>s.object_id>>s.score>>s.leaderboard>>s.hardcore>>s.earned_at;
            if(!row || serialize(s)!=line || lower(s.username)!=lower(user) || !hex(s.id,64) ||
               !hex(s.game_hash,32) || !s.object_id || s.earned_at<=0){a.valid=false;break;}
            a.records.push_back(std::move(s));
        }
        data>>std::ws;
        if(!data.eof()) a.valid=false;
        if(!a.valid){a.records.clear();ok=false;}
        return a;
    }
    bool write(const std::string& user, Account& a) {
        try {
            fs::create_directories(root);
#ifndef _WIN32
            fs::permissions(root,fs::perms::owner_all,fs::perm_options::replace);
#endif
            static std::atomic<unsigned long long> serial{0};
            const auto file=path(user), temp=fs::path(file).concat("."+std::to_string(++serial)+".tmp");
            std::ofstream out(temp,std::ios::binary|std::ios::trunc);
            std::ostringstream body;
            body<<std::quoted(lower(user))<<'\n'<<a.records.size()<<'\n';
            for(const auto& s:a.records)body<<serialize(s)<<'\n';
            const auto value=body.str();
            out<<"RavenEmuOutbox1\n"<<hmac(a.key,value)<<'\n'<<value;
            out.close();if(!out){ok=false;return false;}
#ifndef _WIN32
            fs::permissions(temp,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace);
            fs::rename(temp,file);
#else
            if(!MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){ok=false;return false;}
#endif
            ok=true;return true;
        } catch(...) {ok=false;return false;}
    }
};
Outbox::Outbox(fs::path root,VaultFactory vaults):impl_(std::make_unique<Impl>()) {
    impl_->root=std::move(root);impl_->vaults=vaults ? std::move(vaults) : VaultFactory([](const auto& target){return make_session_store(target);});
}
Outbox::~Outbox()=default;
bool Outbox::available(const std::string& user){
    try{return impl_->account(user).valid;}
    catch(...){impl_->accounts[lower(user)].valid=false;impl_->ok=false;return false;}
}
bool Outbox::healthy()const{return impl_->ok;}
std::vector<Submission> Outbox::entries(const std::string& user){return available(user)?impl_->account(user).records:std::vector<Submission>{};}
std::string Outbox::put(Submission s){
    if(!available(s.username)||!hex(s.game_hash,32)||!s.object_id||s.earned_at<=0)return {};
    auto& a=impl_->account(s.username);
    const auto key=lower(s.username)+":"+s.game_hash+":"+std::to_string(s.object_id)+":"+
        std::to_string(s.leaderboard)+":"+std::to_string(s.hardcore)+":"+std::to_string(s.leaderboard?s.score:0);
    s.id=digest(key);
    if(std::any_of(a.records.begin(),a.records.end(),[&](const auto& v){return v.id==s.id;}))return s.id;
    if(a.records.size()>=1024){impl_->ok=false;return {};}
    a.records.push_back(s);
    if(!impl_->write(s.username,a)){a.records.pop_back();return {};}
    return s.id;
}
bool Outbox::erase(const std::string& user,const std::string& id){
    if(!available(user))return false;
    auto& a=impl_->account(user);
    const auto previous=a.records;
    std::erase_if(a.records,[&](const auto& v){return v.id==id;});
    if(impl_->write(user,a))return true;
    a.records=previous;return false;
}
bool Outbox::clear(const std::string& user){
    try{
        fs::remove(impl_->path(user));impl_->accounts.erase(lower(user));
        const auto vault=impl_->vaults("RavenEmu/RetroAchievements/Outbox/"+digest(lower(user)));
        return !vault || vault->clear();
    }catch(...){impl_->ok=false;return false;}
}
std::shared_ptr<Outbox> make_outbox(){auto root=achievement_data_root();if(!root.empty())root/="pending";return std::make_shared<Outbox>(root);}
}

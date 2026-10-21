// SPDX-License-Identifier: AGPL-3.0-only
#include "retro_test_storage.hpp"
#include <fstream>
#include <iostream>
using namespace ravenemudsk_ra;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
void write(const std::filesystem::path& path,const std::string& value){std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<value;}
int main(){
    retro_test::Directory root;retro_test::Vaults keys;
    const auto fixed=std::make_shared<retro_test::Vault>();
    fixed->value=SessionCredentials{"gameuser","000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"};
    auto factory=[fixed](const std::string&){return fixed;};
    Submission award;award.username="GameUser";award.game_hash="0123456789abcdef0123456789abcdef";
    award.object_id=123;award.hardcore=true;award.earned_at=1700000000;
    std::string id;
    {
        Outbox queue(root.path,factory);check(queue.available("GameUser"),"Protected journal unavailable");
        id=queue.put(award);check(!id.empty(),"Award not persisted");
        auto duplicate=award;duplicate.earned_at+=60;check(queue.put(duplicate)==id,"Retry created a second award");
        check(queue.entries("GAMEUSER").size()==1,"Account names must be case insensitive");
        check(queue.entries("GameUser")[0].earned_at==award.earned_at,"Retry changed original unlock time");
    }
    std::filesystem::path file;
    for(const auto& entry:std::filesystem::directory_iterator(root.path))if(entry.path().extension()==".outbox")file=entry.path();
    check(!file.empty(),"Journal not written");const auto original=read(file);
    // Expected MAC calculated independently with Python's hmac/hashlib for this fixed record/key.
    check(original.find("a1de4750eb6808099f7c12866d0a04f8ab2d7fba23c7a8f1cd2646ae8fa25450")!=std::string::npos,"Journal HMAC differs from independent SHA-256 reference");
    check(original.find(fixed->value->token)==std::string::npos,"Journal exposed its signing key");
    {
        Outbox restarted(root.path,factory);check(restarted.entries("GameUser").size()==1,"Restart lost the pending award");
        auto score=award;score.leaderboard=true;score.object_id=4401;score.score=90;
        const auto score_id=restarted.put(score);check(!score_id.empty()&&score_id!=id,"Leaderboard lost its identity");
        score.score=95;check(!restarted.put(score).empty()&&restarted.entries("GameUser").size()==3,"Distinct scores must be retained");
        check(restarted.erase("GameUser",id)&&restarted.entries("GameUser").size()==2,"Acknowledged award not removed");
    }
    // Never replay an edited, truncated, unsigned, account-swapped or keyless journal.
    for(auto corrupt: {original.substr(0,original.size()-4),std::string("RavenEmuOutbox1\n"),original+"extra"}){
        write(file,corrupt);Outbox damaged(root.path,factory);
        check(!damaged.available("GameUser")&&damaged.entries("GameUser").empty()&&!damaged.healthy(),"Tampered journal accepted");
    }
    write(file,original);
    // Another account's journal, copied over this one's, is refused even with the same key.
    {
        const auto other_vault=std::make_shared<retro_test::Vault>();
        other_vault->value=SessionCredentials{"otheruser",fixed->value->token};
        auto other_keys=[other_vault](const std::string&){return other_vault;};
        std::filesystem::path other_file;
        {Outbox other(root.path,other_keys);auto mine=award;mine.username="OtherUser";check(!other.put(mine).empty(),"Second account not persisted");}
        for(const auto& entry:std::filesystem::directory_iterator(root.path))
            if(entry.path().extension()==".outbox"&&entry.path()!=file)other_file=entry.path();
        check(!other_file.empty(),"Second account's journal not written");
        write(other_file,original);
        {Outbox swapped(root.path,other_keys);
            check(!swapped.available("OtherUser")&&swapped.entries("OtherUser").empty(),"Another account's journal was replayed");}
        {Outbox owner(root.path,factory);check(owner.entries("GameUser").size()==1,"The owner's journal must stay readable");}
        std::filesystem::remove(other_file);
    }
    auto changed=original;changed[changed.find("1700000000")]='9';write(file,changed);
    {Outbox damaged(root.path,factory);check(!damaged.available("GameUser"),"Edited unlock timestamp accepted");}
    write(file,original);
    {Outbox missing(root.path,keys.factory());check(!missing.available("GameUser"),"A missing key must not adopt an existing journal");}
    const auto broken=std::make_shared<retro_test::Vault>();broken->throws=true;
    {Outbox unavailable(root.path,[broken](const auto&){return broken;});
        check(!unavailable.available("GameUser")&&!unavailable.available("GameUser"),"A throwing vault became trusted on retry");}
    {
        Outbox restored(root.path,factory);check(restored.entries("GameUser").size()==1,"Valid journal failed to reopen");
        check(restored.clear("GameUser")&&!fixed->value&&!std::filesystem::exists(file),"Logout must remove journal and signing key");
    }
    std::cout<<"Protected outbox persistence, deduplication, HMAC reference, tampering, account swap and vault failures passed\n";
}

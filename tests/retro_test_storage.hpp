// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/outbox.hpp>
#include <chrono>
#include <map>
#include <stdexcept>
namespace retro_test {
struct Directory {
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("raven-retro-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Directory(){std::filesystem::create_directories(path);}
    ~Directory(){std::error_code ignored;std::filesystem::remove_all(path,ignored);}
};
struct Vault : ravenemudsk_ra::SessionStore {
    std::optional<ravenemudsk_ra::SessionCredentials> value;
    bool writable=true,throws=false;
    std::optional<ravenemudsk_ra::SessionCredentials> load()override{
        if(throws)throw std::runtime_error("Vault unavailable");return value;
    }
    bool save(const ravenemudsk_ra::SessionCredentials& v)override{if(!writable)return false;value=v;return true;}
    bool clear()override{value.reset();return true;}
};
struct Vaults {
    std::map<std::string,std::shared_ptr<Vault>> stores;
    ravenemudsk_ra::Outbox::VaultFactory factory(){return [this](const auto& target){
        auto& vault=stores[target];if(!vault)vault=std::make_shared<Vault>();return vault;
    };}
};
}

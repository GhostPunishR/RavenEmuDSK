// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
namespace ravenemudsk_ra {
struct Snapshot;
// Public display data only. Authentication always goes through the SDK and OS vault.
class ProfileStore {
public:
    virtual ~ProfileStore() = default;
    virtual bool load(const std::string& username, Snapshot& snapshot) = 0;
    virtual bool save(const Snapshot& snapshot) = 0;
    virtual bool clear(const std::string& username) = 0;
};
std::shared_ptr<ProfileStore> make_profile_store(const std::filesystem::path& root = {});
}

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
    virtual bool load_game(const std::string&, const std::string&, Snapshot&) {
        return false;
    }
    virtual bool clear(const std::string& username) = 0;
};
// Use the full native path, never a filename or a display title, to distinguish editions.
std::string rom_identity(const std::filesystem::path& rom);
std::shared_ptr<ProfileStore> make_profile_store(const std::filesystem::path& root = {});
}

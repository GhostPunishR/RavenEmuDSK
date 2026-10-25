// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/library/library.hpp>
#include <ravenemudsk/library/storage.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
    out.close();
    require(bool(out), "Could not write test fixture");
}
std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    require(bool(in), "Expected file is missing");
    return {std::istreambuf_iterator<char>(in), {}};
}
struct TemporaryFolder {
    fs::path path = fs::temp_directory_path() / ("raven-storage-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TemporaryFolder() { std::error_code ec; fs::remove_all(path, ec); }
};
}
int main() {
    using namespace ravenemudsk_ui;
    TemporaryFolder tmp;
    const auto folders = prepare_folders({tmp.path / "data"});
    const auto first = tmp.path / "original" / "Game.gb";
    const auto second = tmp.path / "modified" / "Game.gb";
    write(first, "original ROM");
    write(second, "modified ROM");
    const auto a = save_path(folders, first, ".sav");
    const auto b = save_path(folders, second, ".sav");
    require(a != b, "Different ROM paths must not share a save");
    write(a, "progress A");
    write(b, "progress B");
    require(read(a) == "progress A" && read(b) == "progress B", "Save isolation failed");
    require(save_path(folders, first.parent_path() / "." / first.filename(), ".sav") == a,
            "Equivalent paths must resolve to the same save");
    std::error_code symlink_error;
    const auto alias = first.parent_path() / "Alias.gb";
    fs::create_symlink(first, alias, symlink_error);
    if (!symlink_error)
        require(save_path(folders, alias, ".sav") == a, "A ROM symlink must keep the same save");
    require(save_path(folders, first, ".rstate") != save_path(folders, second, ".rstate"),
            "Save states must also be isolated");

    // Migrate older shared files once, keep backups, and share ownership across suffixes.
    const auto old_rom = tmp.path / "original" / "Old.gb";
    const auto other_old_rom = tmp.path / "modified" / "Old.gb";
    write(old_rom, "ROM A");
    write(other_old_rom, "ROM B");
    const auto old_save = folders.saves / "Old.gb.sav";
    write(old_save, "old progress");
    write(folders.saves / "Old.gb.rstate", "old state");
    const auto migrated = save_path(folders, old_rom, ".sav");
    require(read(migrated) == "old progress" && read(old_save) == "old progress",
            "Migration must preserve the old save");
    require(read(save_path(folders, old_rom, ".rstate")) == "old state", "State migration failed");
    require(!fs::exists(save_path(folders, other_old_rom, ".sav")),
            "A same-named ROM must not inherit another ROM's old save");
    require(!fs::exists(save_path(folders, other_old_rom, ".rstate")),
            "Legacy ownership must apply to every suffix");
    write(migrated, "new progress");
    require(read(save_path(folders, old_rom, ".sav")) == "new progress",
            "Migration must never overwrite a current save");
    require(read(old_save) == "old progress", "Writing a migrated save changed the backup");

    // A save next to a ROM belongs to that ROM and takes precedence over shared legacy data.
    write(sidecar(second, ".sav"), "local progress");
    fs::remove(b);
    write(folders.saves / "Game.gb.sav", "shared progress");
    require(read(save_path(folders, second, ".sav")) == "local progress",
            "ROM-local progress must take precedence");

    // The launcher must find captures from either historical suffix/location.
    write(sidecar(first, ".ravenemudsk-preview.bmp"), "old capture");
    const auto preview = preview_path(folders, first);
    require(read(preview) == "old capture", "Old capture suffix was not migrated");
    write(preview, "new capture");
    require(read(preview_path(folders, first)) == "new capture", "Launcher cannot find new capture");
    require(read(sidecar(first, ".ravenemudsk-preview.bmp")) == "old capture",
            "Old capture backup was modified");
    write(sidecar(second, ".ravenemu-preview.bmp"), "player capture");
    require(read(preview_path(folders, second)) == "player capture", "Player capture migration failed");

    // Do not silently fall back to a shared writable save when migration cannot be recorded.
    const auto broken = prepare_folders({tmp.path / "broken"});
    write(broken.saves / ".legacy-owners", "not a directory");
    write(broken.saves / "Game.gb.sav", "untouched");
    bool failed = false;
    try { (void)save_path(broken, first, ".sav"); }
    catch (const fs::filesystem_error&) { failed = true; }
    require(failed, "Migration failures must be reported");
    require(read(broken.saves / "Game.gb.sav") == "untouched", "Failed migration damaged old progress");

    // The RavenEmuDSK folders move into the RavenEmu one, without replacing a file.
    const auto user_data = tmp.path / "user data";
    const auto root = user_data / "RavenEmu";
    const auto legacy = user_data / "RavenEmuDSK" / "RavenEmuDSK";
    write(legacy / "bibliotheque.txt", "old library");
    write(legacy / "reglages.txt", "old settings");
    write(root / "reglages.txt", "new settings");
    write(root / "RetroAchievements" / "profile", "account");
    write(root / "RavenEmuDSK" / "Sauvegardes" / "Game.gb.sav", "old save");
    write(root / "RavenEmuDSK" / "Jaquettes" / "Old.png", "old cover");
    write(root / "Jaquettes" / "New.png", "new cover");
    move_legacy_data(root);
    require(read(root / "bibliotheque.txt") == "old library", "Library was not moved");
    require(read(root / "reglages.txt") == "new settings", "A newer file was replaced");
    require(read(legacy / "reglages.txt") == "old settings", "A file that could not move was lost");
    require(!fs::exists(legacy / "bibliotheque.txt"), "Library was copied, not moved");
    require(read(root / "Sauvegardes" / "Game.gb.sav") == "old save", "Saves were not moved");
    require(read(root / "Jaquettes" / "Old.png") == "old cover", "Folders were not merged");
    require(read(root / "Jaquettes" / "New.png") == "new cover", "Merging lost a file");
    require(read(root / "RetroAchievements" / "profile") == "account", "RetroAchievements data changed");
    require(!fs::exists(root / "RavenEmuDSK"), "Empty old folder was kept");
    fs::remove(legacy / "reglages.txt");
    move_legacy_data(root);
    require(!fs::exists(user_data / "RavenEmuDSK"), "Empty old folders were kept");
    require(read(root / "reglages.txt") == "new settings", "A second run changed the data");
    const auto fresh = tmp.path / "fresh" / "RavenEmu";
    move_legacy_data(fresh);
    require(!fs::exists(tmp.path / "fresh"), "Nothing to move must create nothing");
    write(tmp.path / "file" / "RavenEmuDSK", "a file, not a folder");
    move_legacy_data(tmp.path / "file" / "RavenEmu");
    require(read(tmp.path / "file" / "RavenEmuDSK") == "a file, not a folder", "A file was removed");

    // The capitalized folders of older versions become lowercase, files included.
    const auto portable = tmp.path / "portable";
    write(portable / "Sauvegardes" / "Game.gb.sav", "old save");
    write(portable / "Jaquettes" / "Game.png", "cover");
    write(portable / "BIOS" / "gba_bios.bin", "bios");
    write(portable / "Captures" / "a.png", "first");
    write(portable / "captures" / "b.png", "second"); // The same folder where case is ignored.
    const auto lowered = prepare_folders({portable});
    require(lowered.saves == portable / "sauvegardes" && lowered.bios == portable / "bios", "Folders are not lowercase");
    require(read(lowered.saves / "Game.gb.sav") == "old save", "Saves were lost in the rename");
    require(read(lowered.covers / "Game.png") == "cover", "Covers were lost in the rename");
    require(read(lowered.bios / "gba_bios.bin") == "bios", "BIOS was lost in the rename");
    require(read(lowered.captures / "a.png") == "first" && read(lowered.captures / "b.png") == "second",
            "Captures were not merged");
    std::vector<std::string> names; // As written on disk.
    for (const auto& entry : fs::directory_iterator(portable)) names.push_back(entry.path().filename().string());
    std::sort(names.begin(), names.end());
    require(names == std::vector<std::string>{"bios", "captures", "jaquettes", "sauvegardes"},
            "A folder kept its capital letter");
    require(prepare_folders({portable}).saves == lowered.saves, "A second run moved the folders");
}

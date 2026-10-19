// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/library.hpp>
#include <ravenemudsk/ui/settings.hpp>
#include <ravenemudsk/ui/storage.hpp>
#include <ravenemudsk/ui/updates.hpp>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
namespace fs = std::filesystem;
int main() {
    const auto folder = fs::temp_directory_path() / "ravenemudsk_library_tests";
    fs::create_directories(folder);
    const auto gb = folder / "Game One.GB";
    const auto gba = folder / "Game_two.gba";
    { std::ofstream f(gb); f << "test"; }
    { std::ofstream f(gba); f << "test"; }
    { std::ofstream f(folder / "not-a-rom.txt"); f << "test"; }
    ravenemudsk_ui::Library library(folder / "library.txt");
    assert(library.add(gb));
    assert(!library.add(gb));
    assert(library.import_folder(folder) == 1);
    assert(library.games().size() == 2);
    assert(ravenemudsk_ui::detect_system(gb) == "gb");
    assert(ravenemudsk_ui::display_title(library.games()[1]) == "Game two");
    library.mark_played(gb, 125);
    library.save();
    ravenemudsk_ui::Library loaded(folder / "library.txt");
    loaded.load();
    assert(loaded.games().size() == 2);
    assert(loaded.games()[0].seconds_played == 125);
    assert(loaded.games()[0].last_played > 0);
    // Non-ANSI names (here Japanese) must keep their sidecar files on Windows.
    const auto unicode = folder / ravenemudsk_ui::from_utf8(
        "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3 Pok\xC3\xA9mon.gbc");
    { std::ofstream f(unicode); f << "test"; }
    const auto save = ravenemudsk_ui::sidecar(unicode, ".sav");
    assert(ravenemudsk_ui::utf8(save) == ravenemudsk_ui::utf8(unicode) + ".sav");
    { std::ofstream f(save); f << "save"; }
    assert(fs::exists(folder / ravenemudsk_ui::from_utf8(
        "\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3 Pok\xC3\xA9mon.gbc.sav")));
    assert(loaded.add(unicode));
    loaded.save();
    ravenemudsk_ui::Library reloaded(folder / "library.txt");
    reloaded.load();
    assert(reloaded.games().size() == 3 && reloaded.games()[2].path == loaded.games()[2].path);
    assert(reloaded.remove(unicode) && !reloaded.remove(unicode));
    assert(reloaded.games().size() == 2 && fs::exists(unicode));
    // Folders: an unusable first root falls back to the next one.
    { std::ofstream f(folder / "not-a-folder"); f << "x"; }
    const auto folders = ravenemudsk_ui::prepare_folders({folder / "not-a-folder" / "app", folder / "data"});
    assert(folders.saves == folder / "data" / "Sauvegardes" && fs::is_directory(folders.covers));
    // Saves found next to the ROM are copied into Sauvegardes, never moved.
    { std::ofstream f(ravenemudsk_ui::sidecar(gb, ".sav")); f << "progress"; }
    const auto sav = ravenemudsk_ui::save_path(folders, gb, ".sav");
    assert(sav == folders.saves / "Game One.GB.sav" && fs::exists(sav));
    assert(fs::exists(ravenemudsk_ui::sidecar(gb, ".sav")));
    assert(ravenemudsk_ui::save_path(folders, gba, ".rstate") == folders.saves / "Game_two.gba.rstate");
    // Covers: Jaquettes first, one image per game.
    { std::ofstream f(folder / "art.png"); f << "png"; }
    { std::ofstream f(folder / "art.JPG"); f << "jpg"; }
    assert(ravenemudsk_ui::install_cover(folders, gb, folder / "art.png") == folders.covers / "Game One.png");
    assert(ravenemudsk_ui::cover_candidates(folders, gb).front().parent_path() == folders.covers);
    assert(ravenemudsk_ui::install_cover(folders, gb, folder / "art.JPG") == folders.covers / "Game One.jpg");
    assert(!fs::exists(folders.covers / "Game One.png"));
    assert(ravenemudsk_ui::is_image(folder / "art.JPG") && !ravenemudsk_ui::is_image(gb));
    // Settings: key=value lines survive a reload, line breaks are flattened.
    ravenemudsk_ui::Settings settings(folder / "reglages.txt");
    settings.set("pseudo", "Pixel\nHero");
    settings.set_flag("tv_mode", true);
    settings.set("avatar_color", "3");
    settings.save();
    ravenemudsk_ui::Settings reread(folder / "reglages.txt");
    reread.load();
    assert(reread.get("pseudo") == "Pixel Hero" && reread.flag("tv_mode", false));
    assert(reread.number("avatar_color", 0) == 3 && reread.number("missing", 7) == 7);
    // Levels: 10 XP per achievement point, 1 XP per minute; level n starts at 50n(n-1).
    assert(ravenemudsk_ui::level_for(0, 0).level == 1);
    const auto level = ravenemudsk_ui::level_for(10, 754);  // 100 + 12 XP
    assert(level.level == 2 && level.xp == 112 && level.level_start == 100 && level.next_level == 300);
    assert(ravenemudsk_ui::level_for(30, 0).level == 3);
    // Names: trimmed, control characters dropped, length counted in characters.
    assert(ravenemudsk_ui::clean_name("  Zo\xC3\xA9\t ", 24) == "Zo\xC3\xA9");
    assert(ravenemudsk_ui::clean_name("\xC3\xA9\xC3\xA9\xC3\xA9", 2) == "\xC3\xA9\xC3\xA9");
    // Update check: strict versions, comparison and the GitHub tag field.
    const auto v030 = ravenemudsk_ui::parse_version("0.3.0");
    const auto v041 = ravenemudsk_ui::parse_version("v0.4.1");
    assert(v030 && v041 && ravenemudsk_ui::is_newer(*v041, *v030) && !ravenemudsk_ui::is_newer(*v030, *v041));
    assert(!ravenemudsk_ui::is_newer(*v030, *v030));
    assert(ravenemudsk_ui::is_newer(*ravenemudsk_ui::parse_version("0.10.0"), *ravenemudsk_ui::parse_version("0.9.9")));
    for (const char* bad : {"", "v", "1.2", "1.2.3.4", "1.2.x", "-1.0.0", "1.2.3 ", "1..3"})
        assert(!ravenemudsk_ui::parse_version(bad));
    assert(ravenemudsk_ui::latest_tag(R"({"url":"x","tag_name" : "v0.4.1","name":"RavenEmuDSK"})") == "v0.4.1");
    assert(!ravenemudsk_ui::latest_tag(R"({"message":"Not Found"})"));
    assert(ravenemudsk_ui::release_page(*v041) ==
           "https://github.com/GhostPunishR/RavenEmuDSK/releases/tag/v0.4.1");
    fs::remove_all(folder);
    std::cout << "RavenEmuDSK library tests passed\n";
    return 0;
}

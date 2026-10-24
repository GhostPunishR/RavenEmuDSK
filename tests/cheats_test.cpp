// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/saves/cheats.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs = std::filesystem;
using namespace ravenemudsk_ui;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
int main() {
    const auto folder = fs::temp_directory_path() /
        ("ravenemudsk-cheats-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup { fs::path folder; ~Cleanup() { std::error_code ignored; fs::remove_all(folder, ignored); } } cleanup{folder};
    // A code typed or pasted over several lines comes out on one.
    require(one_line("  82002A4C 0063\r\n\t82002A4E   0001 \n") == "82002A4C 0063 82002A4E 0001", "One line");
    require(one_line(" \n\t ").empty(), "Blank is empty");

    const auto file = folder / "Pokémon Rubis.gba.cheats";
    require(load_cheats(file).empty(), "No file, no cheats");
    save_cheats(file, {{"Argent infini", "82002A4C 0063\n82002A4E 0001", true},
                       {"Nom\tavec tabulation", "010238CD", false},
                       {"Vide", "  ", true}});
    const auto cheats = load_cheats(file);
    require(cheats.size() == 2, "Two cheats come back; an empty code is dropped");
    require(cheats[0].name == "Argent infini" && cheats[0].code == "82002A4C 0063 82002A4E 0001" && cheats[0].enabled,
            "First cheat, code on one line, on");
    require(cheats[1].name == "Nom avec tabulation" && cheats[1].code == "010238CD" && !cheats[1].enabled,
            "Second cheat: a tab in its name cannot split the line; off");
    require(!fs::exists(fs::path(file) += ".tmp"), "No temporary file left");
    // Lines that are not cheats are skipped.
    { std::ofstream out(file, std::ios::app); out << "garbage\r\n1\tCRLF\tABC-DEF-GHI\r\n"; }
    const auto mixed = load_cheats(file);
    require(mixed.size() == 3 && mixed[2].name == "CRLF" && mixed[2].code == "ABC-DEF-GHI", "Junk skipped, CRLF read");
    save_cheats(file, {});
    require(load_cheats(file).empty(), "An empty list empties the file");
    std::cout << "Cheat lists passed\n";
}

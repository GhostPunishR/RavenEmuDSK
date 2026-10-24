// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
// One cheat of a game's list, as the player entered it.
struct Cheat {
    std::string name;
    std::string code;   // one or more lines of the code, on one line, separated by spaces
    bool enabled = true;
};
// The code as typed, on one line: line breaks, tabs and runs of spaces become
// one space, the ends trimmed. RavenCore reads spaces, line breaks and '+'
// alike between the lines of a code.
std::string one_line(const std::string& text);
// A game's cheats, in <ROM>.cheats beside its saves: a line per cheat,
// "1<TAB>name<TAB>code" (0 when it is off). Unreadable lines are skipped; a
// missing file is an empty list.
std::vector<Cheat> load_cheats(const std::filesystem::path& file);
// Written whole, through a temporary file: never half a list.
void save_cheats(const std::filesystem::path& file, const std::vector<Cheat>& cheats);
} // namespace ravenemudsk_ui

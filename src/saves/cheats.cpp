// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/saves/cheats.hpp>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace ravenemudsk_ui {
std::string one_line(const std::string& text) {
    std::string out;
    bool space = false;
    for (const char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            space = !out.empty();
            continue;
        }
        if (space) out += ' ';
        space = false;
        out += c;
    }
    return out;
}

std::vector<Cheat> load_cheats(const std::filesystem::path& file) {
    std::vector<Cheat> cheats;
    std::ifstream in(file, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find('\t');
        const auto second = first == std::string::npos ? std::string::npos : line.find('\t', first + 1);
        if (second == std::string::npos) continue;
        Cheat cheat{line.substr(first + 1, second - first - 1), one_line(line.substr(second + 1)), line.substr(0, first) == "1"};
        if (!cheat.code.empty()) cheats.push_back(std::move(cheat));
    }
    return cheats;
}

void save_cheats(const std::filesystem::path& file, const std::vector<Cheat>& cheats) {
    auto temporary = file;
    temporary += ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& cheat : cheats) {
            // A name holds no tab or line break: they would split the line.
            std::string name;
            for (const char c : cheat.name) name += c == '\t' || c == '\n' || c == '\r' ? ' ' : c;
            out << (cheat.enabled ? '1' : '0') << '\t' << name << '\t' << one_line(cheat.code) << '\n';
        }
        out.flush();
        if (!out) throw std::runtime_error("Codes de triche non enregistrés");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Codes de triche non enregistrés");
#else
    std::filesystem::rename(temporary, file);
#endif
}
} // namespace ravenemudsk_ui

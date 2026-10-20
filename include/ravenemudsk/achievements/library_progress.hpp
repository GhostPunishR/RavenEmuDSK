// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace ravenemudsk_ra {
// "Mes jeux RetroAchievements": the account's progress on each recognized
// library game, read from the per-ROM cache, so it is available offline.
struct GameProgress {
    std::string path;  // Library path, UTF-8.
    std::string title; // Title given by RetroAchievements.
    int unlocked = 0, total = 0;
    unsigned points = 0, total_points = 0;
    bool hardcore = false; // Mode of the counters.
    std::int64_t last_played = 0;
};
enum class GameSort { Recent, Progress, Title };
inline constexpr std::array<const char*, 3> game_sort_labels{"Récemment joués", "Progression", "Nom"};
struct GamesSummary { int games = 0, mastered = 0, completed = 0, unlocked = 0, total = 0; };
// Every achievement unlocked: "Maîtrisé" with Hardcore counters, "Complété"
// with Softcore ones (Hardcore progress is then unknown); nullptr otherwise.
const char* completion_label(const GameProgress& game);
GamesSummary summarize(const std::vector<GameProgress>& games);
// "3 jeux · 1 maîtrisé · 2 complétés · 45 / 120 succès"; empty counts are left out.
std::string summary_text(const GamesSummary& summary);
// Stable sorts. Progress puts the closest to completion first.
void sort_games(std::vector<GameProgress>& games, GameSort sort);
} // namespace ravenemudsk_ra

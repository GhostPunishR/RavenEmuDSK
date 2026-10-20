// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/library_progress.hpp>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ra;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameProgress game(const char* title, int unlocked, int total, bool hardcore, std::int64_t played) {
    GameProgress g;
    g.path = std::string("/roms/") + title + ".gb"; g.title = title;
    g.unlocked = unlocked; g.total = total; g.hardcore = hardcore; g.last_played = played;
    return g;
}
// One letter per game, "Écho" read as E.
std::string order(const std::vector<GameProgress>& games) {
    std::string out;
    for (const auto& g : games) out += g.title == "Écho" ? 'E' : g.title[0];
    return out;
}
} // namespace

int main() {
    std::vector<GameProgress> games{
        game("Bravo", 10, 10, true, 300),  // Mastered.
        game("Alpha", 12, 40, false, 100), // 30 %.
        game("Delta", 5, 5, false, 0),     // Completed in Softcore, never played here.
        game("Écho", 9, 10, true, 200),    // 90 %, accented title.
        game("Charlie", 3, 10, false, 300)};
    require(std::string(completion_label(games[0])) == "Maîtrisé" && std::string(completion_label(games[2])) == "Complété" &&
            !completion_label(games[1]) && !completion_label(game("Vide", 0, 0, true, 0)),
            "Mastered needs Hardcore counters; a game without achievements is never complete");

    const auto summary = summarize(games);
    require(summary.games == 5 && summary.mastered == 1 && summary.completed == 1 &&
            summary.unlocked == 39 && summary.total == 75, "Summary counts");
    require(summary_text(summary) == "5 jeux \xC2\xB7 1 maîtrisé \xC2\xB7 1 complété \xC2\xB7 39 / 75 succès",
            "Summary sentence");
    GamesSummary plural{3, 2, 0, 4, 9};
    require(summary_text(plural) == "3 jeux \xC2\xB7 2 maîtrisés \xC2\xB7 4 / 9 succès", "Plurals and empty counts");
    require(summary_text(GamesSummary{1, 0, 0, 0, 3}) == "1 jeu \xC2\xB7 0 / 3 succès", "Singular");

    sort_games(games, GameSort::Title);
    require(order(games) == "ABCDE", "Titles sort without accents or case");
    sort_games(games, GameSort::Recent);
    require(order(games) == "BCEAD", "Recent: latest first, ties by title, never played last");
    sort_games(games, GameSort::Progress);
    require(order(games) == "BDEAC", "Progress: complete first, then closest to completion, ties by unlocked count");
    std::cout << "Library progress summary, mastery labels and sorts passed\n";
}

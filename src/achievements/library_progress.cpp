// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/library_progress.hpp>
#include <ravenemudsk/achievements/achievement_list.hpp>
#include <algorithm>
namespace ravenemudsk_ra {
namespace {
bool complete(const GameProgress& game) { return game.total > 0 && game.unlocked >= game.total; }
std::string count(int value, const char* one, const char* many) {
    return std::to_string(value) + ' ' + (value > 1 ? many : one);
}
} // namespace

const char* completion_label(const GameProgress& game) {
    if (!complete(game)) return nullptr;
    return game.hardcore ? "Maîtrisé" : "Complété";
}

GamesSummary summarize(const std::vector<GameProgress>& games) {
    GamesSummary summary;
    for (const auto& game : games) {
        ++summary.games;
        if (complete(game)) ++(game.hardcore ? summary.mastered : summary.completed);
        summary.unlocked += game.unlocked;
        summary.total += game.total;
    }
    return summary;
}

std::string summary_text(const GamesSummary& summary) {
    std::string text = count(summary.games, "jeu", "jeux");
    if (summary.mastered) text += " \xC2\xB7 " + count(summary.mastered, "maîtrisé", "maîtrisés");
    if (summary.completed) text += " \xC2\xB7 " + count(summary.completed, "complété", "complétés");
    return text + " \xC2\xB7 " + std::to_string(summary.unlocked) + " / " + std::to_string(summary.total) + " succès";
}

void sort_games(std::vector<GameProgress>& games, GameSort sort) {
    const auto by_title = [](const GameProgress& l, const GameProgress& r) {
        return fold_search_text(l.title) < fold_search_text(r.title);
    };
    std::stable_sort(games.begin(), games.end(), by_title);
    if (sort == GameSort::Recent)
        std::stable_sort(games.begin(), games.end(), [](const GameProgress& l, const GameProgress& r) {
            return l.last_played > r.last_played;
        });
    else if (sort == GameSort::Progress)
        std::stable_sort(games.begin(), games.end(), [](const GameProgress& l, const GameProgress& r) {
            // Cross-multiplied ratios: no rounding, and a game without achievements never divides by zero.
            const auto left = static_cast<std::int64_t>(l.unlocked) * std::max(r.total, 1);
            const auto right = static_cast<std::int64_t>(r.unlocked) * std::max(l.total, 1);
            return left != right ? left > right : l.unlocked > r.unlocked;
        });
}
} // namespace ravenemudsk_ra

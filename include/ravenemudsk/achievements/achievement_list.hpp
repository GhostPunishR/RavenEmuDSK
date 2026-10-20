// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/client.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace ravenemudsk_ra {
// The profile's list of a game's achievements: search, filters and sorting,
// kept free of ImGui so that these rules are tested on their own.
enum class AchievementFilter { All, Locked, Unlocked };
// Recommended keeps the SDK's grouping: recently unlocked, almost there,
// locked, then unlocked, each in the set's order.
enum class AchievementSort { Recommended, Progress, Date, Rarity };
inline constexpr std::array<const char*, 4> achievement_sort_labels{
    "Recommandé", "Progression", "Date de déblocage", "Rareté"};
struct AchievementCounts { int all = 0, locked = 0, unlocked = 0; };
// Lowercase and without French accents, so that "pecheur" finds "Pêcheur".
std::string fold_search_text(const std::string& text);
// Counts follow the search, so each filter announces what it would list.
AchievementCounts count_achievements(const std::vector<Achievement>& entries, const std::string& query);
// `hardcore` picks the rarity of the mode the profile shows.
std::vector<const Achievement*> list_achievements(const std::vector<Achievement>& entries,
    const std::string& query, AchievementFilter filter, AchievementSort sort, bool hardcore);
// "15 nov. 2023" in local time; empty for 0.
std::string format_unlock_date(std::int64_t unix_seconds);
// "12,3 %" with a French decimal comma; empty when the server sent none.
std::string format_rarity(float percent);
// "Manquable", "Progression" or "Victoire"; nullptr for a standard achievement.
const char* achievement_type_label(int type);
// "Obtenu par 3,1 % des joueurs en Hardcore, 12,3 % en Softcore"; empty without rarity.
std::string rarity_sentence(const Achievement& achievement);
} // namespace ravenemudsk_ra

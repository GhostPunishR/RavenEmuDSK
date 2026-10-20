// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/achievement_list.hpp>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ra;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
Achievement entry(const char* id, const char* title, bool unlocked, std::int64_t when = 0,
                  float percent = 0.f, float rarity = 0.f, float rarity_hardcore = 0.f, int type = 0) {
    Achievement a;
    a.id = id; a.title = title; a.description = "Description de " + std::string(title);
    a.unlocked = unlocked; a.unlock_time = when; a.measured_percent = percent;
    a.rarity = rarity; a.rarity_hardcore = rarity_hardcore; a.type = type;
    return a;
}
std::string ids(const std::vector<const Achievement*>& list) {
    std::string out;
    for (const auto* a : list) out += a->id;
    return out;
}
} // namespace

int main() {
    require(fold_search_text("Pêcheur ÉMÉRITE Œuvre Ça") == "pecheur emerite oeuvre ca", "Accents must fold");
    const std::vector<Achievement> entries{
        entry("1", "Premier pas", true, 1700000100, 100.f, 80.f, 40.f),
        entry("2", "Pêcheur confirmé", false, 0, 50.f, 12.f, 3.f, 1),
        entry("3", "Badge Roche", true, 1700000300, 100.f, 60.f, 0.f, 2),
        entry("4", "Maître de la Ligue", false, 0, 90.f, 0.f, 0.f, 3),
        entry("5", "Sans soin", false, 0, 0.f, 5.f, 1.f)};

    const auto all = count_achievements(entries, "");
    require(all.all == 5 && all.locked == 3 && all.unlocked == 2, "Counts of the unfiltered list");
    const auto searched = count_achievements(entries, "PECHEUR");
    require(searched.all == 1 && searched.locked == 1 && searched.unlocked == 0, "Counts must follow the search");
    require(ids(list_achievements(entries, "description de badge", AchievementFilter::All,
        AchievementSort::Recommended, false)) == "3", "Search must also read descriptions");

    require(ids(list_achievements(entries, "", AchievementFilter::Locked, AchievementSort::Recommended, false)) == "245",
        "Locked filter");
    require(ids(list_achievements(entries, "", AchievementFilter::Unlocked, AchievementSort::Recommended, false)) == "13",
        "Unlocked filter");
    require(ids(list_achievements(entries, "", AchievementFilter::All, AchievementSort::Progress, false)) == "42513",
        "Progress: locked first, closest to completion first, then unlocked in game order");
    require(ids(list_achievements(entries, "", AchievementFilter::All, AchievementSort::Date, false)) == "31245",
        "Date: latest unlock first, locked ones keep the game order");
    require(ids(list_achievements(entries, "", AchievementFilter::All, AchievementSort::Rarity, false)) == "52314",
        "Softcore rarity: rarest first, unknown last");
    require(ids(list_achievements(entries, "", AchievementFilter::All, AchievementSort::Rarity, true)) == "52134",
        "Hardcore rarity is used in Hardcore");

    require(format_unlock_date(1700049600) == "15 nov. 2023", "French short date");
    require(format_unlock_date(0).empty(), "No date for a locked achievement");
    require(format_rarity(12.34f) == "12,3 %" && format_rarity(.05f) == "moins de 0,1 %" && format_rarity(0.f).empty(),
        "Rarity uses a French decimal comma");
    require(rarity_sentence(entries[0]) == "Obtenu par 40,0 % des joueurs en Hardcore, 80,0 % en Softcore" &&
        rarity_sentence(entries[3]).empty() && rarity_sentence(entry("6", "x", false, 0, 0.f, 5.f)) ==
        "Obtenu par 5,0 % des joueurs en Softcore", "Rarity sentence");
    require(std::string(achievement_type_label(1)) == "Manquable" && std::string(achievement_type_label(2)) == "Progression" &&
        std::string(achievement_type_label(3)) == "Victoire" && !achievement_type_label(0), "Type labels");
    std::cout << "Achievement search, filters, counts, sorts, dates, rarity and types passed\n";
}

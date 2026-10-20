// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/achievements/achievement_list.hpp>
#include <rc_client.h>
#include <algorithm>
#include <cstdio>
#include <ctime>
namespace ravenemudsk_ra {
namespace {
// Base letter of each code point from U+00C0 to U+00FF, or 0 to keep it.
constexpr char latin1_base[64] = {
    'a','a','a','a','a','a', 0 ,'c','e','e','e','e','i','i','i','i',
     0 ,'n','o','o','o','o','o', 0 , 0 ,'u','u','u','u','y', 0 , 0 ,
    'a','a','a','a','a','a', 0 ,'c','e','e','e','e','i','i','i','i',
     0 ,'n','o','o','o','o','o', 0 , 0 ,'u','u','u','u','y', 0 ,'y'};
bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
bool matches(const Achievement& a, const std::string& folded_query) {
    return folded_query.empty() || contains(fold_search_text(a.title), folded_query) ||
        contains(fold_search_text(a.description), folded_query);
}
} // namespace

std::string fold_search_text(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        const auto next = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0u;
        if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
        else if ((c == 0xC3) && next >= 0x80 && next <= 0xBF && latin1_base[next - 0x80]) {
            out += latin1_base[next - 0x80];
            ++i;
        } else if (c == 0xC3 && (next == 0x86 || next == 0xA6)) { out += "ae"; ++i; }  // Æ æ
        else if (c == 0xC5 && (next == 0x92 || next == 0x93)) { out += "oe"; ++i; }    // Œ œ
        else out += static_cast<char>(c);
    }
    return out;
}

AchievementCounts count_achievements(const std::vector<Achievement>& entries, const std::string& query) {
    const auto folded = fold_search_text(query);
    AchievementCounts counts;
    for (const auto& a : entries) {
        if (!matches(a, folded)) continue;
        ++counts.all;
        ++(a.unlocked ? counts.unlocked : counts.locked);
    }
    return counts;
}

std::vector<const Achievement*> list_achievements(const std::vector<Achievement>& entries,
    const std::string& query, AchievementFilter filter, AchievementSort sort, bool hardcore) {
    const auto folded = fold_search_text(query);
    std::vector<const Achievement*> list;
    for (const auto& a : entries) {
        if ((filter == AchievementFilter::Locked && a.unlocked) ||
            (filter == AchievementFilter::Unlocked && !a.unlocked) || !matches(a, folded)) continue;
        list.push_back(&a);
    }
    // Stable sorts keep the game's own order between equal entries.
    switch (sort) {
    case AchievementSort::Recommended: break;
    case AchievementSort::Progress:
        std::stable_sort(list.begin(), list.end(), [](const Achievement* l, const Achievement* r) {
            if (l->unlocked != r->unlocked) return !l->unlocked;
            return !l->unlocked && l->measured_percent > r->measured_percent;
        });
        break;
    case AchievementSort::Date:
        std::stable_sort(list.begin(), list.end(), [](const Achievement* l, const Achievement* r) {
            return l->unlock_time > r->unlock_time;
        });
        break;
    case AchievementSort::Rarity: {
        // Rarest first; a rarity the server did not send goes last.
        const auto rarity = [hardcore](const Achievement* a) {
            const float value = hardcore ? a->rarity_hardcore : a->rarity;
            return value > 0.f ? value : 1000.f;
        };
        std::stable_sort(list.begin(), list.end(), [&](const Achievement* l, const Achievement* r) {
            return rarity(l) < rarity(r);
        });
        break;
    }
    }
    return list;
}

std::string format_unlock_date(std::int64_t unix_seconds) {
    if (unix_seconds <= 0) return {};
    static constexpr const char* months[] = {"janv.", "févr.", "mars", "avr.", "mai", "juin",
                                             "juil.", "août", "sept.", "oct.", "nov.", "déc."};
    const std::time_t time = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
#ifdef _WIN32
    if (localtime_s(&local, &time) != 0) return {};
#else
    if (!localtime_r(&time, &local)) return {};
#endif
    if (local.tm_mon < 0 || local.tm_mon > 11) return {};
    return std::to_string(local.tm_mday) + ' ' + months[local.tm_mon] + ' ' + std::to_string(local.tm_year + 1900);
}

std::string format_rarity(float percent) {
    if (!(percent > 0.f)) return {};
    if (percent < .1f) return "moins de 0,1 %";
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%.1f", static_cast<double>(std::min(percent, 100.f)));
    std::string text = buffer;
    std::replace(text.begin(), text.end(), '.', ',');
    return text + " %";
}

std::string rarity_sentence(const Achievement& a) {
    const auto softcore = format_rarity(a.rarity), hardcore = format_rarity(a.rarity_hardcore);
    if (softcore.empty() && hardcore.empty()) return {};
    std::string text = "Obtenu par ";
    if (!hardcore.empty()) text += hardcore + " des joueurs en Hardcore";
    if (!hardcore.empty() && !softcore.empty()) text += ", " + softcore + " en Softcore";
    else if (!softcore.empty()) text += softcore + " des joueurs en Softcore";
    return text;
}

const char* achievement_type_label(int type) {
    switch (type) {
    case RC_CLIENT_ACHIEVEMENT_TYPE_MISSABLE: return "Manquable";
    case RC_CLIENT_ACHIEVEMENT_TYPE_PROGRESSION: return "Progression";
    case RC_CLIENT_ACHIEVEMENT_TYPE_WIN: return "Victoire";
    default: return nullptr;
    }
}
} // namespace ravenemudsk_ra

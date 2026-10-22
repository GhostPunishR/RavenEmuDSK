// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_preferences.hpp>
#include <ravenemudsk/ui/settings.hpp>
#include <algorithm>
namespace ravenemudsk_ui {
GamePreferences game_preferences(const Settings& settings) {
    GamePreferences prefs;
    prefs.hardcore = settings.flag("ra_hardcore", false);
    prefs.unlock_sound = settings.flag("ra_unlock_sound", true);
    prefs.volume = settings.flag("mute", false) ? 0 : std::clamp(settings.number("volume", 100), 0, 100);
    prefs.smooth_image = settings.flag("smooth_image", false);
    prefs.keyboard.load(settings);
    prefs.pads.load(settings);
    return prefs;
}
} // namespace ravenemudsk_ui

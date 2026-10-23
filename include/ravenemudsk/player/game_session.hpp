// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/launcher/app_shell.hpp>

// One game, from its ROM to its last battery save: its window (or the TV
// mode's), the frame loop, saves and states, the pause menu,
// RetroAchievements and the wireless link cable.
namespace ravenemudsk_player {
// Plays the chosen game until the player quits, then saves. Throws when the
// game cannot start or stops on an error; the launcher shows the message.
void play(const ravenemudsk_ui::LaunchRequest& request);
} // namespace ravenemudsk_player

// SPDX-License-Identifier: AGPL-3.0-only
// The settings file, read by the launcher and by a game when it starts.
#include "shell.hpp"

namespace ravenemudsk_ui {
namespace shell {
fs::path settings_file() { return data_file().parent_path() / "reglages.txt"; }
} // namespace shell
using namespace shell;
bool remember_hardcore(bool enabled){
    try{Settings settings(settings_file());settings.load();
        settings.set_flag("ra_hardcore",enabled);settings.save();return true;
    }catch(...){return false;}
}
GamePreferences game_preferences() {
    // An unreadable file leaves every default, as a missing one does.
    Settings settings(settings_file());
    try { settings.load(); } catch (...) { return {}; }
    return game_preferences(settings);
}
} // namespace ravenemudsk_ui

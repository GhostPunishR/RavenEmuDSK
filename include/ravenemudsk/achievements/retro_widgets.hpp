// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/ui/theme.hpp>
namespace ravenemudsk_ui {
// `tv`: drawn in the TV achievements panel, with its large font.
void draw_leaderboards(ravenemudsk_ra::Client& client,const ravenemudsk_ra::Snapshot& game,const Fonts& fonts,bool tv=false);
}

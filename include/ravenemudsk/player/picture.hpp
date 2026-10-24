// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ravenemudsk_ui {
// The picture options of Réglages > Affichage, as RavenEmu Android and the
// RavenCore Libretro cores have them.
enum class FrameBlending { off, mix, ghosting };

// A color LCD's look for the Game Boy Color and the Game Boy Advance, the same
// as RavenEmu Android's: about 28 % of desaturation toward the Rec. 709
// luminance, then the 1.15 gamma of a reflective screen. ARGB8888 in, RGB out
// (alpha 0xFF).
std::uint32_t lcd_corrected(std::uint32_t argb);

// The core's frame as it is shown: the LCD correction, then the blending of
// frames. "Mélange" shows the mean of the last two frames, so that an object
// drawn every other frame stays visible, half transparent; "Rémanence" keeps
// 3/8 of the picture shown, as a slow LCD does. The history starts over on
// reset() and when the options change.
class PictureFilter {
public:
    void configure(bool lcd, FrameBlending blending);
    // Writes `count` shown pixels from the core's `frame` into `shown`.
    void apply(const std::uint32_t* frame, std::uint32_t* shown, std::size_t count);
    void reset() { history_.clear(); }
private:
    bool lcd_ = false;
    FrameBlending blending_ = FrameBlending::off;
    std::vector<std::uint32_t> history_;
};

// Where the w x h picture goes in an out_w x out_h window: as large as fits,
// centered; with `integer`, a whole number of times (every pixel the same
// size), or as large as fits when not even once does.
SDL_Rect picture_rect(int out_w, int out_h, int w, int h, bool integer);
} // namespace ravenemudsk_ui

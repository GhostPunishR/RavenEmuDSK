// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/player/picture.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ui;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
int main() {
    // LCD correction: pure red becomes 0xBF0909, the value RavenEmu Android
    // and the Libretro cores give; black and white stay; opaque whatever the alpha.
    require(lcd_corrected(0x00ff0000) == 0xffbf0909, "Pure red, as on Android");
    require(lcd_corrected(0x00000000) == 0xff000000 && lcd_corrected(0x00ffffff) == 0xffffffff, "Black and white stay");
    const auto gray = lcd_corrected(0x00808080);
    require((gray >> 16 & 0xff) == (gray >> 8 & 0xff) && (gray >> 8 & 0xff) == (gray & 0xff) && (gray & 0xff) < 0x80,
            "A gray stays gray, a little darker");

    PictureFilter filter;
    const std::array<std::uint32_t, 2> white{0x00ffffff, 0x00ffffff}, black{0x00000000, 0x00000000};
    std::array<std::uint32_t, 2> shown{};
    filter.apply(white.data(), shown.data(), 2);
    require(shown[0] == 0xffffffff, "Untouched without options, made opaque");
    filter.configure(false, FrameBlending::mix);
    filter.apply(white.data(), shown.data(), 2);
    filter.apply(black.data(), shown.data(), 2);
    require(shown[0] == 0xff808080, "Mélange: the mean of the last two frames");
    filter.apply(black.data(), shown.data(), 2);
    require(shown[0] == 0xff000000, "Two black frames: black");
    filter.configure(false, FrameBlending::ghosting);
    filter.apply(white.data(), shown.data(), 2);
    filter.apply(black.data(), shown.data(), 2);
    require(shown[0] == 0xff606060, "Rémanence: 3/8 of the picture shown stays");
    filter.apply(black.data(), shown.data(), 2);
    require(shown[0] == 0xff242424, "and fades frame after frame");
    filter.configure(false, FrameBlending::mix);
    filter.apply(black.data(), shown.data(), 2);
    require(shown[0] == 0xff000000, "A new option starts its history over");
    filter.reset();
    filter.configure(true, FrameBlending::off);
    const std::array<std::uint32_t, 2> red{0x00ff0000, 0x00ff0000};
    filter.apply(red.data(), shown.data(), 2);
    require(shown[1] == 0xffbf0909, "Correction alone");

    // Picture placement: fitted, or a whole number of times.
    auto r = picture_rect(1280, 720, 240, 160, false);
    require(r.w == 1080 && r.h == 720 && r.x == 100 && r.y == 0, "Fitted: as large as fits");
    r = picture_rect(1280, 720, 240, 160, true);
    require(r.w == 960 && r.h == 640 && r.x == 160 && r.y == 40, "Integer: four times, centered");
    r = picture_rect(1920, 1080, 160, 144, true);
    require(r.w == 1120 && r.h == 1008, "Game Boy on 1080p: seven times");
    r = picture_rect(200, 120, 240, 160, true);
    require(r.w == 180 && r.h == 120, "Smaller than the console: fitted anyway");
    r = picture_rect(0, 720, 240, 160, true);
    require(r.w == 0 && r.h == 0, "No window, no picture");
    std::cout << "LCD correction, frame blending and integer scaling passed\n";
}

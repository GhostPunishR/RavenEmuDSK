// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/player/picture.hpp>
#include <algorithm>
#include <array>
#include <cmath>
namespace ravenemudsk_ui {
namespace {
const std::array<std::uint8_t, 256> lcd_gamma = [] {
    std::array<std::uint8_t, 256> table{};
    for (std::size_t value = 0; value < table.size(); ++value)
        table[value] = static_cast<std::uint8_t>(std::lround(255.0 * std::pow(static_cast<double>(value) / 255.0, 1.15)));
    return table;
}();
} // namespace

std::uint32_t lcd_corrected(std::uint32_t argb) {
    const int r = static_cast<int>((argb >> 16U) & 255U);
    const int g = static_cast<int>((argb >> 8U) & 255U);
    const int b = static_cast<int>(argb & 255U);
    const int luma = (54 * r + 183 * g + 19 * b) >> 8;
    // 72/256 of the way to the luminance: a mix, always within 0-255.
    const auto channel = [luma](int value) -> std::uint32_t {
        return lcd_gamma[static_cast<std::size_t>(value + (((luma - value) * 72) >> 8))];
    };
    return 0xff000000U | channel(r) << 16U | channel(g) << 8U | channel(b);
}

void PictureFilter::configure(bool lcd, FrameBlending blending) {
    if (lcd != lcd_ || blending != blending_) history_.clear();
    lcd_ = lcd;
    blending_ = blending;
}

void PictureFilter::apply(const std::uint32_t* frame, std::uint32_t* shown, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) shown[i] = lcd_ ? lcd_corrected(frame[i]) : frame[i] | 0xff000000U;
    if (blending_ == FrameBlending::off) return;
    if (history_.size() != count) history_.assign(shown, shown + count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto current = shown[i];
        const auto previous = history_[i];
        std::uint32_t blended = 0xff000000U;
        for (unsigned shift = 0; shift < 24; shift += 8) {
            const auto now = (current >> shift) & 0xffU;
            const auto before = (previous >> shift) & 0xffU;
            blended |= (blending_ == FrameBlending::mix ? (now + before + 1) / 2 : (now * 5 + before * 3 + 4) / 8) << shift;
        }
        shown[i] = blended;
        history_[i] = blending_ == FrameBlending::mix ? current : blended;
    }
}

SDL_Rect picture_rect(int out_w, int out_h, int w, int h, bool integer) {
    if (w <= 0 || h <= 0 || out_w <= 0 || out_h <= 0) return {0, 0, 0, 0};
    double factor = std::min(static_cast<double>(out_w) / w, static_cast<double>(out_h) / h);
    if (integer && factor >= 1.0) factor = std::floor(factor);
    const int pw = static_cast<int>(w * factor), ph = static_cast<int>(h * factor);
    return {(out_w - pw) / 2, (out_h - ph) / 2, pw, ph};
}
} // namespace ravenemudsk_ui

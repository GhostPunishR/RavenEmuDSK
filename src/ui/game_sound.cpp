// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_sound.hpp>
#include <algorithm>
#include <cmath>
namespace ravenemudsk_ui {
std::vector<std::int16_t> unlock_chime(int rate) {
    if (rate <= 0) return {};
    // G5, C6, E6 then G6, each struck 80 ms after the previous one.
    constexpr double notes[] = {783.99, 1046.50, 1318.51, 1567.98};
    constexpr double step = .08, ring = .55, attack = .004, release = .05;
    constexpr double tau = 6.283185307179586;
    const auto frames = static_cast<std::size_t>((step * 3 + ring) * rate);
    std::vector<double> mono(frames, 0.);
    for (std::size_t n = 0; n < std::size(notes); ++n) {
        const auto start = static_cast<std::size_t>(static_cast<double>(n) * step * rate);
        for (std::size_t i = start; i < frames; ++i) {
            const double t = static_cast<double>(i - start) / rate;
            if (t >= ring) break;
            // Struck, then ringing out; a short release so it ends without a click.
            const double envelope = std::min(1., t / attack) * std::exp(-t / .16) *
                                    std::min(1., (ring - t) / release);
            const double f = notes[n];
            mono[i] += envelope * (std::sin(tau * f * t) + .35 * std::sin(tau * 2. * f * t) +
                                   .12 * std::sin(tau * 3. * f * t));
        }
    }
    double peak = 0.;
    for (const double v : mono) peak = std::max(peak, std::abs(v));
    // A quarter of full scale: heard over the game, never harsh.
    const double gain = peak > 0. ? 8000. / peak : 0.;
    std::vector<std::int16_t> pcm(frames * 2);
    for (std::size_t i = 0; i < frames; ++i)
        pcm[2 * i] = pcm[2 * i + 1] = static_cast<std::int16_t>(std::lround(mono[i] * gain));
    return pcm;
}
void scale_volume(std::int16_t* pcm, std::size_t count, int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == 100) return;
    for (std::size_t i = 0; i < count; ++i) pcm[i] = static_cast<std::int16_t>(pcm[i] * percent / 100);
}
void SoundMixer::mix(std::int16_t* out, std::size_t count) {
    if (!playing()) return;
    const std::size_t n = std::min(count, sound_->size() - position_);
    for (std::size_t i = 0; i < n; ++i) {
        const int sum = out[i] + (*sound_)[position_ + i];
        out[i] = static_cast<std::int16_t>(std::clamp(sum, -32768, 32767));
    }
    position_ += n;
}
} // namespace ravenemudsk_ui

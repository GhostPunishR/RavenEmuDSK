// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/audio/av_sync.hpp>
#include <algorithm>
#include <cmath>
namespace ravenemudsk_ui {
namespace {
// Catmull-Rom between y1 and y2, at t in [0, 1).
double hermite(double y0, double y1, double y2, double y3, double t) {
    const double c1 = .5 * (y2 - y0);
    const double c2 = y0 - 2.5 * y1 + 2. * y2 - .5 * y3;
    const double c3 = .5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}
} // namespace

void Resampler::reset() {
    // One silent frame before the stream: the first output frame is the first
    // input frame, interpolated with silence before it.
    pending_.assign(2, 0);
    position_ = 1.0;
}

void Resampler::process(const std::int16_t* in, std::size_t frames, double ratio, std::vector<std::int16_t>& out) {
    pending_.insert(pending_.end(), in, in + frames * 2);
    if (!(ratio > 0.)) return;
    const double step = 1. / ratio;
    const std::size_t count = pending_.size() / 2;
    while (position_ + 2. < static_cast<double>(count)) {
        const auto i = static_cast<std::size_t>(position_);
        const double t = position_ - static_cast<double>(i);
        for (std::size_t c = 0; c < 2; ++c) {
            const auto at = [&](std::size_t frame) { return static_cast<double>(pending_[frame * 2 + c]); };
            const double v = hermite(at(i - 1), at(i), at(i + 1), at(i + 2), t);
            out.push_back(static_cast<std::int16_t>(std::clamp(std::lround(v), -32768L, 32767L)));
        }
        position_ += step;
    }
    // Keep the frame before the next position, and what follows; a position
    // beyond the input (a ratio below 1/2) waits for the frames to come.
    const auto drop = std::min(static_cast<std::size_t>(position_) - 1, count);
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(drop * 2));
    position_ -= static_cast<double>(drop);
}

double rate_correction(double queued, double target, double max_delta) {
    if (!(target > 0.)) return 1.;
    const double direction = std::clamp((target - queued) / target, -1., 1.);
    return 1. + max_delta * direction;
}

void FramePacer::presented(double seconds, double busy) {
    if (!(game_fps_ > 0.) || !(seconds > 0.) || seconds > 1.5 / game_fps_) return;
    if (!(busy >= .5 / game_fps_)) {
        if (++unpaced_ >= 3) {
            locked_ = false;
            samples_ = 0;
            interval_ = 0;
        }
        return;
    }
    unpaced_ = 0;
    // A running mean over about half a second.
    interval_ = interval_ > 0 ? interval_ + (seconds - interval_) / 32. : seconds;
    if (samples_ < 60) ++samples_;
    const double skew = std::abs(game_fps_ * interval_ - 1.);
    if (samples_ < 60) locked_ = false;
    else if (skew <= .05) locked_ = true;
    else if (skew > .06) locked_ = false;
}
} // namespace ravenemudsk_ui

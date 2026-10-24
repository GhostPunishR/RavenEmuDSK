// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/motion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
namespace ravenemudsk_ui {
namespace {
constexpr float gravity = 9.80665f;
constexpr float half_turn_per_second = 3.14159265f;
int clamp_axis(int value) { return std::clamp(value, -32768, 32767); }
// -1..1 to -32768..32767, rounded; NaN gives 0.
int to_axis(float unit) {
    if (!(unit == unit)) return 0;
    return clamp_axis(static_cast<int>(std::lround(unit * 32768.f)));
}
// A stick axis past its dead zone, back to the full range.
int past_dead_zone(int value) {
    const int magnitude = std::min(std::abs(value), 32767);
    if (magnitude <= MotionInput::stick_dead_zone) return 0;
    const int scaled = (magnitude - MotionInput::stick_dead_zone) * 32767 / (32767 - MotionInput::stick_dead_zone);
    return value < 0 ? -scaled : scaled;
}
} // namespace

void MotionInput::recenter() {
    samples_ = 0;
    std::fill(std::begin(level_), std::end(level_), 0.f);
    std::fill(std::begin(drift_), std::end(drift_), 0.f);
}

void MotionInput::sense(const float acceleration[3], const float rotation[3]) {
    std::copy(acceleration, acceleration + 3, acceleration_);
    std::copy(rotation, rotation + 3, rotation_);
    sensed_ = true;
    if (samples_ >= calibration_samples) return;
    for (int axis = 0; axis < 3; ++axis) {
        level_[axis] += acceleration[axis] / calibration_samples;
        drift_[axis] += rotation[axis] / calibration_samples;
    }
    ++samples_;
}

void MotionInput::stick(int x, int y) {
    stick_x_ = past_dead_zone(x);
    stick_y_ = past_dead_zone(y);
}

Motion MotionInput::reading() const {
    // Stick right: the right side down, or clockwise; stick up (negative y):
    // the top edge away. A full stick is a full tilt, as in Libretro.
    Motion motion{stick_x_, clamp_axis(-stick_y_), stick_x_};
    if (!sensed_ || samples_ < calibration_samples) return motion;
    // Away from the level position: the right side going down turns the
    // measured "up" toward the left (x falls); the top edge going away turns
    // it toward the player (z grows).
    const float x = (acceleration_[0] - level_[0]) / gravity;
    const float z = (acceleration_[2] - level_[2]) / gravity;
    motion.tilt_x = clamp_axis(motion.tilt_x + to_axis(-x));
    motion.tilt_y = clamp_axis(motion.tilt_y + to_axis(z));
    // Around the axis toward the player: counterclockwise positive for SDL,
    // clockwise for the cartridge.
    motion.rotation = clamp_axis(motion.rotation + to_axis(-(rotation_[2] - drift_[2]) / half_turn_per_second));
    return motion;
}
} // namespace ravenemudsk_ui

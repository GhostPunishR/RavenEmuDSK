// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/input/motion.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ui;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
static constexpr float g = 9.80665f, pi = 3.14159265f;
static bool near(int value, int expected) { return std::abs(value - expected) <= 2; }

// The controller held level, then `samples` readings of `acceleration` and `rotation`.
static void feed(MotionInput& motion, float ax, float ay, float az, float gz, int samples = 1) {
    const float acceleration[3] = {ax, ay, az}, rotation[3] = {0.f, 0.f, gz};
    for (int i = 0; i < samples; ++i) motion.sense(acceleration, rotation);
}

static void stick_tilts_and_turns() {
    MotionInput motion;
    auto m = motion.reading();
    require(m.tilt_x == 0 && m.tilt_y == 0 && m.rotation == 0, "At rest, nothing");
    motion.stick(32767, 0);
    m = motion.reading();
    require(m.tilt_x == 32767 && m.rotation == 32767 && m.tilt_y == 0, "Stick right: right side down, clockwise");
    motion.stick(0, -32768);
    require(motion.reading().tilt_y == 32767, "Stick up: the top edge away");
    motion.stick(-32768, 32767);
    m = motion.reading();
    require(m.tilt_x == -32767 && m.tilt_y == -32767 && m.rotation == -32767, "Stick left and down");
    motion.stick(MotionInput::stick_dead_zone, -MotionInput::stick_dead_zone);
    m = motion.reading();
    require(m.tilt_x == 0 && m.tilt_y == 0, "A resting stick's wobble is ignored");
    motion.stick((32767 + MotionInput::stick_dead_zone) / 2, 0);
    require(near(motion.reading().tilt_x, 16383), "Half the travel past the dead zone is half a tilt");
}

static void accelerometer_tilts_from_where_it_was_held() {
    MotionInput motion;
    motion.recenter();
    // Until the level position is set, the sensors add nothing, however the
    // controller is held.
    feed(motion, -g * .5f, g, 0.f, 0.f, MotionInput::calibration_samples - 1);
    require(motion.reading().tilt_x == 0, "Nothing while the level position is measured");
    // Held level: "up" along y.
    motion.recenter();
    feed(motion, 0.f, g, 0.f, 0.f, MotionInput::calibration_samples);
    auto m = motion.reading();
    require(m.tilt_x == 0 && m.tilt_y == 0, "Level is not tilted");
    // Right side down by 30 degrees: "up" leans toward the left.
    feed(motion, -g * .5f, g * .866f, 0.f, 0.f);
    require(near(motion.reading().tilt_x, 16384), "Right side down: half a tilt to the right");
    // Top edge away by 30 degrees: "up" leans toward the player.
    feed(motion, 0.f, g * .866f, g * .5f, 0.f);
    m = motion.reading();
    require(m.tilt_x == 0 && near(m.tilt_y, 16384), "Top edge away: half a tilt forward");
    // Held 30 degrees toward the player at the start: that is level.
    motion.recenter();
    feed(motion, 0.f, g * .866f, -g * .5f, 0.f, MotionInput::calibration_samples);
    require(motion.reading().tilt_y == 0, "The starting position counts as level");
    feed(motion, 0.f, g, 0.f, 0.f);
    require(near(motion.reading().tilt_y, 16384), "Lowering the top edge from there tilts forward");
    // Stick and sensors add up, within range.
    motion.stick(32767, 0);
    feed(motion, -g, 0.f, -g * .5f, 0.f);
    require(motion.reading().tilt_x == 32767, "Stick and sensors add up, clamped");
}

static void gyroscope_turns_without_drifting() {
    MotionInput motion;
    motion.recenter();
    // A gyroscope at rest reads a little: that drift is measured, then removed.
    feed(motion, 0.f, g, 0.f, .02f, MotionInput::calibration_samples);
    require(motion.reading().rotation == 0, "No turn at rest");
    // Half a turn per second, clockwise as the player sees it: negative for SDL.
    feed(motion, 0.f, g, 0.f, .02f - pi);
    require(motion.reading().rotation == 32767, "Half a turn per second: full scale");
    feed(motion, 0.f, g, 0.f, .02f + pi / 4.f);
    require(near(motion.reading().rotation, -8192), "An eighth of a turn per second, counterclockwise");
    const float nan[3] = {std::nanf(""), std::nanf(""), std::nanf("")};
    motion.sense(nan, nan);
    const auto m = motion.reading();
    require(m.tilt_x == 0 && m.tilt_y == 0 && m.rotation == 0, "A broken reading is ignored");
}

int main() {
    stick_tilts_and_turns();
    accelerometer_tilts_from_where_it_was_held();
    gyroscope_turns_without_drifting();
    std::cout << "Controller tilt and rotation passed\n";
}

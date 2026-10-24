// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
namespace ravenemudsk_ui {
// What a cartridge's motion sensors read, on RavenCore's scales
// (-32768..32767): the tilt per axis (x when the right side goes down, y when
// the top edge moves away from the player; full scale at 1 g) and the
// rotation speed (clockwise as the player sees it; full scale at half a turn
// per second).
struct Motion {
    int tilt_x = 0, tilt_y = 0, rotation = 0;
};

// The motion sensors of Kirby Tilt 'n' Tumble, Yoshi Topsy-Turvy or WarioWare:
// Twisted!, fed from a controller: its left stick, plus its accelerometer and
// gyroscope when it has them (DualShock 4, DualSense, Switch Pro, Joy-Con).
// Same scales as RavenEmu Android and RavenCore's Libretro cores.
class MotionInput {
public:
    // How the controller is held now counts as level: the next few readings
    // (a tenth of a second) set that position and the gyroscope's drift at
    // rest. Until then the sensors add nothing.
    void recenter();
    // SDL's readings along the controller held in front of you (x to the
    // right, y up, z toward the player): acceleration in m/s², gravity
    // included, and rotation in rad/s, counterclockwise positive.
    void sense(const float acceleration[3], const float rotation[3]);
    // The left stick, as SDL gives it (y down).
    void stick(int x, int y);
    Motion reading() const;
    static constexpr int calibration_samples = 6;
    // Past a resting stick's wobble; the rest of the travel is rescaled.
    static constexpr int stick_dead_zone = 4000;
private:
    int stick_x_ = 0, stick_y_ = 0;
    bool sensed_ = false;
    float acceleration_[3] = {}, rotation_[3] = {};
    // The level position and the drift, summed until enough readings arrive.
    int samples_ = 0;
    float level_[3] = {}, drift_[3] = {};
};
} // namespace ravenemudsk_ui

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ravenemudsk_ui {
// Picture and sound in step on the desktop. The screen paces the game when its
// refresh is close to the console's (60 Hz against 59.73 Hz): one emulated
// frame per refresh, so no frame is ever shown twice. The sound, produced a
// little faster then, is resampled to the audio device's rate, and nudged by
// at most 0.5 % so that the device's queue stays near its target: it neither
// runs dry (crackles) nor grows (lag). This is dynamic rate control, as
// described by Hans-Kristian Arntzen for RetroArch; the code is RavenEmu's.

// Interleaved stereo PCM16 resampler (cubic Hermite interpolation). It keeps
// its phase and the last input frames across calls, so a stream cut in any
// pieces comes out the same.
class Resampler {
public:
    Resampler() { reset(); }
    // Appends to `out` the `frames` stereo frames of `in`, stretched by
    // `ratio` output frames per input frame (1 keeps them; above 1, more).
    // The last two input frames wait for the next call.
    void process(const std::int16_t* in, std::size_t frames, double ratio, std::vector<std::int16_t>& out);
    // Forgets the stream: after the sound has been cut (pause, cleared queue).
    void reset();
private:
    std::vector<std::int16_t> pending_;
    double position_ = 1.0;
};

// The rate correction for `queued` seconds of sound waiting in the device
// against `target`: 1 + `max_delta` when it is empty, 1 - `max_delta` when it
// holds twice the target, in proportion between.
double rate_correction(double queued, double target, double max_delta = 0.005);

// Measures the screen's refresh from the time between two shown pictures, and
// says when it is close enough to the game's to pace it.
class FramePacer {
public:
    explicit FramePacer(double game_fps) : game_fps_(game_fps) {}
    // The time between the last two pictures, in seconds, while the game runs,
    // and the part of it not spent in RavenEmu's own sleeps. When the screen
    // paces the pictures, waiting for it fills the interval; a picture shown
    // at once (no vsync, hidden window) means it does not, and three in a row
    // stop the pacing. Pauses and stalls (over 1.5 frames) are not measured.
    void presented(double seconds, double busy);
    // One emulated frame per refresh: the screen's rate is within 5 % of the
    // game's (RetroArch's default skew), measured over a second at least.
    bool locked() const { return locked_; }
    // The screen's refresh rate, in Hz; 0 until measured.
    double refresh() const { return interval_ > 0 ? 1.0 / interval_ : 0.0; }
    // Output frames per input frame for the sound: the game runs at the
    // screen's rate while locked, so its sound is played that much slower.
    double audio_ratio() const { return locked_ ? game_fps_ / refresh() : 1.0; }
private:
    double game_fps_;
    double interval_ = 0;
    int samples_ = 0;
    int unpaced_ = 0;
    bool locked_ = false;
};
} // namespace ravenemudsk_ui

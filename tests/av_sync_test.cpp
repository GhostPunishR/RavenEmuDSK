// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/audio/av_sync.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace ravenemudsk_ui;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
static constexpr double game_fps = 59.7275;
static constexpr int rate = 32768;

// Stereo frames whose left and right differ, so a swapped channel shows.
static std::vector<std::int16_t> ramp(std::size_t frames) {
    std::vector<std::int16_t> pcm(frames * 2);
    for (std::size_t i = 0; i < frames; ++i) {
        pcm[2 * i] = static_cast<std::int16_t>(i * 7 % 20000);
        pcm[2 * i + 1] = static_cast<std::int16_t>(-static_cast<int>(i * 3 % 20000));
    }
    return pcm;
}

static void resampler_keeps_the_stream_at_ratio_one() {
    const auto input = ramp(150);
    Resampler resampler;
    std::vector<std::int16_t> out;
    resampler.process(input.data(), 100, 1., out);
    // The last two frames wait for the next ones.
    require(out.size() == 98 * 2, "98 of 100 frames out at once");
    resampler.process(input.data() + 200, 50, 1., out);
    require(out.size() == 148 * 2, "The rest follows");
    require(std::equal(out.begin(), out.end(), input.begin()), "Same samples, both channels, at ratio 1");
}

static void resampler_ignores_how_the_stream_is_cut() {
    const auto input = ramp(4000);
    Resampler whole, pieces;
    std::vector<std::int16_t> a, b;
    whole.process(input.data(), 4000, 1.003, a);
    std::size_t at = 0;
    for (const std::size_t size : {1u, 7u, 548u, 549u, 2u, 1000u}) {
        pieces.process(input.data() + at * 2, size, 1.003, b);
        at += size;
    }
    pieces.process(input.data() + at * 2, 4000 - at, 1.003, b);
    require(a == b, "Pieces give what the whole stream gives");
}

static void resampler_stretches_by_the_ratio() {
    const std::vector<std::int16_t> second(rate * 2, 1000);
    for (const double ratio : {game_fps / 60., 1.005, .995, .25, 3.}) {
        Resampler resampler;
        std::vector<std::int16_t> out;
        resampler.process(second.data(), rate, ratio, out);
        const double frames = static_cast<double>(out.size() / 2);
        require(std::abs(frames - rate * ratio) <= 3. * std::max(1., ratio), "Output length follows the ratio");
        // A constant stays constant, past the silent frame it starts after.
        require(std::all_of(out.begin() + 8, out.end(), [](std::int16_t s) { return s == 1000; }),
                "A constant level is kept");
    }
}

static void resampler_follows_a_tone() {
    // A 1 kHz tone, 32 samples per period, read 0.5 % slower.
    constexpr double tau = 6.283185307179586, f = 1000., amplitude = 16000., ratio = .995;
    std::vector<std::int16_t> tone(rate * 2);
    for (int i = 0; i < rate; ++i)
        tone[2 * i] = tone[2 * i + 1] = static_cast<std::int16_t>(std::lround(amplitude * std::sin(tau * f * i / rate)));
    Resampler resampler;
    std::vector<std::int16_t> out;
    resampler.process(tone.data(), rate, ratio, out);
    double worst = 0.;
    for (std::size_t k = 4; k < out.size() / 2; ++k) {
        const double ideal = amplitude * std::sin(tau * f * (static_cast<double>(k) / ratio) / rate);
        worst = std::max(worst, std::abs(out[2 * k] - ideal));
    }
    // Under 0.5 % of the tone's amplitude: far below what one hears.
    require(worst < amplitude * .005, "The tone is followed closely");
}

static void resampler_never_wraps_around() {
    // Full-scale square: the interpolation overshoots at each edge, and must
    // clamp there instead of wrapping to the other sign.
    std::vector<std::int16_t> square(4000 * 2);
    for (std::size_t i = 0; i < 4000; ++i) square[2 * i] = square[2 * i + 1] = (i / 100) % 2 ? -32768 : 32767;
    Resampler resampler;
    std::vector<std::int16_t> out;
    resampler.process(square.data(), 4000, .99, out);
    for (std::size_t k = 0; k < out.size() / 2; ++k) {
        const double position = static_cast<double>(k) / .99;
        const double from_edge = std::abs(position / 100. - std::round(position / 100.)) * 100.;
        if (k < 4 || from_edge < 3.) continue;
        const bool low = static_cast<int>(position / 100.) % 2;
        require(out[2 * k] == (low ? -32768 : 32767), "Flat parts stay at full scale");
    }
}

static void resampler_starts_again_after_reset() {
    const auto input = ramp(100);
    Resampler resampler;
    std::vector<std::int16_t> first, again;
    resampler.process(input.data(), 100, 1., first);
    resampler.reset();
    resampler.process(input.data(), 100, 1., again);
    require(first == again, "A reset stream starts from scratch");
    std::vector<std::int16_t> none;
    resampler.process(input.data(), 100, 0., none);
    require(none.empty(), "No ratio, no sound");
}

static void correction_follows_the_queue() {
    require(rate_correction(0., .05) == 1.005, "Empty: 0.5 % more");
    require(rate_correction(.05, .05) == 1., "At the target: unchanged");
    require(std::abs(rate_correction(.075, .05) - .9975) < 1e-12, "Half over: 0.25 % less");
    require(rate_correction(.1, .05) == .995 && rate_correction(1., .05) == .995, "Twice and more: 0.5 % less");
    require(rate_correction(.01, 0.) == 1., "No target: unchanged");
}

static void pacer_locks_on_a_close_screen() {
    FramePacer pacer(game_fps);
    for (int i = 0; i < 59; ++i) pacer.presented(1. / 60., 1. / 60.);
    require(!pacer.locked(), "Not before a second of pictures");
    pacer.presented(1. / 60., 1. / 60.);
    require(pacer.locked() && std::abs(pacer.refresh() - 60.) < 1e-9, "60 Hz screen: one frame per refresh");
    require(std::abs(pacer.audio_ratio() - game_fps / 60.) < 1e-12, "Sound played 0.46 % slower");
    // A pause, a stall: not measured.
    pacer.presented(.1, .1);
    pacer.presented(0., 0.);
    require(pacer.locked() && std::abs(pacer.refresh() - 60.) < 1e-9, "Pauses and stalls are ignored");
    // 57 Hz is still within 5 %; 55 Hz is not.
    for (int i = 0; i < 400; ++i) pacer.presented(1. / 57., 1. / 57.);
    require(pacer.locked(), "57 Hz still paces the game");
    for (int i = 0; i < 400; ++i) pacer.presented(1. / 55., 1. / 55.);
    require(!pacer.locked() && pacer.audio_ratio() == 1., "55 Hz: the game keeps its own clock");
}

static void pacer_leaves_far_screens_alone() {
    for (const double hz : {144., 120., 75., 50.}) {
        FramePacer pacer(game_fps);
        for (int i = 0; i < 300; ++i) pacer.presented(1. / hz, 1. / hz);
        require(!pacer.locked(), "Far refresh rates keep the game's clock");
    }
    // Pictures that do not wait for the screen (vsync off, window hidden): the
    // loop's own sleeps or the emulation fill the interval, whatever its length.
    FramePacer unpaced(game_fps);
    for (int i = 0; i < 300; ++i) unpaced.presented(1. / 60., i % 2 ? .004 : .0001);
    require(!unpaced.locked(), "A screen that does not pace the pictures never locks");
    FramePacer pacer(game_fps);
    for (int i = 0; i < 100; ++i) pacer.presented(1. / 60., 1. / 60.);
    require(pacer.locked(), "Locked first");
    // RavenEmu's 1 ms sleep while it waits for the next frame still counts as paced.
    for (int i = 0; i < 100; ++i) pacer.presented(1. / 60., 1. / 60. - .001);
    require(pacer.locked(), "Still paced around a short sleep");
    pacer.presented(.002, .002);
    pacer.presented(.002, .002);
    require(pacer.locked(), "Two quick pictures are not enough to unlock");
    pacer.presented(.002, .002);
    require(!pacer.locked() && pacer.refresh() == 0., "Three in a row: the game takes its own clock again");
    for (int i = 0; i < 59; ++i) pacer.presented(1. / 60., 1. / 60.);
    require(!pacer.locked(), "Measured again from scratch");
    pacer.presented(1. / 60., 1. / 60.);
    require(pacer.locked(), "then locked again");
    require(!FramePacer(0.).locked(), "No game rate, no pacing");
}

// The closed loop: a screen at `screen_hz`, the device eating `rate` frames a
// second, the game's frames resampled with the pacer and the correction. The
// queue settles where the correction makes up for the error of the measured
// refresh: at the target when there is none, never empty nor at twice the
// target within 0.5 %.
static void queue_settles(double screen_hz, double measured_hz, double low, double high) {
    FramePacer pacer(game_fps);
    for (int i = 0; i < 60; ++i) pacer.presented(1. / measured_hz, 1. / measured_hz);
    require(pacer.locked(), "Locked for the simulation");
    constexpr double target = .05;
    Resampler resampler;
    std::vector<std::int16_t> out;
    double queued = target, produced = 0., lowest = 1., highest = 0.;
    const std::vector<std::int16_t> frame(2000, 0);
    for (int refresh = 0; refresh < static_cast<int>(screen_hz * 120); ++refresh) {
        // The core's samples per frame, whole: 548 or 549.
        produced += rate / game_fps;
        const auto n = static_cast<std::size_t>(produced);
        produced -= static_cast<double>(n);
        out.clear();
        resampler.process(frame.data(), n, pacer.audio_ratio() * rate_correction(queued, target), out);
        queued += static_cast<double>(out.size() / 2) / rate - 1. / screen_hz;
        if (refresh > screen_hz * 60) {
            lowest = std::min(lowest, queued);
            highest = std::max(highest, queued);
        }
    }
    require(lowest > target * low && highest < target * high, "The queue settles near its target");
    require(highest - lowest < target * .02, "and stays there");
}

int main() {
    resampler_keeps_the_stream_at_ratio_one();
    resampler_ignores_how_the_stream_is_cut();
    resampler_stretches_by_the_ratio();
    resampler_follows_a_tone();
    resampler_never_wraps_around();
    resampler_starts_again_after_reset();
    correction_follows_the_queue();
    pacer_locks_on_a_close_screen();
    pacer_leaves_far_screens_alone();
    queue_settles(60., 60., .98, 1.02);
    // A screen at 59.94 Hz measured as 60, or at 60.2: the correction absorbs it.
    queue_settles(59.94, 60., .7, 1.);
    queue_settles(60.2, 60., 1., 1.8);
    std::cout << "Resampler, rate control and frame pacing passed\n";
}

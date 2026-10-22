// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_sound.hpp>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ui;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
int main() {
    for (const int rate : {32768, 44100, 48000}) {
        const auto chime = unlock_chime(rate);
        // Stereo, a little under a second, both channels alike.
        require(chime.size() % 2 == 0, "Interleaved stereo");
        const double seconds = static_cast<double>(chime.size() / 2) / rate;
        require(seconds > .7 && seconds < 1., "Chime length");
        bool same = true;
        for (std::size_t i = 0; i < chime.size(); i += 2) same = same && chime[i] == chime[i + 1];
        require(same, "Both channels carry the chime");
        // A quarter of full scale at most, and it starts and ends without a click.
        int peak = 0;
        for (const auto s : chime) peak = std::max(peak, std::abs(static_cast<int>(s)));
        require(peak >= 7900 && peak <= 8000, "Chime level");
        require(std::abs(static_cast<int>(chime.front())) < 200 && std::abs(static_cast<int>(chime.back())) < 200,
                "No click at either end");
    }
    require(unlock_chime(0).empty(), "No chime without an audio rate");

    // The mixer adds the chime to the game's samples, across several buffers.
    const std::vector<std::int16_t> sound{1000, -1000, 2000, -2000, 30000, -30000};
    SoundMixer mixer;
    require(!mixer.playing(), "Silent before anything plays");
    std::vector<std::int16_t> game{10, 10, 10, 10};
    mixer.play(sound);
    mixer.mix(game.data(), game.size());
    require(game == std::vector<std::int16_t>{1010, -990, 2010, -1990} && mixer.playing(), "First buffer");
    std::vector<std::int16_t> loud{10000, -10000, 7, 7};
    mixer.mix(loud.data(), loud.size());
    require(loud == std::vector<std::int16_t>{32767, -32768, 7, 7} && !mixer.playing(), "Saturates, then stops");
    std::vector<std::int16_t> after{5, 5};
    mixer.mix(after.data(), after.size());
    require(after == std::vector<std::int16_t>{5, 5}, "Nothing added once finished");
    mixer.play(sound);
    require(mixer.playing(), "Playing again restarts the sound");
    // Volume: the whole mix, chime included, follows the game's volume.
    std::vector<std::int16_t> level{10000, -10000, 32767, -32768};
    scale_volume(level.data(), level.size(), 50);
    require(level == std::vector<std::int16_t>{5000, -5000, 16383, -16384}, "Half volume");
    scale_volume(level.data(), level.size(), 100);
    require(level == std::vector<std::int16_t>{5000, -5000, 16383, -16384}, "Full volume leaves the samples");
    scale_volume(level.data(), level.size(), 0);
    require(level == std::vector<std::int16_t>{0, 0, 0, 0}, "Muted");
    std::vector<std::int16_t> clamped{100, -100};
    scale_volume(clamped.data(), clamped.size(), 250);
    require(clamped == std::vector<std::int16_t>{100, -100}, "Above 100 % is full volume");
    scale_volume(clamped.data(), clamped.size(), -5);
    require(clamped == std::vector<std::int16_t>{0, 0}, "Below 0 % is silence");
    std::cout << "Unlock chime synthesis, mixing and volume passed\n";
}

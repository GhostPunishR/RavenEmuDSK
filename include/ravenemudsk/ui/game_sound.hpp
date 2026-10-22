// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace ravenemudsk_ui {
// The short chime of an unlocked achievement or a completed game, as
// interleaved stereo PCM16 at `rate` Hz. It is synthesized here: four rising
// bell-like notes, under a second, no recording shipped with RavenEmu.
std::vector<std::int16_t> unlock_chime(int rate);
// Adds one sound at a time to the game's own samples, so it plays through the
// game's audio device, paced by it, and pauses with it.
class SoundMixer {
public:
    // `pcm` must outlive the playback; playing again restarts it.
    void play(const std::vector<std::int16_t>& pcm) { sound_ = &pcm; position_ = 0; }
    // Mixes the next `count` samples into `out`, saturating at the PCM16 range.
    void mix(std::int16_t* out, std::size_t count);
    bool playing() const { return sound_ && position_ < sound_->size(); }
private:
    const std::vector<std::int16_t>* sound_ = nullptr;
    std::size_t position_ = 0;
};
// The game's volume: scales `count` samples to `percent` of their level,
// clamped to 0-100 (0 silences them).
void scale_volume(std::int16_t* pcm, std::size_t count, int percent);
} // namespace ravenemudsk_ui

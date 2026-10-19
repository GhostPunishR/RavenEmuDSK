// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>
struct ravenemu_bridge;
namespace ravenemudsk_ra { class Client; }
namespace ravenemudsk_ui {
struct GameState { std::vector<std::uint8_t> core,achievements; };
// One checksummed file couples the core and achievement conditions atomically.
std::vector<std::uint8_t> encode_game_state(const GameState& state);
// Legacy raw RavenCore states remain supported, without achievement conditions.
GameState decode_game_state(const std::vector<std::uint8_t>& bytes);
GameState capture_game_state(ravenemu_bridge* core,ravenemudsk_ra::Client& client);
// The Hardcore guard runs before decoding or changing either runtime.
bool restore_game_state(ravenemu_bridge* core,ravenemudsk_ra::Client& client,
                        const std::vector<std::uint8_t>& bytes);
}

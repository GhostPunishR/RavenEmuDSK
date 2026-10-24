// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <cstdint>
#include <filesystem>
namespace ravenemudsk_ui {
// Local, user-provided PNG/JPEG/BMP only. No cover fetching or ROM upload.
SDL_Surface* load_local_surface(const std::filesystem::path& path);
SDL_Texture* load_local_image(SDL_Renderer* renderer,
                              const std::filesystem::path& path);
// Writes a picture of the core (ARGB8888, `width` x `height`) as a PNG.
bool save_png(const std::filesystem::path& path, const std::uint32_t* argb, int width, int height);
} // namespace ravenemudsk_ui

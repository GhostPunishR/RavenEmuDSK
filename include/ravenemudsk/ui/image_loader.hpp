// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <filesystem>
namespace ravenemudsk_ui {
// Local, user-provided PNG/JPEG/BMP only. No cover fetching or ROM upload.
SDL_Texture* load_local_image(SDL_Renderer* renderer,
                              const std::filesystem::path& path);
} // namespace ravenemudsk_ui

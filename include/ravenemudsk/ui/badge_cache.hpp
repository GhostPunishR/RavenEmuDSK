// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <cstdint>
#include <future>
#include <string>
#include <unordered_map>
#include <vector>
namespace ravenemudsk_ui {
// Official RetroAchievements badges, downloaded asynchronously and decoded on
// the SDL render thread. Unavailable images are rendered as a violet star.
class BadgeCache {
public:
    explicit BadgeCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~BadgeCache();
    BadgeCache(const BadgeCache&)=delete;
    BadgeCache& operator=(const BadgeCache&)=delete;
    SDL_Texture* image(const std::string& url);
private:
    struct Entry {
        bool started=false;
        bool finished=false;
        std::future<std::vector<std::uint8_t>> download;
        SDL_Texture* texture=nullptr;
    };
    SDL_Renderer* renderer_;
    std::unordered_map<std::string,Entry> images_;
};
} // namespace ravenemudsk_ui

// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <SDL.h>
#include <cstdint>
#include <future>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
namespace ravenemudsk_ui {
// Official RetroAchievements badges, downloaded asynchronously and decoded on
// the SDL render thread. Unavailable images are rendered as a violet star.
class BadgeCache {
public:
    using Download = std::function<std::vector<std::uint8_t>(const std::string&)>;
    using Clock = std::function<std::uint64_t()>;
    explicit BadgeCache(SDL_Renderer* renderer, Download download = {}, Clock clock = {},
                        std::size_t capacity = 128);
    // Call after the previous frame was submitted: textures used by this frame stay alive.
    void begin_frame();
    ~BadgeCache();
    BadgeCache(const BadgeCache&)=delete;
    BadgeCache& operator=(const BadgeCache&)=delete;
    SDL_Texture* image(const std::string& url);
private:
    struct Entry {
        unsigned failures=0;
        std::uint64_t retry_at=0, last_used=0, frame=0;
        std::future<std::vector<std::uint8_t>> download;
        SDL_Texture* texture=nullptr;
    };
    SDL_Renderer* renderer_;
    Download download_;
    Clock clock_;
    std::size_t capacity_;
    std::uint64_t frame_=1, access_=0;
    void complete(Entry& entry);
    std::unordered_map<std::string,Entry> images_;
};
} // namespace ravenemudsk_ui

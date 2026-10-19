// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/badge_cache.hpp>
#include <curl/curl.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace ravenemudsk_ui;
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
SDL_Texture* wait_image(BadgeCache& cache,const std::string& url) {
    for(int i=0;i<1000;++i) { cache.begin_frame();if(auto* image=cache.image(url))return image;std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    return nullptr;
}
int main(int argc,char** argv) {
    require(argc==2,"PNG fixture required");
    std::ifstream file(argv[1],std::ios::binary);std::vector<std::uint8_t> png{std::istreambuf_iterator<char>(file),{}};
    require(!png.empty(),"PNG fixture unreadable");
    SDL_SetMainReady();SDL_setenv("SDL_VIDEODRIVER","dummy",1);require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL initialization");
    auto* surface=SDL_CreateRGBSurfaceWithFormat(0,128,128,32,SDL_PIXELFORMAT_RGBA32);
    auto* renderer=SDL_CreateSoftwareRenderer(surface);require(renderer,"Software renderer");
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::string base="https://media.retroachievements.org/Badge/";
    {
        std::atomic<int> requests=0;std::uint64_t clock=0;
        BadgeCache cache(renderer,[&](const std::string&){return ++requests==1?std::vector<std::uint8_t>{1,2,3}:png;},[&]{return clock;},2);
        cache.image(base+"1.png");
        for(int i=0;i<20;++i){cache.begin_frame();cache.image(base+"1.png");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        require(requests==1,"A failure must back off");
        clock=1999;cache.image(base+"1.png");require(requests==1,"Retry too early");
        clock=2000;require(wait_image(cache,base+"1.png"),"Download must recover");
        auto* first=cache.image(base+"1.png");
        require(wait_image(cache,base+"2.png"),"Second image must load");
        cache.begin_frame();first=cache.image(base+"1.png");require(first,"First image unexpectedly evicted");
        cache.image(base+"3.png"); // Evicts 2, but 1 is pinned until render submission.
        int w=0;require(SDL_QueryTexture(first,nullptr,nullptr,&w,nullptr)==0 && w>0,"Texture evicted during the current frame");
        require(wait_image(cache,base+"3.png"),"Full cache must accept a new image");
        const auto previous=requests.load();
        for(const auto& url:{"http://media.retroachievements.org/Badge/1.png","https://evil.example/a.png","https://media.retroachievements.org/Badge/1.png?token=x"})cache.image(url);
        require(requests==previous,"Invalid hosts or URLs must not be downloaded");
    }
    {
        std::promise<void> release;auto ready=release.get_future().share();std::atomic<int> requests=0;
        BadgeCache cache(renderer,[&](const std::string&){++requests;ready.wait();return png;});
        for(int i=0;i<10;++i)cache.image(base+std::to_string(i)+".png");
        for(int i=0;i<20 && requests<2;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto active=requests.load();release.set_value();require(active==2,"More than two image jobs launched");
    }
    curl_global_cleanup();SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);SDL_Quit();
    std::cout<<"Badge retry, bounded LRU, frame lifetime, URLs and concurrency passed\n";
}

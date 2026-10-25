// SPDX-License-Identifier: AGPL-3.0-only
// RavenEmu: SDL2 desktop gameplay, RavenCore emulation, official RetroAchievements.
// The entry point: the launcher, then the chosen game, until the player leaves.
#include <SDL.h>
#include <ravenemudsk/launcher/app_shell.hpp>
#include <ravenemudsk/player/game_session.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
namespace {
using Clock=std::chrono::steady_clock;
using ravenemudsk_player::play;

int entry(int argc,char** argv) {
    try {
        if(argc>1) {
            if(argc==2 && std::string(argv[1])=="--help") {
                std::cout<<"RavenEmu - Windows, Linux, macOS\nUsage: RavenEmu [gb|gbc|gba ROM]\n";return 0;
            }
            if(argc==2 && std::string(argv[1])=="--version"){std::cout<<"RavenEmu " RAVENEMUDSK_VERSION "\n";return 0;}
            if(argc!=3)throw std::runtime_error("Usage : RavenEmu [gb|gbc|gba ROM]");
            ravenemudsk_ra::client().set_hardcore(ravenemudsk_ui::game_preferences().hardcore);
            play({argv[1],argv[2],false});return 0;
        }
        // SDL stays up for the whole session: the launcher and the game never
        // shut down and restart its subsystems (controller threads included),
        // and a TV cover window survives from one to the other. Audio is
        // optional here; the game still asks for it.
        if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_EVENTS)!=0)
            throw std::runtime_error(SDL_GetError());
        (void)SDL_InitSubSystem(SDL_INIT_AUDIO);
        struct Screen{~Screen(){ravenemudsk_ui::close_screen();SDL_Quit();}} screen;
        std::string error;
        while(true) {
            const auto req=ravenemudsk_ui::run_launcher(error);
            error.clear();
            if(!req)return 0;
            const auto started=Clock::now();
            try{play(*req);}catch(const std::exception& e){error=e.what();continue;}
            const auto seconds=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-started).count();
            ravenemudsk_ui::record_session(*req,static_cast<unsigned long long>(std::max<std::int64_t>(0,seconds)));
        }
    } catch(const std::exception& e){std::cerr<<"RavenEmu: "<<e.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int argc=0;
    LPWSTR* wide=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!wide)return 1;
    std::vector<std::string> strings;
    std::vector<char*> args;
    for(int i=0;i<argc;++i){
        const int n=WideCharToMultiByte(CP_UTF8,0,wide[i],-1,nullptr,0,nullptr,nullptr);
        if(n<1){LocalFree(wide);return 1;}
        std::string s(static_cast<size_t>(n),'\0');
        WideCharToMultiByte(CP_UTF8,0,wide[i],-1,s.data(),n,nullptr,nullptr);
        s.pop_back();
        strings.push_back(std::move(s));
    }
    LocalFree(wide);
    for(auto& s:strings)args.push_back(s.data());
    return entry(argc,args.data());
}
#else
int main(int argc,char** argv){return entry(argc,argv);}
#endif

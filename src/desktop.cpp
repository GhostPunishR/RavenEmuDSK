// SPDX-License-Identifier: AGPL-3.0-only
// RavenEmu: SDL2 desktop gameplay, RavenCore emulation, official RetroAchievements.
#include <SDL.h>
#include <ravenemu/bridge.h>
#include <ravenemudsk/ui/app_shell.hpp>
#include <ravenemudsk/ui/game_overlay.hpp>
#include <ravenemudsk/ui/game_input.hpp>
#include <ravenemudsk/ui/game_state.hpp>
#include <ravenemudsk/ui/library.hpp>
#include <ravenemudsk/ui/image_loader.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
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
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using Core=std::unique_ptr<ravenemu_bridge, decltype(&ravenemu_destroy)>;
template<class T, void(*Free)(T*)>
using Ptr=std::unique_ptr<T, decltype(Free)>;
struct Sdl {
    Sdl() { if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER) != 0)
        throw std::runtime_error(SDL_GetError()); }
    ~Sdl() { SDL_Quit(); }
};
void verify(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string(message)+": "+ravenemu_last_error());
}
std::vector<std::uint8_t> read_file(const fs::path& file, std::size_t cap) {
    std::ifstream in(file,std::ios::binary|std::ios::ate);
    if(!in)throw std::runtime_error("Fichier illisible : "+ravenemudsk_ui::utf8(file));
    const auto len=in.tellg();
    if(len<=0 || static_cast<std::uint64_t>(len)>cap)throw std::runtime_error("Taille du fichier invalide");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(len));
    in.seekg(0);
    if(!in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("Lecture de la ROM impossible");
    return bytes;
}
std::vector<std::uint8_t> read_optional(const fs::path& file, std::size_t cap) {
    if(!fs::exists(file))return {};
    return read_file(file,cap);
}
void atomic_write(const fs::path& path, const std::vector<std::uint8_t>& data) {
    const fs::path tmp=ravenemudsk_ui::sidecar(path,".tmp");
    { std::ofstream o(tmp,std::ios::binary|std::ios::trunc);
      if(!o || !o.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size())))
          throw std::runtime_error("Écriture impossible : "+ravenemudsk_ui::utf8(tmp));
      o.flush(); if(!o)throw std::runtime_error("Écriture interrompue"); }
#ifdef _WIN32
    if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Sauvegarde impossible (Windows)");
#else
    fs::rename(tmp,path);
#endif
}
void save_battery(ravenemu_bridge* core, const fs::path& path) {
    if(!ravenemu_battery_dirty(core))return;
    const size_t len=ravenemu_battery_size(core);
    if(!len || len>(4u<<20))throw std::runtime_error("Taille de sauvegarde invalide");
    std::vector<std::uint8_t> bytes(len);
    std::uint64_t gen=0;
    verify(ravenemu_copy_battery_generation(core,bytes.data(),bytes.size(),&gen)!=0,"Copie de sauvegarde");
    atomic_write(path,bytes);
    verify(ravenemu_acknowledge_battery(core,gen)!=0,"Validation de sauvegarde");
}
void save_state(ravenemu_bridge* core, const fs::path& path) {
    atomic_write(path,ravenemudsk_ui::encode_game_state(ravenemudsk_ui::capture_game_state(core,ravenemudsk_ra::client())));
}
bool load_state(ravenemu_bridge* core,const fs::path& path) {
    auto& ra=ravenemudsk_ra::client();
    if(!ra.can_load_state())throw std::runtime_error("Chargement d’état interdit en Hardcore");
    return ravenemudsk_ui::restore_game_state(core,ra,read_file(path,24u<<20));
}
int system_of(const std::string& code) {
    if(code=="gb")return RAVENEMU_GB;
    if(code=="gbc")return RAVENEMU_GBC;
    if(code=="gba")return RAVENEMU_GBA;
    throw std::runtime_error("Console inconnue : "+code);
}
void play(const ravenemudsk_ui::LaunchRequest& req) {
    const auto rom_path=ravenemudsk_ui::from_utf8(req.rom);
    auto rom=read_file(rom_path,64u<<20);
    verify(ravenemu_api_version()==RAVENEMU_API_VERSION,"Version ABI RavenCore");
    Core core(ravenemu_create(system_of(req.system)),&ravenemu_destroy);
    verify(bool(core),"Création RavenCore");
    const auto folders=ravenemudsk_ui::app_folders();
    const auto battery_path=ravenemudsk_ui::save_path(folders,rom_path,".sav");
    const auto state_path=ravenemudsk_ui::save_path(folders,rom_path,".rstate");
    const auto preview_path=ravenemudsk_ui::preview_path(folders,rom_path);
    const auto battery=read_optional(battery_path,4u<<20);
    verify(ravenemu_load(core.get(),rom.data(),rom.size(),battery.empty()?nullptr:battery.data(),battery.size())!=0,
           "Chargement ROM");
    const int w=ravenemu_video_width(core.get()),h=ravenemu_video_height(core.get());
    const int hz=ravenemu_audio_rate(core.get());
    const double fps=ravenemu_video_fps(core.get());
    if(w<1||w>1024||h<1||h>1024||!std::isfinite(fps)||fps<30||fps>120||hz<8000||hz>192000)
        throw std::runtime_error("Format audio ou vidéo RavenCore invalide");
    Sdl sdl;
    const auto title=ravenemudsk_ui::display_title({rom_path,req.system});
    Ptr<SDL_Window,SDL_DestroyWindow> win(SDL_CreateWindow(("RavenEmu - "+title).c_str(),
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,std::max(640,w*4),std::max(520,h*4),
        SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
    if(!win)throw std::runtime_error(SDL_GetError());
    if(char* base=SDL_GetBasePath()) {
        const auto path=ravenemudsk_ui::from_utf8(base)/"assets"/"ravenemu-logo.png";
        SDL_free(base);
        if(SDL_Surface* icon=ravenemudsk_ui::load_local_surface(path)) {
            SDL_SetWindowIcon(win.get(),icon);
            SDL_FreeSurface(icon);
        }
    }
    SDL_Renderer* sr=SDL_CreateRenderer(win.get(),-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!sr)sr=SDL_CreateRenderer(win.get(),-1,SDL_RENDERER_SOFTWARE);
    Ptr<SDL_Renderer,SDL_DestroyRenderer> renderer(sr,SDL_DestroyRenderer);
    if(!renderer)throw std::runtime_error(SDL_GetError());
    Ptr<SDL_Texture,SDL_DestroyTexture> texture(SDL_CreateTexture(renderer.get(),
        SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h),SDL_DestroyTexture);
    if(!texture)throw std::runtime_error(SDL_GetError());
    if(req.fullscreen)SDL_SetWindowFullscreen(win.get(),SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_AudioSpec spec{};
    spec.freq=hz;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
    SDL_AudioDeviceID audio_device=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
    if(audio_device)SDL_PauseAudioDevice(audio_device,0);
    Ptr<SDL_GameController,SDL_GameControllerClose> pad(nullptr,SDL_GameControllerClose);
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
        pad.reset(SDL_GameControllerOpen(i));if(pad)break;
    }
    ravenemudsk_ra::client().start_game(system_of(req.system),rom,core.get(),req.rom);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w)*h);
    std::vector<std::int16_t> pcm(8192);
    ravenemudsk_ui::GameOverlay overlay(win.get(),renderer.get());
    const auto keyboard=ravenemudsk_ui::keyboard_preferences();
    ravenemudsk_ui::GameInput input([&](int button, bool pressed) {
        ravenemu_set_button(core.get(),button,pressed);
    },keyboard);
    ravenemudsk_ui::OverlayInfo info{};
    info.game_title=title;info.system=req.system;info.has_state=fs::exists(state_path);
    info.keyboard=keyboard;
    auto toast=[&](ravenemudsk_ui::Toast::Kind kind,std::string heading,std::string message="") {
        overlay.notify({kind,std::move(heading),std::move(message),-1.0,5.0});
    };
    bool running=true,paused=false;
    using Action=ravenemudsk_ui::OverlayAction;
    auto next=Clock::now();
    auto last_save=Clock::now();
    auto next_progress=Clock::now();
    const auto duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps));
    try {
      while(running) {
        SDL_Event e{};
        while(SDL_PollEvent(&e)) {
            overlay.process_event(e);
            if(e.type==SDL_QUIT)running=false;
            if(e.type==SDL_CONTROLLERDEVICEADDED && !pad && SDL_IsGameController(e.cdevice.which))
                pad.reset(SDL_GameControllerOpen(e.cdevice.which));
            if(e.type==SDL_CONTROLLERDEVICEREMOVED && pad &&
               SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))==e.cdevice.which)
                { input.release_all(); pad.reset(); }
            if(e.type==SDL_KEYDOWN && !e.key.repeat) {
                using Key=ravenemudsk_ui::KeyAction;
                const auto action=overlay.menu_open()&&e.key.keysym.sym!=SDLK_ESCAPE?
                    Key::Count:keyboard.action(e.key.keysym.sym);
                switch(action) {
                    case Key::Menu:
                        if(overlay.menu_open())overlay.close_menu();
                        else if(!ravenemudsk_ra::client().hardcore() || ravenemudsk_ra::client().can_pause())
                            overlay.open_menu();
                        break;
                    case Key::Pause:
                        if(paused || !ravenemudsk_ra::client().hardcore() || ravenemudsk_ra::client().can_pause()) {
                            paused=!paused;input.release_all();
                        }
                        break;
                    case Key::Reset:
                        input.release_all();verify(ravenemu_reset(core.get())!=0,"Reset");ravenemudsk_ra::client().reset();overlay.clear_retro();break;
                    case Key::SaveState:
                        save_state(core.get(),state_path);info.has_state=true;toast(ravenemudsk_ui::Toast::Kind::Success,"État sauvegardé");
                        break;
                    case Key::LoadState:
                        if(ravenemudsk_ra::client().snapshot().hardcore_preferred) toast(ravenemudsk_ui::Toast::Kind::Info,"Hardcore","Chargement interdit");
                        else if(fs::exists(state_path)){
                            const bool restored=load_state(core.get(),state_path);input.release_all();overlay.clear_retro();
                            toast(ravenemudsk_ui::Toast::Kind::Success,"État chargé",restored?"Progression RetroAchievements restaurée":"Conditions RetroAchievements réinitialisées");}
                        break;
                    case Key::Fullscreen:
                        SDL_SetWindowFullscreen(win.get(),(SDL_GetWindowFlags(win.get())&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                    default:break;
                }
            }
            if(pad && e.type==SDL_CONTROLLERBUTTONDOWN &&
               SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))==e.cbutton.which &&
               e.cbutton.button==SDL_CONTROLLER_BUTTON_GUIDE &&
               (!ravenemudsk_ra::client().hardcore() || ravenemudsk_ra::client().can_pause()))
                overlay.open_menu();
            input.set_menu_open(overlay.menu_open()||paused);
            input.process_event(e,pad ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get())) : -1);
        }
        const auto now=Clock::now();
        const auto ra_status=ravenemudsk_ra::client().snapshot();
        const bool waiting=ra_status.hardcore_preferred && (!ra_status.logged_in ||
            (ra_status.game_status!=ravenemudsk_ra::GameStatus::Ready && ra_status.game_status!=ravenemudsk_ra::GameStatus::NoAchievements));
        if(!waiting&&!paused&&!overlay.menu_open() && now>=next) {
            verify(ravenemu_run_frame(core.get(),pixels.data(),pixels.size())!=0,"Frame");
            ravenemudsk_ra::client().pump(true);
            const auto n=ravenemu_read_audio(core.get(),pcm.data(),pcm.size());
            if(audio_device && n) {
                if(SDL_GetQueuedAudioSize(audio_device)>static_cast<Uint32>(hz*4/5))
                    SDL_ClearQueuedAudio(audio_device);
                SDL_QueueAudio(audio_device,pcm.data(),static_cast<Uint32>(n*sizeof(std::int16_t)));
            }
            SDL_UpdateTexture(texture.get(),nullptr,pixels.data(),w*static_cast<int>(sizeof(std::uint32_t)));
            next+=duration;
            if(now>next+std::chrono::milliseconds(150))next=now+duration;
        } else ravenemudsk_ra::client().pump(false);
        auto& retro=ravenemudsk_ra::client();
        for(const auto& event:retro.drain_events()) {
            if(event.kind=="hardcore_refused"){
                if(!ravenemudsk_ui::remember_hardcore(false))
                    toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le Hardcore reste sélectionné dans les réglages.");
                continue;
            }
            if(event.kind=="hardcore_reset"){
                input.release_all();paused=false;next=Clock::now();
                overlay.clear_retro();overlay.close_menu();
            }
            if(event.kind=="error")toast(ravenemudsk_ui::Toast::Kind::Error,event.title,event.detail);
            else overlay.retro_event(event);
        }
        if(now>=next_progress) {
            overlay.update_retro_progress(retro.snapshot().achievements);
            next_progress=now+std::chrono::milliseconds(350);
        }
        if(now-last_save>std::chrono::seconds(30)) {
            save_battery(core.get(),battery_path);last_save=now;
        }
        SDL_SetRenderDrawColor(renderer.get(),11,12,18,255);
        SDL_RenderClear(renderer.get());
        int out_w=0,out_h=0;
        SDL_GetRendererOutputSize(renderer.get(),&out_w,&out_h);
        const auto factor=std::min(double(out_w)/w,double(out_h)/h);
        const SDL_Rect rect{(out_w-int(w*factor))/2,(out_h-int(h*factor))/2,int(w*factor),int(h*factor)};
        SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&rect);
        info.hardcore=retro.hardcore();
        info.hardcore_preferred=retro.snapshot().hardcore_preferred;
        info.hardcore_waiting=waiting;
        info.retro_status=waiting?retro.snapshot().status:retro.snapshot().rich_presence;
        info.paused=paused;
        info.fullscreen=(SDL_GetWindowFlags(win.get())&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0;
        switch(overlay.draw(info)) {
            case Action::None:break;
            case Action::Resume:overlay.close_menu();break;
            case Action::SaveState:
                save_state(core.get(),state_path);info.has_state=true;
                overlay.close_menu();break;
            case Action::LoadState:
                if(!ravenemudsk_ra::client().snapshot().hardcore_preferred && fs::exists(state_path)){
                    load_state(core.get(),state_path);input.release_all();overlay.clear_retro();}
                overlay.close_menu();break;
            case Action::ToggleHardcore:
                input.release_all();retro.set_hardcore(!retro.snapshot().hardcore_preferred);
                if(!ravenemudsk_ui::remember_hardcore(retro.snapshot().hardcore_preferred))
                    toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le choix reste valable pour cette session.");
                overlay.close_menu();paused=false;break;
            case Action::Reset:
                input.release_all();verify(ravenemu_reset(core.get())!=0,"Reset");ravenemudsk_ra::client().reset();overlay.clear_retro();overlay.close_menu();break;
            case Action::ToggleFullscreen:
                SDL_SetWindowFullscreen(win.get(),info.fullscreen?0:SDL_WINDOW_FULLSCREEN_DESKTOP);break;
            case Action::Quit:running=false;break;
        }
        SDL_RenderPresent(renderer.get());
        if(waiting||paused||overlay.menu_open())SDL_Delay(16);
        else if(Clock::now()<next)std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      save_battery(core.get(),battery_path);
      if(!pixels.empty()) {
          SDL_Surface* bitmap=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),w,h,32,
              w*static_cast<int>(sizeof(std::uint32_t)),SDL_PIXELFORMAT_ARGB8888);
          if(bitmap){SDL_SaveBMP(bitmap,ravenemudsk_ui::utf8(preview_path).c_str());SDL_FreeSurface(bitmap);}
      }
    } catch(...) {
        ravenemudsk_ra::client().end_game();
        if(audio_device)SDL_CloseAudioDevice(audio_device);
        throw;
    }
    ravenemudsk_ra::client().end_game();
    if(audio_device)SDL_CloseAudioDevice(audio_device);
}
int entry(int argc,char** argv) {
    try {
        if(argc>1) {
            if(argc==2 && std::string(argv[1])=="--help") {
                std::cout<<"RavenEmu - Windows, Linux, macOS\nUsage: RavenEmuDSK [gb|gbc|gba ROM]\n";return 0;
            }
            if(argc==2 && std::string(argv[1])=="--version"){std::cout<<"RavenEmu " RAVENEMUDSK_VERSION "\n";return 0;}
            if(argc!=3)throw std::runtime_error("Usage : RavenEmuDSK [gb|gbc|gba ROM]");
            ravenemudsk_ra::client().set_hardcore(ravenemudsk_ui::hardcore_preference());
            play({argv[1],argv[2],false});return 0;
        }
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

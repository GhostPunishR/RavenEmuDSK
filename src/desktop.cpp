// SPDX-License-Identifier: AGPL-3.0-only
// RavenEmuDSK: SDL2 desktop gameplay, RavenCore emulation, official RetroAchievements.
#include <SDL.h>
#include <ravenemu/bridge.h>
#include <ravenemudsk/ui/app_shell.hpp>
#include <ravenemudsk/ui/game_overlay.hpp>
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
    const size_t len=ravenemu_state_size(core);
    if(!len || len>(16u<<20))throw std::runtime_error("État trop volumineux");
    std::vector<std::uint8_t> bytes(len);
    verify(ravenemu_save_state(core,bytes.data(),bytes.size())!=0,"Sauvegarde d'état");
    atomic_write(path,bytes);
}
void load_state(ravenemu_bridge* core,const fs::path& path) {
    auto data=read_file(path,16u<<20);
    verify(ravenemu_load_state(core,data.data(),data.size())!=0,"Chargement d'état");
}
int system_of(const std::string& code) {
    if(code=="gb")return RAVENEMU_GB;
    if(code=="gbc")return RAVENEMU_GBC;
    if(code=="gba")return RAVENEMU_GBA;
    throw std::runtime_error("Console inconnue : "+code);
}
int key_of(SDL_Keycode key) {
    switch(key) {
        case SDLK_UP:return RAVENEMU_UP;case SDLK_DOWN:return RAVENEMU_DOWN;
        case SDLK_LEFT:return RAVENEMU_LEFT;case SDLK_RIGHT:return RAVENEMU_RIGHT;
        case SDLK_z:return RAVENEMU_A;case SDLK_x:return RAVENEMU_B;
        case SDLK_RETURN:return RAVENEMU_START;case SDLK_BACKSPACE:return RAVENEMU_SELECT;
        case SDLK_a:return RAVENEMU_L;case SDLK_s:return RAVENEMU_R;
        default:return -1;
    }
}
int pad_of(std::uint8_t btn) {
    switch(btn) {
        case SDL_CONTROLLER_BUTTON_A:return RAVENEMU_A;
        case SDL_CONTROLLER_BUTTON_B:return RAVENEMU_B;
        case SDL_CONTROLLER_BUTTON_START:return RAVENEMU_START;
        case SDL_CONTROLLER_BUTTON_BACK:return RAVENEMU_SELECT;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:return RAVENEMU_UP;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:return RAVENEMU_DOWN;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:return RAVENEMU_LEFT;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:return RAVENEMU_RIGHT;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:return RAVENEMU_L;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:return RAVENEMU_R;
        default:return -1;
    }
}
void release_controls(ravenemu_bridge* core) {
    for(int b=RAVENEMU_UP;b<=RAVENEMU_R;++b)ravenemu_set_button(core,b,0);
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
    const auto preview_path=ravenemudsk_ui::save_path(folders,rom_path,".ravenemu-preview.bmp");
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
    Ptr<SDL_Window,SDL_DestroyWindow> win(SDL_CreateWindow(("RavenEmuDSK - "+title).c_str(),
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
    ravenemudsk_ra::client().start_game(system_of(req.system),rom,core.get());
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w)*h);
    std::vector<std::int16_t> pcm(8192);
    ravenemudsk_ui::GameOverlay overlay(win.get(),renderer.get());
    ravenemudsk_ui::OverlayInfo info{};
    info.game_title=title;info.system=req.system;info.has_state=fs::exists(state_path);
    auto toast=[&](ravenemudsk_ui::Toast::Kind kind,std::string heading,std::string message="") {
        overlay.notify({kind,std::move(heading),std::move(message),-1.0,5.0});
    };
    bool running=true,paused=false;
    using Action=ravenemudsk_ui::OverlayAction;
    auto next=Clock::now();
    auto last_save=Clock::now();
    const auto duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps));
    try {
      while(running) {
        SDL_Event e{};
        while(SDL_PollEvent(&e)) {
            overlay.process_event(e);
            if(e.type==SDL_QUIT)running=false;
            if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST)
                release_controls(core.get());
            if(e.type==SDL_CONTROLLERDEVICEADDED && !pad && SDL_IsGameController(e.cdevice.which))
                pad.reset(SDL_GameControllerOpen(e.cdevice.which));
            if(e.type==SDL_CONTROLLERDEVICEREMOVED && pad &&
               SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))==e.cdevice.which)
                pad.reset();
            if(e.type==SDL_KEYDOWN && !e.key.repeat) {
                switch(e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        if(overlay.menu_open())overlay.close_menu();
                        else if(!ravenemudsk_ra::client().hardcore() || ravenemudsk_ra::client().can_pause())
                            overlay.open_menu();
                        break;
                    case SDLK_p:paused=!paused;break;
                    case SDLK_F4:
                        verify(ravenemu_reset(core.get())!=0,"Reset");ravenemudsk_ra::client().reset();break;
                    case SDLK_F5:
                        if(ravenemudsk_ra::client().hardcore()) toast(ravenemudsk_ui::Toast::Kind::Info,"Hardcore","États désactivés");
                        else {save_state(core.get(),state_path);info.has_state=true;toast(ravenemudsk_ui::Toast::Kind::Success,"État sauvegardé");}
                        break;
                    case SDLK_F9:
                        if(ravenemudsk_ra::client().hardcore()) toast(ravenemudsk_ui::Toast::Kind::Info,"Hardcore","Chargement interdit");
                        else if(fs::exists(state_path)){load_state(core.get(),state_path);ravenemudsk_ra::client().reset();toast(ravenemudsk_ui::Toast::Kind::Success,"État chargé");}
                        break;
                    case SDLK_F11:
                        SDL_SetWindowFullscreen(win.get(),(SDL_GetWindowFlags(win.get())&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);
                        break;
                    default:break;
                }
            }
            if(!overlay.menu_open() && (e.type==SDL_KEYDOWN||e.type==SDL_KEYUP) && !e.key.repeat) {
                const int button=key_of(e.key.keysym.sym);
                if(button>=0)ravenemu_set_button(core.get(),button,e.type==SDL_KEYDOWN);
            }
            if(pad && (e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP)) {
                if(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))==e.cbutton.which) {
                    if(e.cbutton.button==SDL_CONTROLLER_BUTTON_GUIDE && e.type==SDL_CONTROLLERBUTTONDOWN)overlay.open_menu();
                    if(!overlay.menu_open()) {
                        const int button=pad_of(e.cbutton.button);
                        if(button>=0)ravenemu_set_button(core.get(),button,e.type==SDL_CONTROLLERBUTTONDOWN);
                    }
                }
            }
        }
        const auto now=Clock::now();
        if(!paused&&!overlay.menu_open() && now>=next) {
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
        for(const auto& event:ravenemudsk_ra::client().drain_events()) {
            if(event.kind=="achievement")toast(ravenemudsk_ui::Toast::Kind::Achievement,event.title,event.detail);
            else if(event.kind=="hardcore_reset"){
                ravenemu_reset(core.get());
                toast(ravenemudsk_ui::Toast::Kind::Info,event.title);
            } else if(event.kind=="error")toast(ravenemudsk_ui::Toast::Kind::Error,event.title,event.detail);
            else toast(ravenemudsk_ui::Toast::Kind::Info,event.title,event.detail);
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
        info.paused=paused;
        info.fullscreen=(SDL_GetWindowFlags(win.get())&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0;
        switch(overlay.draw(info)) {
            case Action::None:break;
            case Action::Resume:overlay.close_menu();break;
            case Action::SaveState:
                if(!ravenemudsk_ra::client().hardcore()){save_state(core.get(),state_path);info.has_state=true;}
                overlay.close_menu();break;
            case Action::LoadState:
                if(!ravenemudsk_ra::client().hardcore() && fs::exists(state_path)){
                    load_state(core.get(),state_path);ravenemudsk_ra::client().reset();}
                overlay.close_menu();break;
            case Action::Reset:
                ravenemu_reset(core.get());ravenemudsk_ra::client().reset();overlay.close_menu();break;
            case Action::ToggleFullscreen:
                SDL_SetWindowFullscreen(win.get(),info.fullscreen?0:SDL_WINDOW_FULLSCREEN_DESKTOP);break;
            case Action::Quit:running=false;break;
        }
        SDL_RenderPresent(renderer.get());
        if(paused||overlay.menu_open())SDL_Delay(16);
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
                std::cout<<"RavenEmuDSK - Windows, Linux, macOS\nUsage: RavenEmuDSK [gb|gbc|gba ROM]\n";return 0;
            }
            if(argc==2 && std::string(argv[1])=="--version"){std::cout<<"RavenEmuDSK 0.1.0\n";return 0;}
            if(argc!=3)throw std::runtime_error("Usage : RavenEmuDSK [gb|gbc|gba ROM]");
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
    } catch(const std::exception& e){std::cerr<<"RavenEmuDSK: "<<e.what()<<'\n';return 1;}
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

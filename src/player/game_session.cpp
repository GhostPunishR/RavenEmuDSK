// SPDX-License-Identifier: AGPL-3.0-only
// One game on the desktop: its window, the frame loop, saves, the pause menu,
// RetroAchievements and the wireless link cable.
#include <ravenemudsk/player/game_session.hpp>
#include <SDL.h>
#include <ravenemu/bridge.h>
#include <ravenemudsk/player/game_overlay.hpp>
#include <ravenemudsk/input/game_input.hpp>
#include <ravenemudsk/saves/game_state.hpp>
#include <ravenemudsk/library/library.hpp>
#include <ravenemudsk/ui/image_loader.hpp>
#include <ravenemudsk/audio/game_sound.hpp>
#include <ravenemudsk/audio/av_sync.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/wireless/wireless_link.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
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
#endif
namespace {
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
using Core=std::unique_ptr<ravenemu_bridge, decltype(&ravenemu_destroy)>;
template<class T, void(*Free)(T*)>
using Ptr=std::unique_ptr<T, decltype(Free)>;
// Reference-counted, so that a window kept for TV mode survives the game.
struct Sdl {
    static constexpr Uint32 systems=SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMECONTROLLER;
    Sdl() { if (SDL_InitSubSystem(systems) != 0) throw std::runtime_error(SDL_GetError()); }
    ~Sdl() { SDL_QuitSubSystem(systems); }
};
std::optional<ravenemudsk_ui::PadFamily> family_of(SDL_GameController* pad) {
    if(!pad)return std::nullopt;
    return ravenemudsk_ui::pad_family(SDL_GameControllerGetType(pad));
}
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
// From TV mode the launcher's full-screen window and renderer carry on:
// no window opens or closes, so the desktop never shows. They are handed
// back to the launcher afterwards. Leaving full screen later restores a
// usual window.
struct GameScreen {
    bool shared = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    GameScreen() = default;
    GameScreen(const GameScreen&) = delete;
    GameScreen& operator=(const GameScreen&) = delete;
    ~GameScreen() {
        if (shared) {
            ravenemudsk_ui::keep_screen({window, renderer});
            return;
        }
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }
};
// One game, from its ROM to its last battery save. The members follow the
// order in which the game opens them, so they close in the reverse order.
class GameSession {
public:
    explicit GameSession(const ravenemudsk_ui::LaunchRequest& request);
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;
    // Leaves the game's RetroAchievements session and the audio device, also
    // when the game stops on an error.
    ~GameSession();
    // The frame loop, until the player quits; then the last saves.
    void run();
private:
    using Action=ravenemudsk_ui::OverlayAction;
    void open_screen();
    void toast(ravenemudsk_ui::Toast::Kind kind,std::string heading,std::string message="");
    bool pause_allowed();
    void handle(const SDL_Event& e);
    void handle_key(ravenemudsk_ui::KeyAction action);
    void emulate(Clock::time_point now,bool waiting);
    void queue_sound(std::size_t samples);
    void take_retro_events();
    void update_link();
    bool link_holds() const;
    void draw(bool waiting);
    void apply(Action action);
    void finish();

    const ravenemudsk_ui::LaunchRequest& req;
    fs::path rom_path;
    std::vector<std::uint8_t> rom;
    Core core{nullptr,&ravenemu_destroy};
    // Held around every RavenCore call: the wireless link cable plugs in and
    // answers the other console from its own threads, between two frames.
    std::mutex core_mutex;
    fs::path battery_path,state_path,preview_path;
    int w=0,h=0,hz=0;
    double fps=0;
    std::optional<Sdl> sdl;
    std::string title;
    GameScreen screen;
    SDL_Window* window=nullptr;
    SDL_Renderer* renderer=nullptr;
    Ptr<SDL_Texture,SDL_DestroyTexture> texture{nullptr,SDL_DestroyTexture};
    ravenemudsk_ui::GamePreferences prefs;
    SDL_AudioDeviceID audio_device=0;
    Ptr<SDL_GameController,SDL_GameControllerClose> pad{nullptr,SDL_GameControllerClose};
    std::vector<std::uint32_t> pixels;
    std::vector<std::int16_t> pcm;
    std::optional<ravenemudsk_ui::GameOverlay> overlay;
    // An unlock's chime is mixed into the game's sound, when the settings allow it.
    std::vector<std::int16_t> chime;
    ravenemudsk_ui::SoundMixer effects;
    // The screen paces the game when its refresh is close to the console's;
    // the sound is resampled so that the device's queue stays near its target.
    static constexpr double audio_target=.05;
    // The queue's level over about half a second: the device takes the sound
    // by blocks of 31 ms, which the correction must not follow.
    double audio_level=audio_target;
    std::optional<ravenemudsk_ui::FramePacer> pacer;
    ravenemudsk_ui::Resampler resampler;
    std::vector<std::int16_t> resampled;
    std::optional<ravenemudsk_ui::GameInput> input;
    ravenemudsk_ui::OverlayInfo info{};
    // Closed before the core it plugs into.
    std::unique_ptr<ravenemudsk_link::WirelessLink> link;
    bool running=true,paused=false;
    Clock::time_point next,last_save,next_progress,last_frame,last_present;
    Clock::duration duration{};
};
GameSession::GameSession(const ravenemudsk_ui::LaunchRequest& request) : req(request) {
    rom_path=ravenemudsk_ui::from_utf8(req.rom);
    rom=read_file(rom_path,64u<<20);
    verify(ravenemu_api_version()==RAVENEMU_API_VERSION,"Version ABI RavenCore");
    core=Core(ravenemu_create(system_of(req.system)),&ravenemu_destroy);
    verify(bool(core),"Création RavenCore");
    const auto folders=ravenemudsk_ui::app_folders();
    battery_path=ravenemudsk_ui::save_path(folders,rom_path,".sav");
    state_path=ravenemudsk_ui::save_path(folders,rom_path,".rstate");
    preview_path=ravenemudsk_ui::preview_path(folders,rom_path);
    const auto battery=read_optional(battery_path,4u<<20);
    verify(ravenemu_load(core.get(),rom.data(),rom.size(),battery.empty()?nullptr:battery.data(),battery.size())!=0,
           "Chargement ROM");
    w=ravenemu_video_width(core.get());h=ravenemu_video_height(core.get());
    hz=ravenemu_audio_rate(core.get());
    fps=ravenemu_video_fps(core.get());
    if(w<1||w>1024||h<1||h>1024||!std::isfinite(fps)||fps<30||fps>120||hz<8000||hz>192000)
        throw std::runtime_error("Format audio ou vidéo RavenCore invalide");
    sdl.emplace();
    title=ravenemudsk_ui::display_title({rom_path,req.system});
    open_screen();
    // Black at once, before the first emulated frame.
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
    texture.reset(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h));
    if(!texture)throw std::runtime_error(SDL_GetError());
    prefs=ravenemudsk_ui::game_preferences();
    // Sharp pixels whatever scaling hint the launcher left behind.
    SDL_SetTextureScaleMode(texture.get(),prefs.smooth_image?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
    SDL_AudioSpec spec{};
    spec.freq=hz;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
    audio_device=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
    if(audio_device)SDL_PauseAudioDevice(audio_device,0);
    for(int i=0;i<SDL_NumJoysticks();++i)if(SDL_IsGameController(i)) {
        pad.reset(SDL_GameControllerOpen(i));if(pad)break;
    }
    ravenemudsk_ra::client().start_game(system_of(req.system),rom,core.get(),req.rom);
    pixels.resize(static_cast<std::size_t>(w)*h);
    pcm.resize(8192);
    overlay.emplace(window,renderer,&ravenemudsk_ra::client(),req.fullscreen);
    chime=prefs.unlock_sound?ravenemudsk_ui::unlock_chime(hz):std::vector<std::int16_t>{};
    input.emplace([this](int button, bool pressed) {
        ravenemu_set_button(core.get(),button,pressed);
    },prefs.keyboard,prefs.pads);
    info.game_title=title;info.system=req.system;info.has_state=fs::exists(state_path);
    info.keyboard=prefs.keyboard;info.pads=prefs.pads;info.pad=family_of(pad.get());
    next=last_save=next_progress=last_frame=last_present=Clock::now();
    duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps));
    pacer.emplace(fps);
    // Idle until the game uses its link port: then it looks for a partner.
    if(prefs.wireless_link) {
        link=std::make_unique<ravenemudsk_link::WirelessLink>(ravenemudsk_link::WirelessLink::Game{core.get(),
            &core_mutex,req.system=="gba"?ravenemudsk_link::family_gba:ravenemudsk_link::family_gb,title});
        info.link_own_label=link->view().own_label;
    }
}
GameSession::~GameSession() {
    ravenemudsk_ra::client().end_game();
    if(audio_device)SDL_CloseAudioDevice(audio_device);
}
// The launcher's window from TV mode, or a window of its own, with the
// RavenEmu icon and a renderer.
void GameSession::open_screen() {
    if (req.fullscreen) {
        const auto kept = ravenemudsk_ui::take_screen();
        screen.window = kept.window;
        screen.renderer = kept.renderer;
        screen.shared = true;
    }
    if (screen.window) SDL_SetWindowTitle(screen.window,("RavenEmu - "+title).c_str());
    else screen.window=SDL_CreateWindow(("RavenEmu - "+title).c_str(),
        SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,std::max(640,w*4),std::max(520,h*4),
        SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE|(req.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0));
    if(!screen.window)throw std::runtime_error(SDL_GetError());
    window=screen.window;
    // Played from the couch: no mouse cursor over the game.
    SDL_ShowCursor(req.fullscreen?SDL_DISABLE:SDL_ENABLE);
    if(char* base=SDL_GetBasePath()) {
        const auto path=ravenemudsk_ui::from_utf8(base)/"assets"/"ravenemu-logo.png";
        SDL_free(base);
        if(SDL_Surface* icon=ravenemudsk_ui::load_local_surface(path)) {
            SDL_SetWindowIcon(window,icon);
            SDL_FreeSurface(icon);
        }
    }
    if(!screen.renderer)screen.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
    if(!screen.renderer)screen.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!screen.renderer)throw std::runtime_error(SDL_GetError());
    renderer=screen.renderer;
}
void GameSession::toast(ravenemudsk_ui::Toast::Kind kind,std::string heading,std::string message) {
    overlay->notify({kind,std::move(heading),std::move(message),-1.0,5.0});
}
// Hardcore limits pauses: when the SDK refuses one, say how long to play first.
bool GameSession::pause_allowed() {
    std::uint32_t frames=0;
    if(ravenemudsk_ra::client().may_pause(&frames))return true;
    const auto seconds=std::max<std::uint32_t>(1,(frames+59)/60);
    toast(ravenemudsk_ui::Toast::Kind::Info,"Pause indisponible",
          "Le Hardcore limite les pauses : réessayez dans "+std::to_string(seconds)+" s.");
    return false;
}
void GameSession::run() {
    // Let go while the picture is shown and while the loop sleeps: the link
    // cable's moments.
    std::unique_lock core_lock(core_mutex);
    bool was_still=true;
    Clock::duration slept{};
    while(running) {
        SDL_Event e{};
        while(SDL_PollEvent(&e))handle(e);
        input->update(SDL_GetTicks());
        if(link)update_link();
        const auto now=Clock::now();
        const bool waiting=ravenemudsk_ra::client().frames_on_hold();
        emulate(now,waiting);
        take_retro_events();
        auto& retro=ravenemudsk_ra::client();
        if(now>=next_progress) {
            overlay->update_retro_progress(retro.snapshot().achievements);
            next_progress=now+std::chrono::milliseconds(350);
        }
        if(now-last_save>std::chrono::seconds(30)) {
            save_battery(core.get(),battery_path);last_save=now;
        }
        draw(waiting);
        core_lock.unlock();
        SDL_RenderPresent(renderer);
        // The screen's refresh, measured between two pictures of a running
        // game, apart from the loop's own sleep.
        const auto shown=Clock::now();
        const bool still=waiting||paused||overlay->menu_open()||link_holds();
        const auto seconds=[](Clock::duration d){return std::chrono::duration<double>(d).count();};
        if(!still&&!was_still)pacer->presented(seconds(shown-last_present),seconds(shown-last_present-slept));
        last_present=shown;was_still=still;slept={};
        if(still)SDL_Delay(16);
        else if(!pacer->locked()&&Clock::now()<next) {
            const auto before=Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            slept=Clock::now()-before;
        }
        core_lock.lock();
    }
    finish();
}
void GameSession::handle(const SDL_Event& e) {
    overlay->process_event(e);
    if(e.type==SDL_QUIT)running=false;
    if(e.type==SDL_CONTROLLERDEVICEADDED && !pad && SDL_IsGameController(e.cdevice.which))
        { pad.reset(SDL_GameControllerOpen(e.cdevice.which)); info.pad=family_of(pad.get()); }
    if(e.type==SDL_CONTROLLERDEVICEREMOVED && pad &&
       SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get()))==e.cdevice.which)
        { input->release_all(); pad.reset(); info.pad.reset(); }
    // "Relier le câble à…" takes the keys while it is up.
    const bool choosing=!info.link_choices.empty()&&!overlay->menu_open();
    if(e.type==SDL_KEYDOWN && !e.key.repeat) {
        using Key=ravenemudsk_ui::KeyAction;
        handle_key(choosing||(overlay->menu_open()&&e.key.keysym.sym!=SDLK_ESCAPE)?
            Key::Count:prefs.keyboard.action(e.key.keysym.sym));
    }
    // Back and Start together, in either order, open or close the menu,
    // and neither reaches the game. The Guide button stays with the
    // system: Windows opens the Xbox Game Bar on it (Steam its overlay),
    // which would show too.
    input->set_menu_open(overlay->menu_open()||paused||choosing);
    if(input->process_event(e,pad ? SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad.get())) : -1)) {
        if(overlay->menu_open())overlay->close_menu();
        else if(pause_allowed())overlay->open_menu(true);
        input->release_all();
    }
}
void GameSession::handle_key(ravenemudsk_ui::KeyAction action) {
    using Key=ravenemudsk_ui::KeyAction;
    switch(action) {
        case Key::Menu:
            if(overlay->menu_open())overlay->close_menu();
            else if(pause_allowed())overlay->open_menu();
            break;
        case Key::Pause:
            if(paused || pause_allowed()) {
                paused=!paused;input->release_all();
            }
            break;
        case Key::Reset:
            input->release_all();verify(ravenemu_reset(core.get())!=0,"Reset");ravenemudsk_ra::client().reset();overlay->clear_retro();break;
        case Key::SaveState:
            save_state(core.get(),state_path);info.has_state=true;toast(ravenemudsk_ui::Toast::Kind::Success,"État sauvegardé");
            break;
        case Key::LoadState:
            if(ravenemudsk_ra::client().snapshot().hardcore_preferred) toast(ravenemudsk_ui::Toast::Kind::Info,"Hardcore","Chargement interdit");
            else if(!info.link_partner.empty())
                toast(ravenemudsk_ui::Toast::Kind::Info,"Câble sans fil relié","Le chargement d'une sauvegarde d'état est désactivé.");
            else if(fs::exists(state_path)){
                const bool restored=load_state(core.get(),state_path);input->release_all();overlay->clear_retro();
                toast(ravenemudsk_ui::Toast::Kind::Success,"État chargé",restored?"Progression RetroAchievements restaurée":"Conditions RetroAchievements réinitialisées");}
            break;
        case Key::Fullscreen:
            SDL_SetWindowFullscreen(window,(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)?0:SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        default:break;
    }
}
// One emulated frame when it is due: at each refresh while the screen paces
// the game (half a frame apart at least, should the picture stop waiting for
// the screen), else on the game's own clock. Its sound goes out with the chime
// and the volume; between frames the SDK still runs.
void GameSession::emulate(Clock::time_point now,bool waiting) {
    const bool due=pacer->locked()?now>=last_frame+duration/2:now>=next;
    if(!waiting&&!paused&&!overlay->menu_open()&&!link_holds() && due) {
        verify(ravenemu_run_frame(core.get(),pixels.data(),pixels.size())!=0,"Frame");
        if(link)link->set_wanted(ravenemu_link_wanted(core.get())!=0);
        ravenemudsk_ra::client().pump(true);
        const auto n=ravenemu_read_audio(core.get(),pcm.data(),pcm.size());
        effects.mix(pcm.data(),n);
        ravenemudsk_ui::scale_volume(pcm.data(),n,prefs.volume);
        queue_sound(n);
        SDL_UpdateTexture(texture.get(),nullptr,pixels.data(),w*static_cast<int>(sizeof(std::uint32_t)));
        last_frame=now;
        next=pacer->locked()?now+duration:next+duration;
        if(now>next+std::chrono::milliseconds(150))next=now+duration;
    } else ravenemudsk_ra::client().pump(false);
}
// The frame's sound, resampled a little faster or slower so that the device's
// queue stays near its target. A queue run dry (start, pause, slowdown) first
// gets its target in silence; one left far over it by a stall starts afresh.
void GameSession::queue_sound(std::size_t samples) {
    if(!audio_device||!samples)return;
    const double frame_bytes=2*sizeof(std::int16_t);
    double queued=SDL_GetQueuedAudioSize(audio_device)/(frame_bytes*hz);
    if(queued>4*audio_target) {
        SDL_ClearQueuedAudio(audio_device);
        queued=0;
    }
    if(queued==0) {
        resampler.reset();
        const std::vector<std::int16_t> silence(static_cast<std::size_t>(audio_target*hz)*2,0);
        SDL_QueueAudio(audio_device,silence.data(),static_cast<Uint32>(silence.size()*sizeof(std::int16_t)));
        queued=audio_level=audio_target;
    }
    audio_level+=(queued-audio_level)/32;
    resampled.clear();
    resampler.process(pcm.data(),samples/2,pacer->audio_ratio()*ravenemudsk_ui::rate_correction(audio_level,audio_target),
                      resampled);
    SDL_QueueAudio(audio_device,resampled.data(),static_cast<Uint32>(resampled.size()*sizeof(std::int16_t)));
}
void GameSession::take_retro_events() {
    for(const auto& event:ravenemudsk_ra::client().drain_events()) {
        if(event.kind=="hardcore_refused"){
            if(!ravenemudsk_ui::remember_hardcore(false))
                toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le Hardcore reste sélectionné dans les réglages.");
            continue;
        }
        if(event.kind=="hardcore_reset"){
            input->release_all();paused=false;next=Clock::now();
            overlay->clear_retro();overlay->close_menu();
        }
        if(event.kind=="error")toast(ravenemudsk_ui::Toast::Kind::Error,event.title,event.detail);
        else overlay->retro_event(event);
    }
}
// The wireless link cable: this side's pause for the other game, what to show
// of the cable, and its news as notifications.
void GameSession::update_link() {
    link->set_paused(paused||overlay->menu_open());
    const auto view=link->view();
    const bool was_choosing=!info.link_choices.empty();
    info.link_partner=view.partner;
    info.link_partner_paused=view.partner_paused;
    info.link_own_label=view.own_label;
    info.link_choices.clear();
    for(const auto& choice:view.choices)info.link_choices.push_back({choice.id,choice.label});
    if(!was_choosing&&!info.link_choices.empty())input->release_all();
    for(auto& notice:link->take_notices())toast(ravenemudsk_ui::Toast::Kind::Info,std::move(notice));
}
// The game waits while the other one's menu is open, as two linked consoles
// do, and while the player picks a partner.
bool GameSession::link_holds() const {
    return info.link_partner_paused||!info.link_choices.empty();
}
// The picture scaled into the window, the overlay over it, then its action.
void GameSession::draw(bool waiting) {
    auto& retro=ravenemudsk_ra::client();
    SDL_SetRenderDrawColor(renderer,11,12,18,255);
    SDL_RenderClear(renderer);
    int out_w=0,out_h=0;
    SDL_GetRendererOutputSize(renderer,&out_w,&out_h);
    const auto factor=std::min(double(out_w)/w,double(out_h)/h);
    const SDL_Rect rect{(out_w-int(w*factor))/2,(out_h-int(h*factor))/2,int(w*factor),int(h*factor)};
    SDL_RenderCopy(renderer,texture.get(),nullptr,&rect);
    info.hardcore=retro.hardcore();
    info.hardcore_preferred=retro.snapshot().hardcore_preferred;
    info.hardcore_waiting=waiting;
    info.retro_status=waiting?retro.snapshot().status:retro.snapshot().rich_presence;
    info.paused=paused;
    info.fullscreen=(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP)!=0;
    const auto action=overlay->draw(info);
    if(overlay->take_unlock_sound()&&!chime.empty())effects.play(chime);
    apply(action);
}
void GameSession::apply(Action action) {
    auto& retro=ravenemudsk_ra::client();
    switch(action) {
        case Action::None:break;
        case Action::Resume:overlay->close_menu();break;
        case Action::SaveState:
            save_state(core.get(),state_path);info.has_state=true;
            overlay->close_menu();break;
        case Action::LoadState:
            if(!ravenemudsk_ra::client().snapshot().hardcore_preferred && info.link_partner.empty() &&
               fs::exists(state_path)){
                load_state(core.get(),state_path);input->release_all();overlay->clear_retro();}
            overlay->close_menu();break;
        case Action::ToggleHardcore:
            input->release_all();retro.set_hardcore(!retro.snapshot().hardcore_preferred);
            if(!ravenemudsk_ui::remember_hardcore(retro.snapshot().hardcore_preferred))
                toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le choix reste valable pour cette session.");
            overlay->close_menu();paused=false;break;
        case Action::Reset:
            input->release_all();verify(ravenemu_reset(core.get())!=0,"Reset");ravenemudsk_ra::client().reset();overlay->clear_retro();overlay->close_menu();break;
        case Action::ToggleFullscreen:
            SDL_SetWindowFullscreen(window,info.fullscreen?0:SDL_WINDOW_FULLSCREEN_DESKTOP);break;
        case Action::Quit:running=false;break;
        case Action::UnplugLink:
            if(link)link->unplug();
            overlay->close_menu();break;
        case Action::ChooseLink:if(link)link->choose(overlay->chosen_link());break;
        case Action::ChooseLinkLater:if(link)link->choose(std::nullopt);break;
    }
}
// The battery save and the library's preview of the last frame.
void GameSession::finish() {
    if(req.fullscreen) {
        // Left on screen, black, until the launcher's window is up.
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
    }
    save_battery(core.get(),battery_path);
    if(!pixels.empty()) {
        SDL_Surface* bitmap=SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(),w,h,32,
            w*static_cast<int>(sizeof(std::uint32_t)),SDL_PIXELFORMAT_ARGB8888);
        if(bitmap){SDL_SaveBMP(bitmap,ravenemudsk_ui::utf8(preview_path).c_str());SDL_FreeSurface(bitmap);}
    }
}
} // namespace

namespace ravenemudsk_player {
void play(const ravenemudsk_ui::LaunchRequest& req) {
    GameSession game(req);
    game.run();
}
} // namespace ravenemudsk_player

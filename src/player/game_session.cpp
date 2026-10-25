// SPDX-License-Identifier: AGPL-3.0-only
// One game on the desktop: its window, the frame loop, saves, the pause menu,
// RetroAchievements and the wireless link cable.
#include <ravenemudsk/player/game_session.hpp>
#include <SDL.h>
#include <ravenemu/bridge.h>
#include <ravenemudsk/player/game_overlay.hpp>
#include <ravenemudsk/input/game_input.hpp>
#include <ravenemudsk/input/motion.hpp>
#include <ravenemudsk/saves/game_state.hpp>
#include <ravenemudsk/saves/rewind.hpp>
#include <ravenemudsk/saves/cheats.hpp>
#include <ravenemudsk/library/library.hpp>
#include <ravenemudsk/ui/image_loader.hpp>
#include <ravenemudsk/ui/screen.hpp>
#include <ravenemudsk/audio/game_sound.hpp>
#include <ravenemudsk/audio/av_sync.hpp>
#include <ravenemudsk/player/picture.hpp>
#include <ravenemudsk/achievements/client.hpp>
#include <ravenemudsk/wireless/wireless_link.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
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
SDL_JoystickID instance_of(SDL_GameController* pad) {
    return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad));
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
// "le 24/10 à 17:05": when a state file was written, local time.
std::string saved_at(const fs::path& file) {
    std::error_code ec;
    const auto written=fs::last_write_time(file,ec);
    if(ec)return "à une date inconnue";
    const auto when=std::chrono::system_clock::now()+std::chrono::duration_cast<std::chrono::system_clock::duration>(
        written-fs::file_time_type::clock::now());
    const std::time_t t=std::chrono::system_clock::to_time_t(when);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local,&t);
#else
    localtime_r(&t,&local);
#endif
    char text[32];
    std::strftime(text,sizeof text,"le %d/%m à %H:%M",&local);
    return text;
}
// The console that runs the game: Réglages > Console may put a Game Boy game
// on a Game Boy Color, and a color game on a Game Boy Advance's GBC.
int model_of(const std::string& system,const ravenemudsk_ui::GamePreferences& prefs) {
    if(system=="gba")return RAVENEMU_GBA;
    if(system=="gb"&&!prefs.gb_in_color)return RAVENEMU_GB;
    return prefs.gbc_on_gba?RAVENEMU_GBC_ON_GBA:RAVENEMU_GBC;
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
    // The game changed the screen's mode, or turned vsync off: undone when it
    // ends, also on an error, before the launcher gets them back.
    bool mode_switched = false;
    bool vsync_off = false;
    GameScreen() = default;
    GameScreen(const GameScreen&) = delete;
    GameScreen& operator=(const GameScreen&) = delete;
    ~GameScreen() {
        if (mode_switched && window) SDL_SetWindowFullscreen(window, shared ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (vsync_off && renderer) SDL_RenderSetVSync(renderer, 1);
        if (shared) {
            ravenemudsk_ui::keep_screen({window, renderer});
            return;
        }
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }
};
// "Jouer à deux": player 2's game, beside the first and joined to it by
// RavenCore's link cable on this computer. The cable closes before the cores.
struct SecondPlayer {
    fs::path rom_path,battery_path;
    std::string title;
    Core core{nullptr,&ravenemu_destroy};
    std::unique_ptr<ravenemu_link,decltype(&ravenemu_link_destroy)> cable{nullptr,&ravenemu_link_destroy};
    std::vector<std::uint32_t> pixels,shown;
    ravenemudsk_ui::PictureFilter picture;
    Ptr<SDL_Texture,SDL_DestroyTexture> texture{nullptr,SDL_DestroyTexture};
    std::optional<ravenemudsk_ui::GameInput> input;
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
    void fit_refresh();
    void toggle_fullscreen();
    void toast(ravenemudsk_ui::Toast::Kind kind,std::string heading,std::string message="");
    bool pause_allowed();
    void handle(const SDL_Event& e);
    void handle_key(ravenemudsk_ui::KeyAction action);
    bool emulate(Clock::time_point now,bool waiting);
    void queue_sound(std::size_t samples,double speed);
    void load_second();
    void add_controller(int device);
    void use_controller(SDL_GameController* chosen);
    SDL_JoystickID owned_controller(const SDL_Event& e) const;
    SDL_JoystickID controller_of(const SDL_Event& e);
    void route_pair(const SDL_Event& e);
    int player_of(SDL_JoystickID controller) const;
    void describe_players();
    void reset_consoles();
    void start_motion();
    void feed_motion();
    void update_rumble(Clock::time_point now);
    void stop_rumble();
    void take_screenshot();
    void use_gba_bios(ravenemu_bridge* target);
    void apply_cheats();
    void cheats_changed();
    bool rewind_allowed() const;
    void keep_for_rewind();
    bool step_back();
    void save_slot();
    void load_slot();
    void undo_load();
    void select_slot(int step);
    void refresh_slot();
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
    ravenemudsk_ui::Folders folders;
    fs::path battery_path,state_path,preview_path;
    // The state slot chosen (1-9), and the game as it was before the last
    // load, for "Annuler le chargement".
    int slot=1;
    std::vector<std::uint8_t> undo_state;
    // The game's cheats, in <ROM>.cheats beside its saves.
    fs::path cheats_path;
    std::vector<ravenemudsk_ui::Cheat> cheats;
    // Why the real GBA BIOS chosen in Réglages > Console is not used, if so.
    std::string bios_notice;
    // The last moments, to play backwards while the rewind key is held.
    std::optional<ravenemudsk_ui::RewindBuffer> rewind;
    std::vector<std::uint8_t> rewind_state;
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
    // Every controller plugged in plays. The one used last names the buttons
    // on screen, rumbles, tilts the cartridge and moves its stick.
    std::vector<Ptr<SDL_GameController,SDL_GameControllerClose>> controllers;
    SDL_GameController* pad=nullptr;
    // The core's frame, and the one shown (LCD colors, blending), which the
    // screenshots and the state pictures keep too.
    std::vector<std::uint32_t> pixels,shown;
    ravenemudsk_ui::PictureFilter picture;
    std::vector<std::int16_t> pcm;
    std::optional<ravenemudsk_ui::GameOverlay> overlay;
    // An unlock's chime is mixed into the game's sound, when the settings allow it.
    std::vector<std::int16_t> chime;
    ravenemudsk_ui::SoundMixer effects;
    // The screen paces the game when its refresh is close to the console's;
    // the sound is resampled so that the device's queue stays near its target,
    // Réglages > Son's latency.
    double audio_target=.05;
    // The queue's level over about half a second: the device takes the sound
    // by blocks (31 ms for 50 ms ahead), which the correction must not follow.
    double audio_level=audio_target;
    std::optional<ravenemudsk_ui::FramePacer> pacer;
    ravenemudsk_ui::Resampler resampler;
    std::vector<std::int16_t> resampled;
    std::optional<ravenemudsk_ui::GameInput> input;
    // "Jouer à deux", and who plays what: the keyboard plays player 1, and so
    // does the first controller when there are two; a lone controller plays
    // player 2. Swapping exchanges them all. One game is heard, the other's
    // sound dropped.
    std::optional<SecondPlayer> second;
    bool swapped=false;
    int heard=0;
    std::vector<std::int16_t> unheard;
    // The cartridge's sensors (RAVENEMU_SENSOR_*) and motor, on the controller.
    int sensors=0;
    ravenemudsk_ui::MotionInput motion;
    bool rumbling=false;
    Clock::time_point rumble_refresh;
    ravenemudsk_ui::OverlayInfo info{};
    // Closed before the core it plugs into.
    std::unique_ptr<ravenemudsk_link::WirelessLink> link;
    bool running=true,paused=false;
    // Paused because the window went to the background: it resumes with it.
    bool paused_unfocused=false;
    Clock::time_point next,last_save,next_progress,last_frame,last_present;
    Clock::duration duration{};
    // Refreshes shown since the last emulated frame, while the screen paces
    // the game: one frame every pacer->vsyncs() of them.
    int refreshes=0;
    // Réglages > Affichage, "Réduire la latence": the wait after a refresh
    // before the next frame reads the controls, from what a frame takes
    // (smoothed) and a margin that grows when a picture misses its refresh.
    double frame_work=0,delay_margin=.002;
};
GameSession::GameSession(const ravenemudsk_ui::LaunchRequest& request) : req(request) {
    rom_path=ravenemudsk_ui::from_utf8(req.rom);
    rom=read_file(rom_path,64u<<20);
    verify(ravenemu_api_version()==RAVENEMU_API_VERSION,"Version ABI RavenCore");
    system_of(req.system); // an unknown console stops here, before anything opens
    prefs=ravenemudsk_ui::game_preferences();
    core=Core(ravenemu_create(model_of(req.system,prefs)),&ravenemu_destroy);
    verify(bool(core),"Création RavenCore");
    folders=ravenemudsk_ui::app_folders();
    battery_path=ravenemudsk_ui::save_path(folders,rom_path,".sav");
    preview_path=ravenemudsk_ui::preview_path(folders,rom_path);
    if(req.system=="gb"&&prefs.gb_in_color)ravenemu_set_gbc_colors(core.get(),prefs.gb_colors);
    if(req.system=="gba"&&prefs.gba_bios>0)use_gba_bios(core.get());
    const auto battery=read_optional(battery_path,4u<<20);
    verify(ravenemu_load(core.get(),rom.data(),rom.size(),battery.empty()?nullptr:battery.data(),battery.size())!=0,
           "Chargement ROM");
    w=ravenemu_video_width(core.get());h=ravenemu_video_height(core.get());
    hz=ravenemu_audio_rate(core.get());
    fps=ravenemu_video_fps(core.get());
    if(w<1||w>1024||h<1||h>1024||!std::isfinite(fps)||fps<30||fps>120||hz<8000||hz>192000)
        throw std::runtime_error("Format audio ou vidéo RavenCore invalide");
    if(!req.partner_rom.empty())load_second();
    sdl.emplace();
    title=ravenemudsk_ui::display_title({rom_path,req.system});
    if(second)title+=" + "+second->title;
    open_screen();
    // Black at once, before the first emulated frame.
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
    texture.reset(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h));
    if(!texture)throw std::runtime_error(SDL_GetError());
    // Sharp pixels whatever scaling hint the launcher left behind.
    SDL_SetTextureScaleMode(texture.get(),prefs.smooth_image?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
    if(second) {
        second->texture.reset(SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,w,h));
        if(!second->texture)throw std::runtime_error(SDL_GetError());
        SDL_SetTextureScaleMode(second->texture.get(),prefs.smooth_image?SDL_ScaleModeLinear:SDL_ScaleModeNearest);
        second->picture.configure(prefs.lcd_colors&&model_of(req.partner_system,prefs)!=RAVENEMU_GB,
                                  static_cast<ravenemudsk_ui::FrameBlending>(prefs.frame_blending));
    }
    SDL_AudioSpec spec{};
    spec.freq=hz;spec.format=AUDIO_S16SYS;spec.channels=2;
    // Blocks of at most five eighths of the target, a power of two: 1024
    // samples at 32768 Hz for 50 ms. The queue never runs dry between two.
    audio_target=audio_level=prefs.audio_latency/1000.0;
    spec.samples=256;
    while(spec.samples<4096&&spec.samples*2<=hz*audio_target/1.6)spec.samples*=2;
    const char* output=prefs.audio_output.empty()?nullptr:prefs.audio_output.c_str();
    audio_device=SDL_OpenAudioDevice(output,0,&spec,nullptr,0);
    // The chosen output unplugged: the system's, said once the game shows.
    const bool output_missing=output&&!audio_device;
    if(output_missing)audio_device=SDL_OpenAudioDevice(nullptr,0,&spec,nullptr,0);
    if(audio_device)SDL_PauseAudioDevice(audio_device,0);
    for(int i=0;i<SDL_NumJoysticks();++i)add_controller(i);
    sensors=ravenemu_sensors(core.get());
    if(sensors&RAVENEMU_SENSOR_SOLAR)ravenemu_set_solar_light(core.get(),prefs.solar_light*255/100);
    start_motion();
    // RetroAchievements follow single games only.
    if(!second)ravenemudsk_ra::client().start_game(system_of(req.system),rom,core.get(),req.rom);
    pixels.resize(static_cast<std::size_t>(w)*h);
    shown.resize(pixels.size());
    picture.configure(prefs.lcd_colors&&model_of(req.system,prefs)!=RAVENEMU_GB,
                      static_cast<ravenemudsk_ui::FrameBlending>(prefs.frame_blending));
    pcm.resize(8192);
    overlay.emplace(window,renderer,&ravenemudsk_ra::client(),req.fullscreen);
    if(!bios_notice.empty())toast(ravenemudsk_ui::Toast::Kind::Info,"BIOS de RavenCore",bios_notice);
    if(output_missing)
        toast(ravenemudsk_ui::Toast::Kind::Info,"Sortie audio débranchée","Le son passe par celle du système.");
    if(!second) {
        cheats_path=ravenemudsk_ui::save_path(folders,rom_path,".cheats");
        cheats=ravenemudsk_ui::load_cheats(cheats_path);
        info.cheats=&cheats;info.cheat_system=system_of(req.system);
        apply_cheats();
    }
    chime=prefs.unlock_sound?ravenemudsk_ui::unlock_chime(hz):std::vector<std::int16_t>{};
    input.emplace([this](int button, bool pressed) {
        ravenemu_set_button(core.get(),button,pressed);
    },prefs.keyboard,prefs.pads);
    // The stick tilts and turns the cartridges that have sensors.
    const bool tilting=!second&&(sensors&(RAVENEMU_SENSOR_TILT|RAVENEMU_SENSOR_ROTATION));
    input->set_stick_dpad(prefs.stick_dpad&&!tilting?prefs.stick_dead_zone:0);
    if(second) {
        second->input.emplace([this](int button, bool pressed) {
            ravenemu_set_button(second->core.get(),button,pressed);
        },prefs.keyboard,prefs.pads);
        second->input->set_stick_dpad(prefs.stick_dpad?prefs.stick_dead_zone:0);
        info.two_players=true;
        describe_players();
    }
    info.game_title=title;info.system=req.system;
    refresh_slot();
    info.keyboard=prefs.keyboard;info.pads=prefs.pads;info.pad=family_of(pad);
    next=last_save=next_progress=last_frame=last_present=Clock::now();
    duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0/fps));
    pacer.emplace(fps);
    // 64 MB: from half a minute of action to several minutes of quieter play.
    if(prefs.rewind&&!second)rewind.emplace(64u<<20);
    // Idle until the game uses its link port: then it looks for a partner.
    if(prefs.wireless_link&&!second) {
        link=std::make_unique<ravenemudsk_link::WirelessLink>(ravenemudsk_link::WirelessLink::Game{core.get(),
            &core_mutex,req.system=="gba"?ravenemudsk_link::family_gba:ravenemudsk_link::family_gb,title});
        info.link_own_label=link->view().own_label;
    }
}
GameSession::~GameSession() {
    stop_rumble();
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
    else {
        // The screen chosen in Réglages > Affichage, while it is connected.
        const int display=prefs.display>=0&&prefs.display<SDL_GetNumVideoDisplays()?prefs.display:0;
        // Four times the console, three for two games side by side, within
        // nine tenths of the screen.
        int width=std::max(640,second?(2*w+w/16)*3:w*4),height=std::max(520,h*(second?3:4));
        SDL_Rect usable{};
        if(SDL_GetDisplayUsableBounds(display,&usable)==0&&usable.w>0&&usable.h>0) {
            width=std::min(width,usable.w*9/10);height=std::min(height,usable.h*9/10);
        }
        screen.window=SDL_CreateWindow(("RavenEmu - "+title).c_str(),
        SDL_WINDOWPOS_CENTERED_DISPLAY(display),SDL_WINDOWPOS_CENTERED_DISPLAY(display),width,height,
        SDL_WINDOW_SHOWN|SDL_WINDOW_RESIZABLE|(req.fullscreen?SDL_WINDOW_FULLSCREEN_DESKTOP:0));
    }
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
    if(!screen.renderer) {
        screen.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
        ravenemudsk_ui::limit_frame_queue(screen.renderer);
    }
    if(!screen.renderer)screen.renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!screen.renderer)throw std::runtime_error(SDL_GetError());
    renderer=screen.renderer;
    // A G-Sync or FreeSync screen follows the pictures: each one is shown
    // when the game has it, without waiting for a fixed refresh.
    if(prefs.variable_refresh&&SDL_RenderSetVSync(renderer,0)==0)screen.vsync_off=true;
    else if(req.fullscreen&&prefs.refresh_switch)fit_refresh();
}
// Full screen on a screen whose refresh the game's frames do not fall on
// (144 or 165 Hz): the same size at a rate they do, 120 Hz or else 60 Hz, in
// exclusive full screen. Left as it is when none fits or the switch fails.
void GameSession::fit_refresh() {
    const int display=SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode desktop{};
    if(display<0||SDL_GetDesktopDisplayMode(display,&desktop)!=0)return;
    if(ravenemudsk_ui::vsyncs_per_frame(desktop.refresh_rate,fps)>0)return;
    std::vector<SDL_DisplayMode> modes;
    std::vector<int> rates;
    for(int i=0;i<SDL_GetNumDisplayModes(display);++i) {
        SDL_DisplayMode mode{};
        if(SDL_GetDisplayMode(display,i,&mode)==0&&mode.w==desktop.w&&mode.h==desktop.h) {
            modes.push_back(mode);rates.push_back(mode.refresh_rate);
        }
    }
    const int rate=ravenemudsk_ui::fitting_refresh(rates,desktop.refresh_rate,fps);
    const auto mode=std::find_if(modes.begin(),modes.end(),[rate](const SDL_DisplayMode& m){return m.refresh_rate==rate;});
    if(rate<=0||mode==modes.end())return;
    if(SDL_SetWindowDisplayMode(window,&*mode)==0&&SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN)==0)
        screen.mode_switched=true;
    else SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
}
// F11: full screen on the chosen screen, at a refresh the game falls on, or
// back to a window (and the screen's own mode).
void GameSession::toggle_fullscreen() {
    if(SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(window,0);
        screen.mode_switched=false;
        return;
    }
    SDL_SetWindowFullscreen(window,SDL_WINDOW_FULLSCREEN_DESKTOP);
    if(prefs.refresh_switch&&!screen.vsync_off)fit_refresh();
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
    const auto seconds=[](Clock::duration d){return std::chrono::duration<double>(d).count();};
    while(running) {
        const auto started=Clock::now();
        SDL_Event e{};
        while(SDL_PollEvent(&e))handle(e);
        input->update(SDL_GetTicks());
        if(link)update_link();
        // Neither faster nor backwards with two games: both would have to agree.
        info.fast_forward=input->fast_forward()&&info.link_partner.empty()&&!paused&&!second;
        info.rewinding=input->rewind()&&rewind_allowed()&&!paused;
        if(!info.rewinding)info.rewind_end=false;
        const auto now=Clock::now();
        // Hardcore waits for RetroAchievements, which two games do without.
        const bool waiting=!second&&ravenemudsk_ra::client().frames_on_hold();
        const bool produced=emulate(now,waiting);
        const bool idle=waiting||paused||overlay->menu_open()||link_holds();
        if(screen.vsync_off&&!produced&&!idle) {
            // Variable refresh: a picture only with a new frame, shown the
            // moment it is due, so that the screen refreshes on the game's beat.
            core_lock.unlock();
            const auto due=next;
            while(Clock::now()+std::chrono::milliseconds(2)<due)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            while(Clock::now()<due)std::this_thread::yield();
            core_lock.lock();
            continue;
        }
        take_retro_events();
        auto& retro=ravenemudsk_ra::client();
        if(now>=next_progress) {
            overlay->update_retro_progress(retro.snapshot().achievements);
            next_progress=now+std::chrono::milliseconds(350);
        }
        if(now-last_save>std::chrono::seconds(30)) {
            save_battery(core.get(),battery_path);last_save=now;
            if(second)save_battery(second->core.get(),second->battery_path);
        }
        draw(waiting);
        core_lock.unlock();
        if(produced)frame_work+=(seconds(Clock::now()-started)-frame_work)/16;
        SDL_RenderPresent(renderer);
        // The screen's refresh, measured between two pictures of a running
        // game at its own speed, apart from the loop's own sleep.
        const auto shown=Clock::now();
        const bool still=waiting||paused||overlay->menu_open()||link_holds();
        // Without vsync (variable refresh), the game keeps its own clock.
        if(!still&&!was_still&&!info.fast_forward&&!info.rewinding&&!screen.vsync_off)
            pacer->presented(seconds(shown-last_present),seconds(shown-last_present-slept));
        if(!still)++refreshes;
        // Back in the game, the controller's position counts as level again;
        // stopped, its motor stops.
        if(!still&&was_still)motion.recenter();
        if(still&&!was_still)stop_rumble();
        const double interval=pacer->locked()?1./pacer->refresh():0.;
        // A frame that came a refresh late: wait less before the next ones.
        if(produced&&interval>0&&seconds(shown-last_present)>1.5*interval)
            delay_margin=std::min(delay_margin+.001,.008);
        else if(produced)delay_margin=std::max(.002,delay_margin-.0001);
        last_present=shown;was_still=still;slept={};
        if(still)SDL_Delay(16);
        else if(!pacer->locked()&&Clock::now()<next) {
            const auto before=Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            slept=Clock::now()-before;
        } else if(prefs.frame_delay&&interval>0&&refreshes>=pacer->vsyncs()) {
            // "Réduire la latence": the next frame reads the controls as late
            // as the refresh allows, its work and a margin before the next one.
            const double wait=std::clamp(interval-1.25*frame_work-delay_margin,0.,.75*interval);
            const auto until=shown+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(wait));
            while(Clock::now()+std::chrono::milliseconds(2)<until)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            // Not counted as sleep: the next picture still waits for its refresh.
            while(Clock::now()<until)std::this_thread::yield();
        }
        core_lock.lock();
    }
    finish();
}
void GameSession::handle(const SDL_Event& e) {
    overlay->process_event(e);
    if(e.type==SDL_QUIT)running=false;
    // In the background the game pauses (RetroArch's default too), and
    // resumes when its window comes back, unless the player paused it.
    if(e.type==SDL_WINDOWEVENT&&prefs.pause_unfocused) {
        if(e.window.event==SDL_WINDOWEVENT_FOCUS_LOST&&!paused&&!overlay->menu_open()&&pause_allowed())
            paused=paused_unfocused=true;
        else if(e.window.event==SDL_WINDOWEVENT_FOCUS_GAINED&&paused_unfocused)
            paused=paused_unfocused=false;
    }
    if(e.type==SDL_CONTROLLERDEVICEADDED)add_controller(e.cdevice.which);
    if(e.type==SDL_CONTROLLERDEVICEREMOVED) {
        const auto gone=std::find_if(controllers.begin(),controllers.end(),
                                     [&](const auto& c){return instance_of(c.get())==e.cdevice.which;});
        if(gone!=controllers.end()) {
            input->release_all();
            if(gone->get()==pad){pad=nullptr;rumbling=false;}
            controllers.erase(gone);
            if(second) {
                second->input->release_all();
                if(!pad&&!controllers.empty())pad=controllers.front().get();
                info.pad=family_of(pad);
                describe_players();
            } else {
                if(!pad&&!controllers.empty())use_controller(controllers.front().get());
                if(!pad){info.pad.reset();motion=ravenemudsk_ui::MotionInput{};}
            }
        }
    }
    // "Relier le câble à…" takes the keys while it is up.
    const bool choosing=!info.link_choices.empty()&&!overlay->menu_open();
    if(e.type==SDL_KEYDOWN && !e.key.repeat) {
        using Key=ravenemudsk_ui::KeyAction;
        handle_key(choosing||(overlay->menu_open()&&e.key.keysym.sym!=SDLK_ESCAPE)?
            Key::Count:prefs.keyboard.action(e.key.keysym.sym));
    }
    if(second) {
        route_pair(e);
        return;
    }
    // Back and Start together, in either order, open or close the menu,
    // and neither reaches the game. The Guide button stays with the
    // system: Windows opens the Xbox Game Bar on it (Steam its overlay),
    // which would show too.
    input->set_menu_open(overlay->menu_open()||paused||choosing);
    if(input->process_event(e,controller_of(e))) {
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
                paused=!paused;paused_unfocused=false;input->release_all();
            }
            break;
        case Key::Reset:reset_consoles();break;
        // States and screenshots belong to single games.
        case Key::SaveState:if(!second)save_slot();break;
        case Key::LoadState:if(!second)load_slot();break;
        case Key::PreviousSlot:if(!second)select_slot(-1);break;
        case Key::NextSlot:if(!second)select_slot(1);break;
        case Key::Screenshot:if(!second)take_screenshot();break;
        case Key::Fullscreen:toggle_fullscreen();break;
        default:break;
    }
}
// One emulated frame when it is due: every pacer->vsyncs() refreshes while the
// screen paces the game (each one at 60 Hz, every other one at 120 Hz), else on
// the game's own clock; several while fast forward is held, except over the
// link cable. Their sound goes out with the chime and the volume, as fast;
// between frames the SDK still runs. True when a new picture is ready.
bool GameSession::emulate(Clock::time_point now,bool waiting) {
    const bool due=pacer->locked()?refreshes>=pacer->vsyncs():now>=next;
    if(!waiting&&!paused&&!overlay->menu_open()&&!link_holds() && due && info.rewinding) {
        // Backwards, without sound; the SDK waits meanwhile.
        info.rewind_end=!step_back();
        ravenemudsk_ra::client().pump(false);
        last_frame=now;refreshes=0;
        next=pacer->locked()?now+duration:next+duration;
        if(now>next+std::chrono::milliseconds(150))next=now+duration;
        return true;
    } else if(!waiting&&!paused&&!overlay->menu_open()&&!link_holds() && due) {
        const int frames=info.fast_forward?ravenemudsk_ui::fast_forward_speed:1;
        for(int frame=0;frame<frames;++frame) {
            feed_motion();
            if(second) {
                // Both games at once, over the cable; one of them is heard.
                std::uint32_t* const out[]={pixels.data(),second->pixels.data()};
                verify(ravenemu_link_run_frame(second->cable.get(),out,pixels.size())!=0,"Frame");
                second->input->next_frame();
                unheard.resize(pcm.size());
                ravenemu_read_audio(heard==0?second->core.get():core.get(),unheard.data(),unheard.size());
            } else {
                verify(ravenemu_run_frame(core.get(),pixels.data(),pixels.size())!=0,"Frame");
                update_rumble(now);
            }
            input->next_frame();
            if(link)link->set_wanted(ravenemu_link_wanted(core.get())!=0);
            ravenemudsk_ra::client().pump(true);
            const auto n=ravenemu_read_audio(heard==1&&second?second->core.get():core.get(),pcm.data(),pcm.size());
            effects.mix(pcm.data(),n);
            ravenemudsk_ui::scale_volume(pcm.data(),n,prefs.volume);
            queue_sound(n,frames);
            keep_for_rewind();
        }
        picture.apply(pixels.data(),shown.data(),pixels.size());
        SDL_UpdateTexture(texture.get(),nullptr,shown.data(),w*static_cast<int>(sizeof(std::uint32_t)));
        if(second) {
            second->picture.apply(second->pixels.data(),second->shown.data(),second->pixels.size());
            SDL_UpdateTexture(second->texture.get(),nullptr,second->shown.data(),w*static_cast<int>(sizeof(std::uint32_t)));
        }
        last_frame=now;refreshes=0;
        next=pacer->locked()?now+duration:next+duration;
        if(now>next+std::chrono::milliseconds(150))next=now+duration;
        return true;
    }
    ravenemudsk_ra::client().pump(false);
    return false;
}
// The frame's sound, resampled a little faster or slower so that the device's
// queue stays near its target. A queue run dry (start, pause, slowdown) first
// gets its target in silence; one left far over it by a stall starts afresh.
void GameSession::queue_sound(std::size_t samples,double speed) {
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
    resampler.process(pcm.data(),samples/2,
                      pacer->audio_ratio()*ravenemudsk_ui::rate_correction(audio_level,audio_target)/speed,resampled);
    SDL_QueueAudio(audio_device,resampled.data(),static_cast<Uint32>(resampled.size()*sizeof(std::int16_t)));
}
// "Jouer à deux": player 2's game with its battery save, linked to the
// first one: two different cartridges of the same family, the same size and
// sound.
void GameSession::load_second() {
    if(!ravenemudsk_ui::linkable(req.system,req.partner_system))
        throw std::runtime_error("Ces deux jeux ne se relient pas : Game Boy et Game Boy Color ensemble, "
                                 "ou deux Game Boy Advance.");
    auto& p=second.emplace();
    p.rom_path=ravenemudsk_ui::from_utf8(req.partner_rom);
    std::error_code same;
    if(fs::equivalent(rom_path,p.rom_path,same))
        throw std::runtime_error("Choisissez deux jeux différents : une cartouche n’a qu’une sauvegarde.");
    p.title=ravenemudsk_ui::display_title({p.rom_path,req.partner_system});
    const auto bytes=read_file(p.rom_path,64u<<20);
    p.core=Core(ravenemu_create(model_of(req.partner_system,prefs)),&ravenemu_destroy);
    verify(bool(p.core),"Création RavenCore");
    if(req.partner_system=="gb"&&prefs.gb_in_color)ravenemu_set_gbc_colors(p.core.get(),prefs.gb_colors);
    if(req.partner_system=="gba"&&prefs.gba_bios>0)use_gba_bios(p.core.get());
    p.battery_path=ravenemudsk_ui::save_path(folders,p.rom_path,".sav");
    const auto battery=read_optional(p.battery_path,4u<<20);
    verify(ravenemu_load(p.core.get(),bytes.data(),bytes.size(),battery.empty()?nullptr:battery.data(),battery.size())!=0,
           "Chargement ROM");
    if(ravenemu_video_width(p.core.get())!=w||ravenemu_video_height(p.core.get())!=h||
       ravenemu_audio_rate(p.core.get())!=hz)
        throw std::runtime_error("Les deux jeux n’ont pas le même format audio ou vidéo");
    ravenemu_bridge* const handles[]={core.get(),p.core.get()};
    p.cable.reset(ravenemu_link_create(handles,2));
    verify(bool(p.cable),"Câble link");
    p.pixels.resize(static_cast<std::size_t>(w)*h);
    p.shown.resize(p.pixels.size());
}
// A controller plugged in, at the start or later, once: SDL announces again
// those already there when the game opens it. With two games, who plays what
// may change: both let go of everything.
void GameSession::add_controller(int device) {
    if(!SDL_IsGameController(device))return;
    const auto id=SDL_JoystickGetDeviceInstanceID(device);
    for(const auto& c:controllers)if(instance_of(c.get())==id)return;
    Ptr<SDL_GameController,SDL_GameControllerClose> opened{SDL_GameControllerOpen(device),SDL_GameControllerClose};
    if(!opened)return;
    controllers.push_back(std::move(opened));
    if(!pad) {
        pad=controllers.back().get();info.pad=family_of(pad);
        if(input)start_motion();
    }
    if(second&&second->input) {
        input->release_all();second->input->release_all();
        describe_players();
    }
}
// The player (0 or 1) a controller plays in "Jouer à deux".
int GameSession::player_of(SDL_JoystickID controller) const {
    const auto at=std::find_if(controllers.begin(),controllers.end(),
                               [&](const auto& c){return instance_of(c.get())==controller;});
    const int player=controllers.size()==1||at!=controllers.begin()?1:0;
    return swapped?1-player:player;
}
// "Joueur 1 : clavier  ·  Joueur 2 : manette 1", for the menu.
void GameSession::describe_players() {
    std::string players[2];
    const auto add=[&](int player,const std::string& device) {
        players[player]+=(players[player].empty()?"":", ")+device;
    };
    add(swapped?1:0,"clavier");
    for(std::size_t i=0;i<controllers.size();++i)
        add(player_of(instance_of(controllers[i].get())),"manette "+std::to_string(i+1));
    info.players_note="Joueur 1 : "+(players[0].empty()?std::string("aucune commande"):players[0])+"  \xC2\xB7  Joueur 2 : "+
                      (players[1].empty()?std::string("aucune commande"):players[1]);
}
// Another controller takes over: what the previous one held is let go, its
// motor and sensors stop.
void GameSession::use_controller(SDL_GameController* chosen) {
    if(chosen==pad)return;
    if(pad) {
        stop_rumble();
        for(const auto type:{SDL_SENSOR_ACCEL,SDL_SENSOR_GYRO})
            if(SDL_GameControllerIsSensorEnabled(pad,type))SDL_GameControllerSetSensorEnabled(pad,type,SDL_FALSE);
        input->release_all();
    }
    pad=chosen;info.pad=family_of(pad);
    start_motion();
}
// The game's controller behind an event, or -1: any of them for a button,
// which hands it over; for a stick or a trigger, the one in use only. An axis
// never hands over: a badly calibrated device could otherwise take it all
// the time.
SDL_JoystickID GameSession::controller_of(const SDL_Event& e) {
    const SDL_JoystickID which=owned_controller(e);
    if(which<0)return -1;
    SDL_GameController* owned=SDL_GameControllerFromInstanceID(which);
    if(owned!=pad) {
        // Its releases: of what the handover already let go.
        if(e.type!=SDL_CONTROLLERBUTTONDOWN)return -1;
        use_controller(owned);
    }
    return which;
}
// The game's controller behind a button or an axis, or -1.
SDL_JoystickID GameSession::owned_controller(const SDL_Event& e) const {
    SDL_JoystickID which=-1;
    if(e.type==SDL_CONTROLLERBUTTONDOWN||e.type==SDL_CONTROLLERBUTTONUP)which=e.cbutton.which;
    else if(e.type==SDL_CONTROLLERAXISMOTION)which=e.caxis.which;
    else return -1;
    for(const auto& c:controllers)if(instance_of(c.get())==which)return which;
    return -1;
}
// "Jouer à deux": the keyboard's keys to its player, a controller's input to
// its own; the rest (focus lost) to both. The menu chord of either opens the
// menu, which stops both games.
void GameSession::route_pair(const SDL_Event& e) {
    auto& one=*input;
    auto& two=*second->input;
    const bool held=overlay->menu_open()||paused;
    one.set_menu_open(held);two.set_menu_open(held);
    bool chord=false;
    if(e.type==SDL_KEYDOWN||e.type==SDL_KEYUP)chord=(swapped?two:one).process_event(e);
    else if(const auto which=owned_controller(e);which>=0) {
        // The last one pressed names the buttons in the menu.
        if(e.type==SDL_CONTROLLERBUTTONDOWN){pad=SDL_GameControllerFromInstanceID(which);info.pad=family_of(pad);}
        chord=(player_of(which)==0?one:two).process_event(e,which);
    } else {
        one.process_event(e);two.process_event(e);
    }
    if(chord) {
        if(overlay->menu_open())overlay->close_menu();
        else overlay->open_menu(true);
        one.release_all();two.release_all();
    }
}
// Réinitialiser: both consoles in "Jouer à deux".
void GameSession::reset_consoles() {
    input->release_all();
    verify(ravenemu_reset(core.get())!=0,"Reset");
    if(second) {
        second->input->release_all();
        verify(ravenemu_reset(second->core.get())!=0,"Reset");
    }
    ravenemudsk_ra::client().reset();overlay->clear_retro();
}
// The controller's accelerometer and gyroscope, for a game that tilts or
// turns: switched on (they cost battery), the controller's position as level.
void GameSession::start_motion() {
    motion.recenter();
    if(!pad||!prefs.motion_sensors)return;
    if((sensors&RAVENEMU_SENSOR_TILT)&&SDL_GameControllerHasSensor(pad,SDL_SENSOR_ACCEL))
        SDL_GameControllerSetSensorEnabled(pad,SDL_SENSOR_ACCEL,SDL_TRUE);
    if((sensors&RAVENEMU_SENSOR_ROTATION)&&SDL_GameControllerHasSensor(pad,SDL_SENSOR_GYRO))
        SDL_GameControllerSetSensorEnabled(pad,SDL_SENSOR_GYRO,SDL_TRUE);
}
// Before each frame: the left stick and the controller's sensors tilt and
// turn the cartridge.
void GameSession::feed_motion() {
    if(!(sensors&(RAVENEMU_SENSOR_TILT|RAVENEMU_SENSOR_ROTATION)))return;
    if(pad) {
        motion.stick(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),
                     SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY));
        // A sensor that is off reads level and still.
        float acceleration[3]={0.f,SDL_STANDARD_GRAVITY,0.f},rotation[3]={0.f,0.f,0.f};
        bool sensed=false;
        for(const auto [type,values]:{std::pair{SDL_SENSOR_ACCEL,acceleration},std::pair{SDL_SENSOR_GYRO,rotation}})
            if(SDL_GameControllerIsSensorEnabled(pad,type)&&SDL_GameControllerGetSensorData(pad,type,values,3)==0)
                sensed=true;
        if(sensed)motion.sense(acceleration,rotation);
    } else motion.stick(0,0);
    const auto reading=motion.reading();
    if(sensors&RAVENEMU_SENSOR_TILT)ravenemu_set_tilt(core.get(),reading.tilt_x,reading.tilt_y);
    if(sensors&RAVENEMU_SENSOR_ROTATION)ravenemu_set_rotation(core.get(),reading.rotation);
}
// The cartridge's motor on the controller, at Réglages > Commandes' strength:
// started when it starts, renewed every half second while it runs (each order
// lasts one), stopped with it.
void GameSession::update_rumble(Clock::time_point now) {
    const bool on=pad&&prefs.rumble&&ravenemu_rumble_active(core.get());
    if(on&&(!rumbling||now>=rumble_refresh)) {
        const auto strength=static_cast<Uint16>(0xffff*prefs.rumble_strength/100);
        SDL_GameControllerRumble(pad,strength,strength,1000);
        rumble_refresh=now+std::chrono::milliseconds(500);
    }
    if(!on)stop_rumble();
    rumbling=on;
}
void GameSession::stop_rumble() {
    if(rumbling&&pad)SDL_GameControllerRumble(pad,0,0,0);
    rumbling=false;
}
// The cheats that are on, from the next frame; none in Hardcore, where
// RetroAchievements forbids them.
void GameSession::apply_cheats() {
    std::vector<const char*> codes;
    if(!ravenemudsk_ra::client().snapshot().hardcore_preferred)
        for(const auto& cheat:cheats)if(cheat.enabled)codes.push_back(cheat.code.c_str());
    if(!ravenemu_set_cheats(core.get(),codes.data(),codes.size()))
        toast(ravenemudsk_ui::Toast::Kind::Error,"Code de triche refusé",ravenemu_last_error());
}
// Added, switched or deleted in "Codes de triche": kept for the next time,
// and in play at once.
void GameSession::cheats_changed() {
    try {
        ravenemudsk_ui::save_cheats(cheats_path,cheats);
    } catch(const std::exception& e) {
        toast(ravenemudsk_ui::Toast::Kind::Error,"Codes de triche non enregistrés",e.what());
    }
    apply_cheats();
}
// The player's own GBA BIOS, BIOS/gba_bios.bin, before the game loads; RavenCore's
// own answers the BIOS calls when it is missing or not a BIOS.
void GameSession::use_gba_bios(ravenemu_bridge* target) {
    const auto file=folders.bios/"gba_bios.bin";
    std::vector<std::uint8_t> bios;
    try { bios=read_optional(file,1u<<20); } catch(const std::exception&) { bios.clear(); }
    if(bios.empty())bios_notice="Aucun gba_bios.bin dans le dossier bios : le jeu démarre avec le BIOS de RavenCore.";
    else if(!ravenemu_set_gba_bios(target,bios.data(),bios.size(),prefs.gba_bios==2))
        bios_notice="gba_bios.bin ne fait pas 16 Kio : le jeu démarre avec le BIOS de RavenCore.";
}
// Not in Hardcore (RetroAchievements forbids it), nor over the link cable,
// where the other game goes on.
bool GameSession::rewind_allowed() const {
    return rewind&&!ravenemudsk_ra::client().snapshot().hardcore_preferred&&info.link_partner.empty();
}
void GameSession::keep_for_rewind() {
    if(!rewind_allowed())return;
    rewind_state.resize(ravenemu_state_size(core.get()));
    if(!rewind_state.empty()&&ravenemu_save_state(core.get(),rewind_state.data(),rewind_state.size()))
        rewind->push(rewind_state);
}
// One frame back: the state before, then that frame run again for its
// picture, its sound dropped. False once no older moment is kept.
bool GameSession::step_back() {
    if(!rewind||!rewind->pop(rewind_state))return false;
    verify(ravenemu_load_state(core.get(),rewind_state.data(),rewind_state.size())!=0,"Retour arrière");
    verify(ravenemu_run_frame(core.get(),pixels.data(),pixels.size())!=0,"Frame");
    ravenemu_read_audio(core.get(),pcm.data(),pcm.size());
    picture.apply(pixels.data(),shown.data(),pixels.size());
    SDL_UpdateTexture(texture.get(),nullptr,shown.data(),w*static_cast<int>(sizeof(std::uint32_t)));
    return true;
}
// The chosen slot's state, with the picture of the game at that moment.
void GameSession::save_slot() {
    save_state(core.get(),state_path);
    ravenemudsk_ui::save_png(ravenemudsk_ui::state_picture_path(state_path),shown.data(),w,h);
    refresh_slot();
    toast(ravenemudsk_ui::Toast::Kind::Success,"État sauvegardé","Emplacement "+std::to_string(slot));
}
// The chosen slot's state, unless Hardcore or the link cable forbid it. The
// game as it was stays at hand for "Annuler le chargement".
void GameSession::load_slot() {
    using Kind=ravenemudsk_ui::Toast::Kind;
    if(ravenemudsk_ra::client().snapshot().hardcore_preferred) {toast(Kind::Info,"Hardcore","Chargement interdit");return;}
    if(!info.link_partner.empty()) {
        toast(Kind::Info,"Câble sans fil relié","Le chargement d'une sauvegarde d'état est désactivé.");
        return;
    }
    if(!info.has_state) {toast(Kind::Info,"Emplacement "+std::to_string(slot)+" vide");return;}
    try {
        auto before=ravenemudsk_ui::encode_game_state(ravenemudsk_ui::capture_game_state(core.get(),ravenemudsk_ra::client()));
        const bool restored=load_state(core.get(),state_path);
        undo_state=std::move(before);info.can_undo_load=true;
        input->release_all();overlay->clear_retro();
        toast(Kind::Success,"État chargé  \xC2\xB7  emplacement "+std::to_string(slot),
              restored?"Progression RetroAchievements restaurée":"Conditions RetroAchievements réinitialisées");
    } catch(const std::exception& e) {
        toast(Kind::Error,"État illisible",e.what());
    }
}
// Back to the game as it was before the last load, once.
void GameSession::undo_load() {
    if(undo_state.empty()||ravenemudsk_ra::client().snapshot().hardcore_preferred||!info.link_partner.empty())return;
    try {
        ravenemudsk_ui::restore_game_state(core.get(),ravenemudsk_ra::client(),undo_state);
        input->release_all();overlay->clear_retro();
        toast(ravenemudsk_ui::Toast::Kind::Success,"Chargement annulé","La partie reprend où elle en était.");
    } catch(const std::exception& e) {
        toast(ravenemudsk_ui::Toast::Kind::Error,"Annulation impossible",e.what());
    }
    undo_state.clear();info.can_undo_load=false;
}
// The next or previous of the nine slots, said in a notification when the
// menu is closed (open, the menu shows it).
void GameSession::select_slot(int step) {
    slot=(slot-1+step+ravenemudsk_ui::state_slots)%ravenemudsk_ui::state_slots+1;
    refresh_slot();
    if(!overlay->menu_open())
        toast(ravenemudsk_ui::Toast::Kind::Info,"Emplacement "+std::to_string(slot),
              info.has_state?"Sauvegardé "+info.state_saved:"Vide");
}
void GameSession::refresh_slot() {
    state_path=ravenemudsk_ui::state_path(folders,rom_path,slot);
    info.state_slot=slot;
    info.has_state=fs::exists(state_path);
    info.state_saved=info.has_state?saved_at(state_path):std::string();
    info.state_picture=ravenemudsk_ui::state_picture_path(state_path);
}
// The last frame of the game, without the menu or notifications, at the
// console's resolution, in Captures.
void GameSession::take_screenshot() {
    try {
        const auto path=ravenemudsk_ui::screenshot_path(folders,title,std::time(nullptr));
        if(!ravenemudsk_ui::save_png(path,shown.data(),w,h))throw std::runtime_error("Écriture impossible");
        toast(ravenemudsk_ui::Toast::Kind::Success,"Capture d'écran enregistrée",ravenemudsk_ui::utf8(path.filename()));
    } catch(const std::exception& e) {
        toast(ravenemudsk_ui::Toast::Kind::Error,"Capture d'écran impossible",e.what());
    }
}
void GameSession::take_retro_events() {
    for(const auto& event:ravenemudsk_ra::client().drain_events()) {
        if(event.kind=="hardcore_refused"){
            apply_cheats();
            if(!ravenemudsk_ui::remember_hardcore(false))
                toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le Hardcore reste sélectionné dans les réglages.");
            continue;
        }
        if(event.kind=="hardcore_reset"){
            apply_cheats();
            input->release_all();paused=paused_unfocused=false;next=Clock::now();
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
    if(second) {
        // Player 1 on the left, player 2 on the right, a sixteenth apart.
        const int gap=w/16;
        const SDL_Rect both=ravenemudsk_ui::picture_rect(out_w,out_h,2*w+gap,h,prefs.integer_scale);
        const int one=both.w*w/(2*w+gap);
        const SDL_Rect left{both.x,both.y,one,both.h},right{both.x+both.w-one,both.y,one,both.h};
        SDL_RenderCopy(renderer,texture.get(),nullptr,&left);
        SDL_RenderCopy(renderer,second->texture.get(),nullptr,&right);
    } else {
        const SDL_Rect rect=ravenemudsk_ui::picture_rect(out_w,out_h,w,h,prefs.integer_scale);
        SDL_RenderCopy(renderer,texture.get(),nullptr,&rect);
    }
    info.hardcore=!second&&retro.hardcore();
    info.hardcore_preferred=!second&&retro.snapshot().hardcore_preferred;
    info.hardcore_waiting=waiting;
    info.retro_status=waiting?retro.snapshot().status:retro.snapshot().rich_presence;
    info.paused=paused;
    info.paused_unfocused=paused_unfocused;
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
        case Action::SaveState:save_slot();overlay->close_menu();break;
        case Action::LoadState:load_slot();overlay->close_menu();break;
        case Action::UndoLoad:undo_load();overlay->close_menu();break;
        case Action::CheatsChanged:cheats_changed();break;
        case Action::PreviousSlot:select_slot(-1);break;
        case Action::NextSlot:select_slot(1);break;
        case Action::ToggleHardcore:
            input->release_all();retro.set_hardcore(!retro.snapshot().hardcore_preferred);
            if(!ravenemudsk_ui::remember_hardcore(retro.snapshot().hardcore_preferred))
                toast(ravenemudsk_ui::Toast::Kind::Info,"Mode non mémorisé","Le choix reste valable pour cette session.");
            apply_cheats();
            overlay->close_menu();paused=paused_unfocused=false;break;
        case Action::Reset:reset_consoles();overlay->close_menu();break;
        case Action::SwapPlayers:
            swapped=!swapped;input->release_all();second->input->release_all();describe_players();
            toast(ravenemudsk_ui::Toast::Kind::Info,"Joueurs échangés",info.players_note);
            break;
        case Action::SwitchSound:heard=1-heard;info.heard_player=heard;resampler.reset();break;
        case Action::ToggleFullscreen:
            SDL_SetWindowFullscreen(window,info.fullscreen?0:SDL_WINDOW_FULLSCREEN_DESKTOP);break;
        case Action::Quit:running=false;break;
        case Action::UnplugLink:
            if(link)link->unplug();
            overlay->close_menu();break;
        case Action::ChooseLink:if(link)link->choose(overlay->chosen_link());break;
        case Action::ChooseLinkLater:if(link)link->choose(std::nullopt);break;
        case Action::Screenshot:take_screenshot();break;
    }
}
// The battery save and the library's preview of the last frame.
void GameSession::finish() {
    if(req.fullscreen) {
        // Left on screen, black, until the launcher's window is up.
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);SDL_RenderPresent(renderer);
    }
    save_battery(core.get(),battery_path);
    if(second)save_battery(second->core.get(),second->battery_path);
    if(!pixels.empty()) {
        SDL_Surface* bitmap=SDL_CreateRGBSurfaceWithFormatFrom(shown.data(),w,h,32,
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

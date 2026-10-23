// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_overlay.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace ravenemudsk_ui;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void navigation(SDL_Window* window,SDL_Renderer* renderer){
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    GameOverlay overlay(window,renderer,&client);OverlayInfo info;
    info.game_title="Keyboard/controller navigation";info.system="gb";info.has_state=true;
    info.hardcore=true;info.hardcore_preferred=true;
    const auto draw=[&]{
        SDL_PumpEvents();SDL_SetRenderDrawColor(renderer,11,12,18,255);SDL_RenderClear(renderer);
        const auto action=overlay.draw(info);SDL_RenderPresent(renderer);
        require(ImGui::GetCurrentContext()->DebugDrawIdConflicts==0,"Overlay has conflicting widget IDs");
        return action;
    };
    const auto key=[&](ImGuiKey key){
        ImGui::GetIO().AddKeyEvent(key,true);const auto action=draw();
        ImGui::GetIO().AddKeyEvent(key,false);const auto released=draw();
        return action==OverlayAction::None?released:action;
    };
    const auto focused=[&](const char* label){
        auto* menu=ImGui::FindWindowByName("##pause");
        require(menu!=nullptr,"Pause window missing");
        return ImGui::GetCurrentContext()->NavId==menu->GetID(label);
    };
    for(int width:{1024,640}){
        SDL_SetWindowSize(window,width,width==1024?768:520);
        overlay.open_menu();for(int i=0;i<4;++i)draw();
        require(focused("Reprendre la partie"),"Pause menu did not focus its first keyboard action");
        key(ImGuiKey_DownArrow);require(focused("Sauvegarder l'état"),"Debug state saving unreachable by keyboard in Hardcore");
        key(ImGuiKey_DownArrow);require(focused("Succès et classements"),"Hardcore navigation must skip state loading and reach achievements");
        key(ImGuiKey_Enter);draw();
        const auto* retro=ImGui::FindWindowByName("Succès et classements");
        require(retro&&retro->Active&&!retro->Hidden,"Keyboard could not open the in-game achievements/rankings window");
        require(retro->Size.x<=width&&retro->ScrollMax.x==0,"Achievement window overflows the minimum game width");
        overlay.close_menu();draw();overlay.open_menu();for(int i=0;i<4;++i)draw();
        key(ImGuiKey_DownArrow);key(ImGuiKey_DownArrow);key(ImGuiKey_DownArrow);
        require(focused("Passer en Softcore"),"Hardcore mode switch unreachable by keyboard");
        require(key(ImGuiKey_Enter)==OverlayAction::ToggleHardcore,"Mode switch did not return its player action");
        overlay.close_menu();draw();
    }
}
// The wireless link cable: the partner chooser over the game, then, once
// linked, no state loading and an entry to unplug.
void link_cable(SDL_Window* window,SDL_Renderer* renderer){
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    GameOverlay overlay(window,renderer,&client);OverlayInfo info;
    info.game_title="Link cable";info.system="gb";info.has_state=true;info.link_own_label="PC Linux · 4F2A";
    SDL_SetWindowSize(window,1024,768);
    const auto draw=[&]{
        SDL_PumpEvents();SDL_SetRenderDrawColor(renderer,11,12,18,255);SDL_RenderClear(renderer);
        const auto action=overlay.draw(info);SDL_RenderPresent(renderer);
        require(ImGui::GetCurrentContext()->DebugDrawIdConflicts==0,"Link overlay has conflicting widget IDs");
        return action;
    };
    const auto key=[&](ImGuiKey key){
        ImGui::GetIO().AddKeyEvent(key,true);const auto action=draw();
        ImGui::GetIO().AddKeyEvent(key,false);const auto released=draw();
        return action==OverlayAction::None?released:action;
    };
    info.link_choices={{0x20,"Pixel 7 · 0000"},{0x30,"Galaxy S23 · 0000"}};
    for(int i=0;i<3;++i)draw();
    const auto* chooser=ImGui::FindWindowByName("##link-chooser");
    require(chooser&&chooser->Active&&chooser->Size.y<=768.f,"The partner chooser is up and fits");
    require(key(ImGuiKey_Enter)==OverlayAction::ChooseLink&&overlay.chosen_link()==0x20,"Enter picks the first device");
    key(ImGuiKey_DownArrow);
    require(key(ImGuiKey_Enter)==OverlayAction::ChooseLink&&overlay.chosen_link()==0x30,"The second one by the arrows");
    require(key(ImGuiKey_Escape)==OverlayAction::ChooseLinkLater,"Escape means later");
    overlay.open_menu();for(int i=0;i<3;++i)draw();
    chooser=ImGui::FindWindowByName("##link-chooser");
    require(!chooser||!chooser->Active,"The menu goes first, the chooser waits");
    overlay.close_menu();draw();
    info.link_choices.clear();info.link_partner="Pixel 7 (Tetris)";info.link_partner_paused=true;
    for(int i=0;i<2;++i)draw();
    overlay.open_menu();for(int i=0;i<4;++i)draw();
    const auto focused=[&](const char* label){
        auto* menu=ImGui::FindWindowByName("##pause");
        require(menu!=nullptr,"Pause window missing");
        return ImGui::GetCurrentContext()->NavId==menu->GetID(label);
    };
    key(ImGuiKey_DownArrow);key(ImGuiKey_DownArrow);
    require(focused("Succès et classements"),"Linked: no state loading, it would part the two games");
    for(int i=0;i<4;++i)key(ImGuiKey_DownArrow);
    require(focused("Débrancher le câble"),"Unplug entry reachable by keyboard");
    require(key(ImGuiKey_Enter)==OverlayAction::UnplugLink,"Unplug returns its action");
    overlay.close_menu();draw();
}
// Bounds of what is drawn on top of the game, from the foreground vertices,
// `seconds` after an unlock (or only a toast) was raised: {left, top, right, bottom}.
ImVec4 unlock_card(SDL_Window* window,SDL_Renderer* renderer,bool tv,double seconds,bool toast_only=false){
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    GameOverlay overlay(window,renderer,&client,tv);OverlayInfo info;info.game_title="TV cards";info.system="gba";
    if(toast_only)overlay.notify({Toast::Kind::Success,"État sauvegardé","",-1.0,5.0});
    else overlay.retro_event({"achievement","Maître de la Ligue","","4242","",25});
    int sounds=0;
    const auto start=SDL_GetTicks64();
    do{SDL_PumpEvents();SDL_RenderClear(renderer);overlay.draw(info);SDL_RenderPresent(renderer);SDL_Delay(10);
       sounds+=overlay.take_unlock_sound();}
    while(SDL_GetTicks64()-start<static_cast<Uint64>(seconds*1000.));
    require(sounds==(toast_only?0:1),"The unlock chime must start once, when its popup appears, and only for an unlock");
    ImVec4 box{1e9f,1e9f,-1e9f,-1e9f};
    for(const auto& v:ImGui::GetForegroundDrawList()->VtxBuffer){
        box.x=std::min(box.x,v.pos.x);box.y=std::min(box.y,v.pos.y);box.z=std::max(box.z,v.pos.x);box.w=std::max(box.w,v.pos.y);
    }
    return box;
}
void tv_cards(SDL_Window* window,SDL_Renderer* renderer){
    const auto desk=retro_card_style({1920.f,1080.f},false),tv=retro_card_style({1920.f,1080.f},true);
    require(!desk.tv&&desk.height==60.f&&desk.top==10.f&&desk.max_width==520.f,"Desktop cards keep their RavenEmu size");
    require(tv.tv&&tv.top>=54.f&&tv.bottom>=54.f&&tv.height>=100.f&&tv.badge>=72.f&&tv.max_width<=1152.f&&tv.max_width>=600.f,
        "TV cards must be larger and stay inside a 5 % safe area");
    const auto small_tv=retro_card_style({640.f,480.f},true);
    require(small_tv.max_width<=640.f-2.f*small_tv.top,"TV cards must fit a small screen");
    // TV unlock banner: inside the 5 % safe area, readable from a couch, fitting the screen.
    const auto banner=unlock_banner_style({1920.f,1080.f});
    require(banner.margin>=54.f&&banner.size>=110.f&&banner.max_width<=1920.f-2.f*banner.margin,"TV banner at 1080p");
    const auto low=unlock_banner_style({640.f,480.f});
    require(low.size>=72.f&&low.max_width<=640.f-2.f*low.margin&&low.max_width>=3.f*low.size,"TV banner on a small screen");
    // Timeline: the badge drops, the band unfolds, holds, folds back, the badge rises away.
    const auto at=[](double t){return unlock_banner_frame(t);};
    require(at(0.).drop==0.f&&at(.15).drop>0.f&&at(.15).unfold==0.f,"The badge drops in first, alone");
    require(at(.30).drop==1.f&&at(.5).unfold>0.f&&at(.5).unfold<1.f&&at(.65).unfold==1.f,"Then the band unfolds");
    require(at(2.).drop==1.f&&at(2.).unfold==1.f&&at(4.4).unfold==1.f,"The banner holds");
    require(at(4.6).unfold<1.f&&at(4.6).drop==1.f&&at(4.75).unfold==0.f&&at(4.9).drop<1.f,"Then folds back and rises");
    require(at(5.05).drop==0.f&&at(-1.).drop==0.f,"And is gone");
    for(double t=0.;t<5.05;t+=.01) require(at(t).drop>=0.f&&at(t).drop<=1.f&&at(t).unfold>=0.f&&at(t).unfold<=1.f,"Frame out of range");
    SDL_SetWindowSize(window,1280,720);
    const auto at720=unlock_banner_style({1280.f,720.f});
    // Desktop: the same banner, compact, at the top center of the game window.
    const auto desk_banner=unlock_banner_style({1280.f,720.f},false);
    require(desk_banner.size<at720.size&&desk_banner.margin<at720.margin&&desk_banner.max_width<=1280.f-2.f*desk_banner.margin,
        "Desktop banner is compact and fits the window");
    require(unlock_banner_style({320.f,240.f},false).max_width<=300.f,"Desktop banner fits a small window");
    const auto desk_card=unlock_card(window,renderer,false,1.);
    require(desk_card.y>=desk_banner.margin-2.f&&desk_card.w-desk_card.y>=desk_banner.size&&
            desk_card.w-desk_card.y<=desk_banner.size+8.f,"Desktop banner height is its badge square");
    require(std::abs((desk_card.x+desk_card.z)*.5f-642.f)<3.f&&desk_card.z-desk_card.x>2.f*desk_banner.size,
        "Desktop banner unfolds at the top center");
    const auto desk_drop=unlock_card(window,renderer,false,.1);
    require(desk_drop.z-desk_drop.x<=desk_banner.size+6.f&&desk_drop.y<desk_banner.margin,"Desktop badge drops in alone");
    const auto unfolded=unlock_card(window,renderer,true,1.);
    // The band's drop shadow reaches 4 px right and 6 px down; anti-aliasing 1.5 px.
    require(unfolded.x>=at720.margin-2.f&&unfolded.y>=at720.margin-2.f,"TV banner must sit inside the safe area");
    require(std::abs((unfolded.x+unfolded.z)*.5f-642.f)<3.f,"TV banner must be centered at the top");
    require(unfolded.w-unfolded.y>=at720.size&&unfolded.w-unfolded.y<=at720.size+8.f,"TV banner height is its badge square");
    require(unfolded.z-unfolded.x>2.f*at720.size,"TV banner must unfold its text beside the badge");
    const auto dropping=unlock_card(window,renderer,true,.1);
    require(dropping.z-dropping.x<=at720.size+6.f&&dropping.y<at720.margin,"TV badge drops in alone, from above");
    require(std::abs((dropping.x+dropping.z)*.5f-642.f)<3.f,"TV badge drops in at the center");
    {   // A leaderboard or a service notice shows its banner silently.
        ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
        GameOverlay overlay(window,renderer,&client,true);OverlayInfo info;info.game_title="Silent";info.system="gba";
        overlay.retro_event({"leaderboard_started","Course","","9"});
        bool sound=false;
        for(int frame=0;frame<20;++frame){SDL_RenderClear(renderer);overlay.draw(info);SDL_RenderPresent(renderer);sound|=overlay.take_unlock_sound();}
        require(!sound,"Only unlocks and completed games chime");
    }
    // Local notifications in game sit at the top center too, in both modes.
    for(const bool on_tv:{false,true}){
        const auto toast=unlock_card(window,renderer,on_tv,.6,true);
        require(std::abs((toast.x+toast.z)*.5f-640.f)<3.f&&toast.y<60.f,"In-game toasts must be at the top center");
    }
}
void pad_buttons(SDL_Window* window,SDL_Renderer* renderer){
    require(pad_family(SDL_CONTROLLER_TYPE_XBOXONE)==PadFamily::Xbox&&pad_family(SDL_CONTROLLER_TYPE_XBOX360)==PadFamily::Xbox&&
        pad_family(SDL_CONTROLLER_TYPE_PS4)==PadFamily::PlayStation&&pad_family(SDL_CONTROLLER_TYPE_PS5)==PadFamily::PlayStation5&&
        pad_family(SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO)==PadFamily::Nintendo&&pad_family(SDL_CONTROLLER_TYPE_UNKNOWN)==PadFamily::Generic,
        "Controller families");
    require(std::string(pad_button_name(PadFamily::Xbox,PadButton::South))=="A"&&
        std::string(pad_button_name(PadFamily::Xbox,PadButton::Start))=="Menu"&&
        std::string(pad_button_name(PadFamily::Xbox,PadButton::Back))=="Affichage"&&
        std::string(pad_button_name(PadFamily::PlayStation,PadButton::South))=="Croix"&&
        std::string(pad_button_name(PadFamily::PlayStation5,PadButton::Back))=="Create"&&
        std::string(pad_button_name(PadFamily::PlayStation,PadButton::LeftShoulder))=="L1"&&
        std::string(pad_button_name(PadFamily::Nintendo,PadButton::Start))=="+"&&
        std::string(pad_button_name(PadFamily::Generic,PadButton::RightShoulder))=="RB","Buttons are named as printed");
    // The start hint draws the controller's menu combination with any family.
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    for(const auto family:{PadFamily::Xbox,PadFamily::PlayStation,PadFamily::PlayStation5,PadFamily::Nintendo,PadFamily::Generic}){
        GameOverlay overlay(window,renderer,&client,family==PadFamily::PlayStation);OverlayInfo info;
        info.game_title="Pad hints";info.system="gb";info.pad=family;
        for(int frame=0;frame<3;++frame){SDL_PumpEvents();SDL_RenderClear(renderer);overlay.draw(info);SDL_RenderPresent(renderer);}
        require(ImGui::GetBackgroundDrawList()->VtxBuffer.Size>0,"The controller hint was not drawn");
        const float combo=pad_menu_combo(nullptr,{},family,ImGui::GetIO().Fonts->Fonts[0],30.f);
        require(combo>=60.f,"The menu combination needs both buttons");
        // A menu opened with the controller lists no keyboard shortcut.
        overlay.open_menu(true);for(int frame=0;frame<3;++frame){SDL_PumpEvents();overlay.draw(info);}
        require(ImGui::FindWindowByName("##pause")!=nullptr,"Pause menu missing");
        overlay.close_menu();
    }
}
// A virtual controller: ImGui reads its buttons as it reads a plugged one.
struct VirtualPad{
    int index=-1;SDL_Joystick* joystick=nullptr;
    VirtualPad(){
        require(SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER)==0,"SDL game controllers unavailable");
        SDL_VirtualJoystickDesc desc{};desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;desc.naxes=SDL_CONTROLLER_AXIS_MAX;
        desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;desc.name="Virtual pad";
        index=SDL_JoystickAttachVirtualEx(&desc);require(index>=0,"Virtual controller not attached");
        joystick=SDL_JoystickOpen(index);require(joystick!=nullptr&&SDL_IsGameController(index),"Virtual controller not usable");
    }
    ~VirtualPad(){SDL_JoystickClose(joystick);SDL_JoystickDetachVirtual(index);SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);}
    void set(SDL_GameControllerButton button,bool down){SDL_JoystickSetVirtualButton(joystick,button,down?1:0);}
};
// TV mode, played with the controller: large menu without the full-screen
// switch, achievements in their own TV panel, B goes back one level at a time.
void tv_menu(SDL_Window* window,SDL_Renderer* renderer){
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    SDL_SetWindowSize(window,1280,720);
    VirtualPad pad;
    GameOverlay overlay(window,renderer,&client,true);OverlayInfo info;
    info.game_title="TV menu";info.system="gba";info.has_state=true;info.fullscreen=true;info.pad=PadFamily::Xbox;
    info.has_achievement=true;info.achievement_title="Maître de la Ligue";info.achievement_points=25;
    const auto draw=[&]{
        SDL_PumpEvents();SDL_RenderClear(renderer);const auto action=overlay.draw(info);SDL_RenderPresent(renderer);
        require(ImGui::GetCurrentContext()->DebugDrawIdConflicts==0,"TV overlay has conflicting widget IDs");
        return action;
    };
    const auto press=[&](SDL_GameControllerButton button){
        pad.set(button,true);const auto action=draw();
        pad.set(button,false);const auto released=draw();
        return action==OverlayAction::None?released:action;
    };
    const auto focused=[&](const char* label){
        auto* menu=ImGui::FindWindowByName("##pause");
        require(menu!=nullptr,"TV pause window missing");
        return ImGui::GetCurrentContext()->NavId==menu->GetID(label);
    };
    overlay.open_menu(true);for(int i=0;i<4;++i)draw();
    require((ImGui::GetIO().BackendFlags&ImGuiBackendFlags_HasGamepad)!=0,"The overlay did not see the controller");
    const auto* menu=ImGui::FindWindowByName("##pause");
    require(menu&&menu->Size.x>=700.f&&menu->Size.y<=720.f&&menu->ScrollMax.y==0.f,"TV pause menu must be large and fit a 720p screen");
    require(focused("Reprendre la partie"),"TV pause menu did not focus its first action");
    for(int i=0;i<6;++i)press(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    require(focused("Quitter vers la bibliothèque"),"TV mode stays in full screen: no full-screen entry before Quit");
    for(int i=0;i<3;++i)press(SDL_CONTROLLER_BUTTON_DPAD_UP);
    require(focused("Succès et classements"),"Achievements entry unreachable with the D-pad");
    press(SDL_CONTROLLER_BUTTON_A);draw();
    require(overlay.achievements_open(),"A did not open the achievements from the TV menu");
    auto* retro=ImGui::FindWindowByName("##retro-tv");
    require(retro&&retro->Active&&retro->Size.x<=1280.f&&retro->Size.y<=720.f,"TV achievements panel missing or off screen");
    const auto* desk=ImGui::FindWindowByName("Succès et classements");
    require(!desk||!desk->Active,"TV mode opened the desktop achievements window");
    auto* bar=ImGui::GetCurrentContext()->TabBars.GetByKey(retro->GetID("##retro-tabs-tv"));
    require(bar&&bar->Tabs.Size==3&&bar->SelectedTabId==bar->Tabs[0].ID,"TV achievements open on their first tab");
    press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);draw();
    require(bar->SelectedTabId==bar->Tabs[1].ID,"RB did not show the next tab");
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);draw();
    require(bar->SelectedTabId==bar->Tabs[2].ID,"LB did not wrap to the last tab");
    require(press(SDL_CONTROLLER_BUTTON_B)==OverlayAction::None,"B in the achievements must not leave the menu");
    draw();
    require(!overlay.achievements_open()&&overlay.menu_open(),"B did not go back from the achievements to the menu");
    require(focused("Succès et classements"),"Back from the achievements, their entry keeps the focus");
    require(press(SDL_CONTROLLER_BUTTON_B)==OverlayAction::Resume,"B in the TV menu must resume the game");
    overlay.close_menu();draw();
}
int main(){
    require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL initialization failed");
    SDL_Window* window=SDL_CreateWindow("Overlay regression",0,0,1024,768,0);
    require(window!=nullptr,"SDL window failed");SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    require(renderer!=nullptr,"SDL renderer failed");
    navigation(window,renderer);link_cable(window,renderer);tv_cards(window,renderer);pad_buttons(window,renderer);tv_menu(window,renderer);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Keyboard overlay navigation, Hardcore load exclusion, achievement access and small-window layout, link cable, TV cards, controller buttons, TV menu passed\n";
}

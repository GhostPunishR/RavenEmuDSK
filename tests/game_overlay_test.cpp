// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/game_overlay.hpp>
#include <imgui_internal.h>
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
// Height of the unlock card drawn on top of the game, from the foreground vertices.
std::pair<float,float> unlock_card(SDL_Window* window,SDL_Renderer* renderer,bool tv){
    ravenemudsk_ra::Client client(nullptr,[](const auto&,const auto&,const auto&){return ravenemudsk_ra::ServerReply{"",-2};});
    GameOverlay overlay(window,renderer,&client,tv);OverlayInfo info;info.game_title="TV cards";info.system="gba";
    overlay.retro_event({"achievement","Maître de la Ligue","","4242","",25});
    float top=1e9f,bottom=-1e9f;
    for(int frame=0;frame<30;++frame){
        SDL_PumpEvents();SDL_RenderClear(renderer);overlay.draw(info);SDL_RenderPresent(renderer);
        SDL_Delay(20);
    }
    for(const auto& v:ImGui::GetForegroundDrawList()->VtxBuffer){top=std::min(top,v.pos.y);bottom=std::max(bottom,v.pos.y);}
    return {top,bottom-top};
}
void tv_cards(SDL_Window* window,SDL_Renderer* renderer){
    const auto desk=retro_card_style({1920.f,1080.f},false),tv=retro_card_style({1920.f,1080.f},true);
    require(!desk.tv&&desk.height==60.f&&desk.top==10.f&&desk.max_width==520.f,"Desktop cards keep their RavenEmu size");
    require(tv.tv&&tv.top>=54.f&&tv.bottom>=54.f&&tv.height>=100.f&&tv.badge>=72.f&&tv.max_width<=1152.f&&tv.max_width>=600.f,
        "TV cards must be larger and stay inside a 5 % safe area");
    const auto small=retro_card_style({640.f,480.f},true);
    require(small.max_width<=640.f-2.f*small.top,"TV cards must fit a small screen");
    SDL_SetWindowSize(window,1280,720);
    const auto [desk_top,desk_height]=unlock_card(window,renderer,false);
    const auto [tv_top,tv_height]=unlock_card(window,renderer,true);
    require(desk_height>=55.f&&desk_height<70.f,"Desktop unlock card was not drawn at its usual size");
    // The card starts at 5 % of 720 px; its 2 px outline and anti-aliasing reach 1.5 px further.
    require(tv_height>=100.f&&tv_top>=36.f-2.f&&tv_top>desk_top,"TV unlock card must be larger and inside the safe area");
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
    navigation(window,renderer);tv_cards(window,renderer);pad_buttons(window,renderer);tv_menu(window,renderer);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Keyboard overlay navigation, Hardcore load exclusion, achievement access and small-window layout, TV cards, controller buttons, TV menu passed\n";
}

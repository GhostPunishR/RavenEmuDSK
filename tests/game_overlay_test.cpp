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
int main(){
    require(SDL_Init(SDL_INIT_VIDEO)==0,"SDL initialization failed");
    SDL_Window* window=SDL_CreateWindow("Overlay regression",0,0,1024,768,0);
    require(window!=nullptr,"SDL window failed");SDL_Renderer* renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    require(renderer!=nullptr,"SDL renderer failed");
    navigation(window,renderer);tv_cards(window,renderer);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Keyboard overlay navigation, Hardcore load exclusion, achievement access and small-window layout, TV cards passed\n";
}

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/keyboard_bindings.hpp>
#include <ravenemudsk/ui/settings.hpp>
#include <ravenemu/bridge.h>
namespace ravenemudsk_ui {
const std::array<KeyCommand,static_cast<int>(KeyAction::Count)>& key_commands(){
    static const std::array<KeyCommand,16> commands{{
        {"key_up","Haut","Croix haut",SDLK_UP,RAVENEMU_UP},
        {"key_down","Bas","Croix bas",SDLK_DOWN,RAVENEMU_DOWN},
        {"key_left","Gauche","Croix gauche",SDLK_LEFT,RAVENEMU_LEFT},
        {"key_right","Droite","Croix droite",SDLK_RIGHT,RAVENEMU_RIGHT},
        {"key_a","Bouton A","A",SDLK_z,RAVENEMU_A},
        {"key_b","Bouton B","B",SDLK_x,RAVENEMU_B},
        {"key_start","Start","Start",SDLK_RETURN,RAVENEMU_START},
        {"key_select","Select","Back",SDLK_BACKSPACE,RAVENEMU_SELECT},
        {"key_l","Gâchette L (GBA)","LB",SDLK_a,RAVENEMU_L},
        {"key_r","Gâchette R (GBA)","RB",SDLK_s,RAVENEMU_R},
        {"key_menu","Menu de pause","Select + Start",SDLK_ESCAPE,-1},
        {"key_pause","Pause rapide","Menu de pause",SDLK_p,-1},
        {"key_save","Sauvegarder l’état","Menu de pause",SDLK_F5,-1},
        {"key_load","Charger l’état","Menu de pause",SDLK_F9,-1},
        {"key_reset","Réinitialiser","Menu de pause",SDLK_F4,-1},
        {"key_fullscreen","Plein écran","Menu de pause",SDLK_F11,-1}
    }};
    return commands;
}
KeyboardBindings::KeyboardBindings(){
    for(std::size_t i=0;i<keys_.size();++i)keys_[i]=key_commands()[i].default_key;
}
SDL_Keycode KeyboardBindings::key(KeyAction a)const{
    const auto i=static_cast<std::size_t>(a);return i<keys_.size()?keys_[i]:SDLK_UNKNOWN;
}
std::string KeyboardBindings::label(KeyAction a)const{
    switch(key(a)){
    case SDLK_UNKNOWN:return "Non attribuée";
    case SDLK_ESCAPE:return "Échap";
    case SDLK_RETURN:return "Entrée";
    case SDLK_BACKSPACE:return "Retour arrière";
    case SDLK_SPACE:return "Espace";
    case SDLK_UP:return "Flèche haut";
    case SDLK_DOWN:return "Flèche bas";
    case SDLK_LEFT:return "Flèche gauche";
    case SDLK_RIGHT:return "Flèche droite";
    default:return SDL_GetKeyName(key(a));
    }
}
KeyAction KeyboardBindings::action(SDL_Keycode value)const{
    if(value==SDLK_ESCAPE)return KeyAction::Menu;
    if(value!=SDLK_UNKNOWN)for(std::size_t i=0;i<keys_.size();++i)
        if(keys_[i]==value)return static_cast<KeyAction>(i);
    return KeyAction::Count;
}
int KeyboardBindings::button(SDL_Keycode value)const{
    const auto a=action(value);return a==KeyAction::Count?-1:key_commands()[static_cast<int>(a)].button;
}
bool KeyboardBindings::assign(KeyAction a,SDL_Keycode value){
    const auto i=static_cast<std::size_t>(a);
    if(i>=keys_.size()||value<0||(value==SDLK_ESCAPE&&a!=KeyAction::Menu))return false;
    if(value!=SDLK_UNKNOWN){
        if(value&SDLK_SCANCODE_MASK){
            const auto scan=value&~SDLK_SCANCODE_MASK;if(scan<=SDL_SCANCODE_UNKNOWN||scan>=SDL_NUM_SCANCODES)return false;
        }else if(value>0x10ffff||(value>=0xd800&&value<=0xdfff)||
                 (value<32&&value!=SDLK_BACKSPACE&&value!=SDLK_TAB&&value!=SDLK_RETURN&&value!=SDLK_ESCAPE)||
                 !*SDL_GetKeyName(value))return false;
        for(std::size_t j=0;j<keys_.size();++j)if(j!=i&&keys_[j]==value){
            // Do not move the reserved Escape key onto a gameplay command.
            if(keys_[i]==SDLK_ESCAPE)return false;
            keys_[j]=keys_[i];break;
        }
    }
    keys_[i]=value;return true;
}
void KeyboardBindings::load(const Settings& settings){
    *this=KeyboardBindings{};
    for(std::size_t i=0;i<keys_.size();++i)if(!settings.get(key_commands()[i].setting).empty())
        assign(static_cast<KeyAction>(i),settings.number(key_commands()[i].setting,keys_[i]));
}
void KeyboardBindings::store(Settings& settings)const{
    for(std::size_t i=0;i<keys_.size();++i)settings.set(key_commands()[i].setting,std::to_string(keys_[i]));
}
}

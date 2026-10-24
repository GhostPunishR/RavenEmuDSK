// SPDX-License-Identifier: AGPL-3.0-only
// Settings: library folders, display, sound, controls, about.
#include "shell.hpp"

namespace ravenemudsk_ui::shell {
namespace {
void folder_row(State& s, const char* label, const char* detail, const fs::path& folder) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, {p.x + w, p.y + 64.f}, color::elevated, radius::md);
    draw_icon(d, Icon::Folder, {p.x + 28.f, p.y + 32.f}, 20.f, color::accent_hover);
    text_at(d, s.fonts.strong, {p.x + 56.f, p.y + 12.f}, color::text, label);
    const std::string path = utf8(folder);
    text_at(d, s.fonts.caption, {p.x + 56.f, p.y + 36.f}, color::text_faint,
            ellipsize(s.fonts.caption, path + "  \xC2\xB7  " + detail, w - 200.f));
    ImGui::SetCursorScreenPos({p.x + w - 116.f, p.y + 12.f});
    ImGui::PushID(label);
    if (button("Ouvrir", Icon::Folder, ButtonStyle::Secondary, {104.f, 40.f})) open_folder(folder);
    ImGui::PopID();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", path.c_str());
    ImGui::SetCursorScreenPos({p.x, p.y + 64.f + space::sm});
    ImGui::Dummy({0.f, 0.f});
}
void settings_library(State& s, Library& lib) {
    begin_card("##library");
    card_title(s, Icon::Library, "Bibliothèque");
    muted("RavenEmu mémorise seulement l'emplacement de vos jeux : les ROMs restent dans leur dossier, "
          "sur ce PC.");
    ImGui::Dummy({0.f, space::xs});
    ImGui::PushFont(s.fonts.caption);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color::text_faint));
    ImGui::TextWrapped("Fichier de bibliothèque : %s", utf8(data_file()).c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    if (button("Ajouter un jeu", Icon::Plus, ButtonStyle::Primary)) add_game(s, lib);
    ImGui::SameLine(0.f, space::sm);
    if (button("Importer un dossier", Icon::Folder)) pick_and_import_folder(s, lib);
    if (ImGui::GetContentRegionAvail().x >= 720.f) ImGui::SameLine(0.f, space::sm);
    if (button("Saisir un chemin", Icon::Keyboard, ButtonStyle::Ghost)) s.open_manual = true;
    end_card();

    begin_card("##folders");
    card_title(s, Icon::Folder, "Sauvegardes et jaquettes");
    muted("RavenEmu range automatiquement vos fichiers dans deux dossiers à côté du programme. "
          "Copiez-les pour sauvegarder ou transférer votre progression.");
    ImGui::Dummy({0.f, space::xs});
    folder_row(s, "Sauvegardes", "sauvegardes de jeu, états et dernières captures",
               s.folders.saves);
    folder_row(s, "Jaquettes", "nommez l'image comme la ROM, par exemple MonJeu.png", s.folders.covers);
    end_card();
}

// Controller column of the controls table, named as on the connected controller.
// The console buttons show their bound input; the menu, the chord of the
// bound Select and Start; the other shortcuts are in the pause menu.
std::string controller_label(KeyAction action, const char* fallback, const PadBindings& pads, PadFamily family) {
    if (static_cast<int>(action) < PadBindings::count) return pad_input_name(pads.input(action), family);
    if (action == KeyAction::Menu)
        return pad_input_name(pads.input(KeyAction::Select), family) + " + " + pad_input_name(pads.input(KeyAction::Start), family);
    return fallback;
}
// A switch with its label and an explanation below, saved at once.
void setting_switch(State& s, const char* key, bool fallback, const char* label, const char* help) {
    bool value = s.settings.flag(key, fallback);
    ImGui::PushID(key);
    if (toggle("##switch", &value)) {
        s.settings.set_flag(key, value);
        save_settings(s);
    }
    ImGui::PopID();
    ImGui::SameLine(0.f, space::md);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::PushFont(s.fonts.caption);
    muted(help);
    ImGui::PopFont();
}
// A 0-100 % slider on the card, saved once let go, not at every step.
void percent_slider(State& s, const char* key, int fallback, bool disabled = false) {
    int value = std::clamp(s.settings.number(key, fallback), 0, 100);
    ImGui::PushID(key);
    ImGui::SetNextItemWidth(std::min(420.f, ImGui::GetContentRegionAvail().x));
    if (disabled) ImGui::BeginDisabled();
    // The card is the theme's frame color: the track needs its own.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(color::elevated));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImGui::ColorConvertU32ToFloat4(color::border_strong));
    if (ImGui::SliderInt("##percent", &value, 0, 100, "%d %%", ImGuiSliderFlags_AlwaysClamp))
        s.settings.set(key, std::to_string(value));
    if (ImGui::IsItemDeactivatedAfterEdit()) save_settings(s);
    ImGui::PopStyleColor(3);
    if (disabled) ImGui::EndDisabled();
    ImGui::PopID();
}
void settings_display(State& s) {
    begin_card("##image");
    card_title(s, Icon::Fullscreen, "Image du jeu");
    setting_switch(s, "smooth_image", false, "Lisser l’image",
                   "Adoucit les pixels quand l’image est agrandie. Désactivé, les pixels restent nets, comme sur la "
                   "console. Pris en compte au lancement du jeu.");
    end_card();
    begin_card("##window");
    card_title(s, Icon::Pause, "Fenêtre du jeu");
    setting_switch(s, "pause_unfocused", true, "Pause en arrière-plan",
                   "Met le jeu en pause quand vous passez à une autre fenêtre, et le reprend à votre retour. "
                   "En Hardcore, RetroAchievements peut refuser une pause trop proche de la précédente. "
                   "Pris en compte au lancement du jeu.");
    end_card();
}
void settings_sound(State& s) {
    begin_card("##sound");
    card_title(s, Icon::Volume, "Son du jeu");
    ImGui::TextUnformatted("Volume");
    percent_slider(s, "volume", 100, s.settings.flag("mute", false));
    ImGui::Dummy({0.f, space::sm});
    setting_switch(s, "mute", false, "Couper le son",
                   "Plus aucun son du jeu, ni le carillon des succès. Pris en compte au lancement du jeu.");
    end_card();
}
void settings_link(State& s) {
    begin_card("##link");
    card_title(s, Icon::Link, "Câble link sans fil");
    setting_switch(s, "wireless_link", true, "Câble link sans fil",
                   "Quand le jeu se sert de son port link (échanges, combats), RavenEmu le relie tout seul au jeu "
                   "d'un autre appareil du même réseau Wi-Fi ou local : un téléphone avec RavenEmu Android ou un autre "
                   "ordinateur. Pris en compte au lancement du jeu.");
    ImGui::Dummy({0.f, space::xs});
    muted("Pendant la recherche, cet ordinateur s'annonce sur le réseau local seulement, sous le nom de son système "
          "(PC Windows, Mac ou PC Linux). Rien ne passe par Internet. Au premier lancement, le pare-feu peut demander "
          "l'autorisation : acceptez pour les réseaux privés.");
    end_card();
}
} // namespace

void settings_controls(State& s) {
    KeyboardBindings keys;keys.load(s.settings);
    PadBindings pads;pads.load(s.settings);
    const auto family=connected_pad().value_or(PadFamily::Generic);
    begin_card("##controls");
    card_title(s, Icon::Gamepad, "Commandes en jeu");
    muted("Cliquez sur une touche ou un bouton pour le modifier. Les changements sont enregistrés automatiquement.");
    muted("Les touches et boutons déjà utilisés sont échangés. Échap reste toujours disponible pour ouvrir le menu ; "
          "à la manette, Select et Start pressés ensemble l'ouvrent, tels qu'attribués ici.");
    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, ImGui::ColorConvertU32ToFloat4(with_alpha(color::elevated, .45f)));
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f);
        ImGui::TableSetupColumn("Clavier", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("Manette", ImGuiTableColumnFlags_WidthStretch, 1.f);
        ImGui::PushFont(s.fonts.caption);
        ImGui::TableNextRow();
        for (const char* label : {"ACTION", "CLAVIER", "MANETTE"}) {
            ImGui::TableNextColumn();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color::text_faint), "%s", label);
        }
        ImGui::PopFont();
        for (std::size_t i=0;i<key_commands().size();++i) {
            const auto& command=key_commands()[i];ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow(0,42.f);
            ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();ImGui::TextWrapped("%s",command.label);
            ImGui::TableNextColumn();
            const auto key_name=keys.label(static_cast<KeyAction>(i));
            const float key_width=ImGui::GetContentRegionAvail().x;
            const auto key_caption=ellipsize(s.fonts.body,key_name,std::max(16.f,key_width-64.f));
            if(button(key_caption.c_str(),Icon::Keyboard,ButtonStyle::Secondary,{key_width,34.f})){
                s.editing_key=static_cast<int>(i);s.key_error.clear();
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",key_name.c_str());
            ImGui::TableNextColumn();
            const auto pad_name=controller_label(static_cast<KeyAction>(i),command.controller,pads,family);
            if(static_cast<int>(i)<PadBindings::count){
                const float pad_width=ImGui::GetContentRegionAvail().x;
                const auto pad_caption=ellipsize(s.fonts.body,pad_name,std::max(16.f,pad_width-64.f));
                if(button(pad_caption.c_str(),Icon::Gamepad,ButtonStyle::Secondary,{pad_width,34.f})){
                    s.editing_pad=static_cast<int>(i);s.pad_error.clear();
                }
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",pad_name.c_str());
            }else{
                ImGui::AlignTextToFramePadding();ImGui::TextDisabled("%s",pad_name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
    if(button("Rétablir les commandes par défaut",Icon::Reset,ButtonStyle::Secondary)){
        auto settings=s.settings;KeyboardBindings{}.store(settings);PadBindings{}.store(settings);
        try{settings.save();s.settings=std::move(settings);notify(s,Toast::Kind::Success,"Commandes réinitialisées");}
        catch(const std::exception& e){notify(s,Toast::Kind::Error,"Commandes non enregistrées",e.what());}
    }
    end_card();
    begin_card("##sensors");
    card_title(s, Icon::Gamepad, "Vibrations et capteurs");
    setting_switch(s, "rumble", true, "Vibrations",
                   "La manette vibre avec le moteur des cartouches qui en ont un : Pokémon Pinball, Drill Dozer, "
                   "WarioWare: Twisted!... Pris en compte au lancement du jeu.");
    ImGui::Dummy({0.f, space::sm});
    setting_switch(s, "motion_sensors", true, "Capteurs de mouvement de la manette",
                   "Kirby Tilt 'n' Tumble, Yoshi Topsy-Turvy et WarioWare: Twisted! lisent l'accéléromètre et le "
                   "gyroscope de la manette, s'il en a (DualShock 4, DualSense, Switch Pro, Joy-Con). Au lancement du "
                   "jeu et à chaque reprise, la position de la manette compte comme à plat. Le stick gauche incline "
                   "et tourne aussi, avec ou sans capteurs. Pris en compte au lancement du jeu.");
    ImGui::Dummy({0.f, space::sm});
    ImGui::TextUnformatted("Soleil sur le capteur solaire (Boktai)");
    percent_slider(s, "solar_light", 50);
    ImGui::PushFont(s.fonts.caption);
    muted("Part du plein soleil que reçoit le capteur de la cartouche. Pris en compte au lancement du jeu.");
    ImGui::PopFont();
    end_card();
}

void key_capture_dialog(State& s){
    // One stable popup ID, independent of the edited row/category scroll.
    if(s.editing_key>=0&&!ImGui::IsPopupOpen("##key-capture"))ImGui::OpenPopup("##key-capture");
    ImGui::SetNextWindowSize({std::min(500.f,ImGui::GetIO().DisplaySize.x-48.f),0.f});
    if(ImGui::BeginPopupModal("##key-capture",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize)){
        if(s.editing_key<0)ImGui::CloseCurrentPopup();
        else{
            ImGui::TextWrapped("Nouvelle touche : %s",key_commands()[s.editing_key].label);
            ImGui::TextWrapped("Appuyez sur une touche du clavier, sans combinaison. Échap annule.");
            if(!s.key_error.empty())ImGui::TextWrapped("%s",s.key_error.c_str());
            if(button("Non attribuée",Icon::Close,ButtonStyle::Secondary)){
                auto settings=s.settings;KeyboardBindings keys;keys.load(settings);
                keys.assign(static_cast<KeyAction>(s.editing_key),SDLK_UNKNOWN);keys.store(settings);
                try{settings.save();s.settings=std::move(settings);s.editing_key=-1;ImGui::CloseCurrentPopup();}
                catch(const std::exception& e){s.key_error=e.what();}
            }
            ImGui::SameLine();
            if(button("Annuler",Icon::Close,ButtonStyle::Ghost)){s.editing_key=-1;ImGui::CloseCurrentPopup();}
        }
        ImGui::EndPopup();
    }
}
namespace {
// Saves the controller input chosen for the edited console button.
void choose_pad_input(State& s,int input){
    auto settings=s.settings;PadBindings pads;pads.load(settings);
    const auto action=static_cast<KeyAction>(s.editing_pad);
    if(!pads.assign(action,input)){
        s.pad_error=input==SDL_CONTROLLER_BUTTON_GUIDE?"Le bouton central reste au système (Xbox Game Bar, Steam). Choisissez-en un autre.":
            "Select et Start ouvrent le menu ensemble : ils restent attribués.";
        return;
    }
    pads.store(settings);
    try{settings.save();s.settings=std::move(settings);s.editing_pad=-1;s.pad_release_pending=true;}
    catch(const std::exception& e){s.pad_error=e.what();}
}
} // namespace
void pad_capture_dialog(State& s){
    if(s.editing_pad>=0&&!ImGui::IsPopupOpen("##pad-capture"))ImGui::OpenPopup("##pad-capture");
    ImGui::SetNextWindowSize({std::min(520.f,ImGui::GetIO().DisplaySize.x-48.f),0.f});
    if(ImGui::BeginPopupModal("##pad-capture",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_AlwaysAutoResize)){
        if(s.editing_pad<0)ImGui::CloseCurrentPopup();
        else{
            ImGui::TextWrapped("Nouveau bouton : %s",key_commands()[s.editing_pad].label);
            ImGui::TextWrapped(connected_pad()?"Appuyez sur un bouton ou une gâchette de la manette. Échap annule.":
                "Branchez une manette, puis appuyez sur un bouton ou une gâchette. Échap annule.");
            if(!s.pad_error.empty())ImGui::TextWrapped("%s",s.pad_error.c_str());
            if(button("Non attribué",Icon::Close,ButtonStyle::Secondary))choose_pad_input(s,pad_unbound);
            ImGui::SameLine();
            if(button("Annuler",Icon::Close,ButtonStyle::Ghost)){s.editing_pad=-1;s.pad_release_pending=true;}
            if(s.editing_pad<0)ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
// While a controller button is being chosen, the controller's presses go to
// it, not to the interface.
bool capture_pad_event(State& s,const SDL_Event& event){
    if(s.editing_pad<0)return false;
    if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){
        s.editing_pad=-1;s.pad_release_pending=true;return false;
    }
    if(event.type==SDL_KEYDOWN&&!event.key.repeat&&event.key.keysym.sym==SDLK_ESCAPE){
        s.editing_pad=-1;s.pad_release_pending=true;return true;
    }
    if(event.type==SDL_CONTROLLERBUTTONDOWN){choose_pad_input(s,event.cbutton.button);return true;}
    if(event.type==SDL_CONTROLLERAXISMOTION&&event.caxis.value>=GameInput::trigger_press&&
       (event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT||event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERRIGHT)){
        choose_pad_input(s,event.caxis.axis==SDL_CONTROLLER_AXIS_TRIGGERLEFT?pad_left_trigger:pad_right_trigger);
        return true;
    }
    return event.type==SDL_CONTROLLERBUTTONUP||event.type==SDL_CONTROLLERAXISMOTION;
}
// Any button or trigger of an open controller still held.
bool pad_input_held(){
    for(int i=0;i<SDL_NumJoysticks();++i){
        if(!SDL_IsGameController(i))continue;
        SDL_GameController* pad=SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if(!pad)continue;
        for(int b=0;b<SDL_CONTROLLER_BUTTON_MAX;++b)
            if(SDL_GameControllerGetButton(pad,static_cast<SDL_GameControllerButton>(b)))return true;
        if(SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT)>GameInput::trigger_release||
           SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT)>GameInput::trigger_release)return true;
    }
    return false;
}
bool capture_keyboard_event(State& s,const SDL_Event& event){
    if(s.editing_key<0)return false;
    if(event.type==SDL_WINDOWEVENT&&event.window.event==SDL_WINDOWEVENT_FOCUS_LOST){s.editing_key=-1;return false;}
    if(event.type==SDL_KEYDOWN&&!event.key.repeat){
        if(event.key.keysym.sym==SDLK_ESCAPE)s.editing_key=-1;
        else{
            auto settings=s.settings;KeyboardBindings keys;keys.load(settings);
            if(!keys.assign(static_cast<KeyAction>(s.editing_key),event.key.keysym.sym))
                s.key_error="Touche indisponible. Choisissez une autre touche ou libérez son affectation.";
            else{
                keys.store(settings);
                try{settings.save();s.settings=std::move(settings);s.editing_key=-1;}
                catch(const std::exception& e){s.key_error=e.what();}
            }
        }
    }
    return event.type==SDL_KEYDOWN||event.type==SDL_TEXTINPUT||event.type==SDL_TEXTEDITING;
}
namespace {

void settings_about(State& s) {
    begin_card("##about");
    card_title(s, Icon::Info, "À propos");
    ImGui::TextWrapped("RavenEmu - émulateur Game Boy, Game Boy Color et Game Boy Advance");
    muted("Préversion " RAVENEMUDSK_VERSION ". Licence AGPL-3.0-only. Cœurs d'émulation RavenCore (AGPL-3.0-only), "
          "succès RetroAchievements via rcheevos (MIT), interface Dear ImGui (MIT) "
          "et SDL2 (zlib). Les couleurs des jeux Game Boy sur GBC viennent des tables de "
          "The Cutting Room Floor (tcrf.net, CC BY 3.0), converties au RGB555 de la console. Aucun compte en ligne n'est nécessaire pour l'émulation. Pas de télémétrie. "
          "Les fonctions RetroAchievements transmettent au service les données du compte, du jeu et des succès. "
          "La recherche de mises à jour contacte GitHub.");
    ImGui::Dummy({0.f, space::xs});
    if (button("Confidentialité", Icon::Info, ButtonStyle::Ghost)) {
        if (SDL_OpenURL("https://github.com/GhostPunishR/RavenEmuDSK/blob/main/docs/confidentialite.md") != 0)
            notify(s, Toast::Kind::Error, "Ouverture du navigateur impossible", SDL_GetError());
    }
    end_card();
}
} // namespace
void settings_view(State& s, Library& lib) {
    struct Category {
        SettingsCategory id;
        Icon icon;
        const char* label;
        const char* description;
    };
    const Category categories[] = {
        {SettingsCategory::Library, Icon::Library, "Bibliothèque",
         "Importez vos jeux et retrouvez vos sauvegardes et jaquettes."},
        {SettingsCategory::Display, Icon::Fullscreen, "Affichage",
         "Choisissez comment l’image du jeu est agrandie, et sa pause en arrière-plan."},
        {SettingsCategory::Sound, Icon::Volume, "Son",
         "Réglez le volume du jeu."},
        {SettingsCategory::Controls, Icon::Gamepad, "Commandes",
         "Choisissez les touches du clavier et les boutons de la manette, ses vibrations et ses capteurs."},
        {SettingsCategory::Link, Icon::Link, "Câble link",
         "Reliez vos jeux à ceux d’un autre appareil, sans fil."},
        {SettingsCategory::Updates, Icon::Reset, "Mises à jour",
         "Vérifiez les nouvelles versions et gérez la recherche au démarrage."},
        {SettingsCategory::About, Icon::Info, "À propos",
         "Informations sur l’application et ses composants."}};
    const float height = std::max(1.f, ImGui::GetContentRegionAvail().y - content_pad -
                                     ImGui::GetStyle().ItemSpacing.y);
    ImGui::BeginChild("##settings-navigation", {208.f, height});
    ImGui::PushFont(s.fonts.caption);
    ImGui::TextDisabled("Catégories");
    ImGui::PopFont();
    ImGui::Dummy({0.f, space::xs});
    for (const auto& category : categories) {
        if (button(category.label, category.icon,
                   s.settings_category == category.id ? ButtonStyle::Primary : ButtonStyle::Ghost,
                   {ImGui::GetContentRegionAvail().x, 48.f}))
            s.settings_category = category.id;
    }
    ImGui::EndChild();
    ImGui::SameLine(0.f, space::xl);
    // Each category keeps its own scroll position, independent of the menu.
    ImGui::PushID(static_cast<int>(s.settings_category));
    ImGui::BeginChild("##settings-category", {0.f, height});
    for (const auto& category : categories) {
        if (category.id != s.settings_category) continue;
        ImGui::PushFont(s.fonts.display);
        ImGui::TextUnformatted(category.label);
        ImGui::PopFont();
        muted(category.description);
    }
    ImGui::Dummy({0.f, space::sm});
    switch (s.settings_category) {
    case SettingsCategory::Library: settings_library(s, lib); break;
    case SettingsCategory::Display: settings_display(s); break;
    case SettingsCategory::Sound: settings_sound(s); break;
    case SettingsCategory::Controls: settings_controls(s); break;
    case SettingsCategory::Link: settings_link(s); break;
    case SettingsCategory::Updates: updates_card(s); break;
    case SettingsCategory::About: settings_about(s); break;
    }
    ImGui::EndChild();
    ImGui::PopID();
}
} // namespace ravenemudsk_ui::shell

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/pad_glyphs.hpp>
#include <ravenemudsk/ui/pad_bindings.hpp>
#include <ravenemudsk/ui/theme.hpp>
#include <algorithm>
#include <cfloat>
namespace ravenemudsk_ui {
namespace {
constexpr ImU32 face = IM_COL32(43, 45, 58, 255);
constexpr ImU32 edge = IM_COL32(88, 92, 112, 255);
constexpr ImU32 ink = IM_COL32(236, 238, 245, 255);
bool face_button(PadButton b) {
    return b == PadButton::South || b == PadButton::East || b == PadButton::West || b == PadButton::North;
}
// Xbox letter colors, by position.
ImU32 xbox_color(PadButton b) {
    switch (b) {
    case PadButton::South: return IM_COL32(108, 190, 69, 255);
    case PadButton::East: return IM_COL32(230, 72, 60, 255);
    case PadButton::West: return IM_COL32(64, 140, 230, 255);
    default: return IM_COL32(245, 196, 48, 255);
    }
}
// PlayStation symbol colors, by position.
ImU32 playstation_color(PadButton b) {
    switch (b) {
    case PadButton::South: return IM_COL32(124, 166, 222, 255);
    case PadButton::East: return IM_COL32(236, 104, 104, 255);
    case PadButton::West: return IM_COL32(214, 138, 196, 255);
    default: return IM_COL32(64, 196, 166, 255);
    }
}
void centered_text(ImDrawList* d, ImFont* font, ImVec2 center, ImU32 color, const char* text) {
    const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
    d->AddText(font, font->FontSize, {center.x - size.x * .5f, center.y - size.y * .5f}, color, text);
}
float pill_glyph(ImDrawList* d, ImVec2 p, ImFont* font, float h, const char* text, float alpha) {
    const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
    const float w = std::max(h * 1.4f, size.x + h * .7f);
    if (!d) return w;
    d->AddRectFilled(p, {p.x + w, p.y + h}, with_alpha(face, alpha), h * .5f);
    d->AddRect(p, {p.x + w, p.y + h}, with_alpha(edge, alpha), h * .5f, 0, 1.5f);
    centered_text(d, font, {p.x + w * .5f, p.y + h * .5f}, with_alpha(ink, alpha), text);
    return w;
}
} // namespace

PadFamily pad_family(SDL_GameControllerType type) {
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE: return PadFamily::Xbox;
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4: return PadFamily::PlayStation;
    case SDL_CONTROLLER_TYPE_PS5: return PadFamily::PlayStation5;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 24, 0)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return PadFamily::Nintendo;
    default: return PadFamily::Generic;
    }
}

std::optional<PadFamily> connected_pad() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) return pad_family(SDL_GameControllerTypeForIndex(i));
    return std::nullopt;
}

const char* pad_button_name(PadFamily family, PadButton button) {
    const bool sony = family == PadFamily::PlayStation || family == PadFamily::PlayStation5;
    switch (button) {
    case PadButton::South: return sony ? "Croix" : "A";
    case PadButton::East: return sony ? "Rond" : "B";
    case PadButton::West: return sony ? "Carré" : "X";
    case PadButton::North: return sony ? "Triangle" : "Y";
    case PadButton::LeftShoulder: return sony ? "L1" : family == PadFamily::Nintendo ? "L" : "LB";
    case PadButton::RightShoulder: return sony ? "R1" : family == PadFamily::Nintendo ? "R" : "RB";
    case PadButton::Start:
        return family == PadFamily::Xbox ? "Menu" : sony ? "Options" : family == PadFamily::Nintendo ? "+" : "Start";
    case PadButton::Back:
        return family == PadFamily::Xbox ? "Affichage" : family == PadFamily::PlayStation5 ? "Create" :
               family == PadFamily::PlayStation ? "Share" : family == PadFamily::Nintendo ? "-" : "Select";
    }
    return "";
}

float pad_glyph(ImDrawList* d, ImVec2 p, PadFamily family, PadButton button, ImFont* font, float h, float alpha) {
    const bool sony = family == PadFamily::PlayStation || family == PadFamily::PlayStation5;
    const ImVec2 c{p.x + h * .5f, p.y + h * .5f};
    const float r = h * .5f;
    const bool round = face_button(button) ||
        ((family == PadFamily::Xbox || family == PadFamily::Nintendo) &&
         (button == PadButton::Start || button == PadButton::Back));
    if (!d && round) return h;
    if (face_button(button)) {
        d->AddCircleFilled(c, r, with_alpha(face, alpha), 32);
        d->AddCircle(c, r - .5f, with_alpha(edge, alpha), 32, 1.5f);
        if (sony) {
            // The fonts have no PlayStation symbols: draw them.
            const ImU32 col = with_alpha(playstation_color(button), alpha);
            const float s = r * .45f, t = std::max(1.5f, h * .07f);
            if (button == PadButton::South) {
                d->AddLine({c.x - s, c.y - s}, {c.x + s, c.y + s}, col, t);
                d->AddLine({c.x - s, c.y + s}, {c.x + s, c.y - s}, col, t);
            } else if (button == PadButton::East) d->AddCircle(c, s * 1.1f, col, 24, t);
            else if (button == PadButton::West) d->AddRect({c.x - s, c.y - s}, {c.x + s, c.y + s}, col, 0.f, 0, t);
            else d->AddTriangle({c.x, c.y - s * 1.15f}, {c.x + s * 1.1f, c.y + s * .8f}, {c.x - s * 1.1f, c.y + s * .8f}, col, t);
        } else {
            const ImU32 col = with_alpha(family == PadFamily::Xbox ? xbox_color(button) : ink, alpha);
            centered_text(d, font, c, col, pad_button_name(family, button));
        }
        return h;
    }
    if (button == PadButton::LeftShoulder || button == PadButton::RightShoulder)
        return pill_glyph(d, p, font, h, pad_button_name(family, button), alpha);
    if (round) {
        // Small round buttons: Menu's three lines, View's two windows, or +/-.
        d->AddCircleFilled(c, r, with_alpha(face, alpha), 32);
        d->AddCircle(c, r - .5f, with_alpha(edge, alpha), 32, 1.5f);
        const ImU32 mark = with_alpha(ink, alpha);
        const float s = r * .42f, t = std::max(1.5f, h * .06f);
        if (family == PadFamily::Nintendo) {
            d->AddLine({c.x - s, c.y}, {c.x + s, c.y}, mark, t);
            if (button == PadButton::Start) d->AddLine({c.x, c.y - s}, {c.x, c.y + s}, mark, t);
        } else if (button == PadButton::Start) {
            for (float dy : {-s * .7f, 0.f, s * .7f}) d->AddLine({c.x - s, c.y + dy}, {c.x + s, c.y + dy}, mark, t);
        } else {
            d->AddRect({c.x - s, c.y - s * .8f}, {c.x + s * .35f, c.y + s * .35f}, mark, 1.f, 0, t);
            d->AddRect({c.x - s * .35f, c.y - s * .35f}, {c.x + s, c.y + s * .8f}, mark, 1.f, 0, t);
        }
        return h;
    }
    return pill_glyph(d, p, font, h, sony ? (button == PadButton::Start ? "OPTIONS" :
        family == PadFamily::PlayStation5 ? "CREATE" : "SHARE") : pad_button_name(family, button), alpha);
}

float pad_menu_combo(ImDrawList* d, ImVec2 p, PadFamily family, ImFont* font, float h, float alpha) {
    float x = p.x + pad_glyph(d, p, family, PadButton::Back, font, h, alpha);
    const ImVec2 plus = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, "+");
    if (d) d->AddText(font, font->FontSize, {x + 5.f, p.y + (h - plus.y) * .5f}, with_alpha(color::text_muted, alpha), "+");
    x += plus.x + 10.f;
    return x + pad_glyph(d, {x, p.y}, family, PadButton::Start, font, h, alpha) - p.x;
}
std::string pad_input_name(int input, PadFamily family) {
    const bool sony = family == PadFamily::PlayStation || family == PadFamily::PlayStation5;
    switch (input) {
    case pad_unbound: return "Non attribué";
    case pad_left_trigger:
        return family == PadFamily::Xbox ? "LT" : sony ? "L2" : family == PadFamily::Nintendo ? "ZL" : "Gâchette gauche";
    case pad_right_trigger:
        return family == PadFamily::Xbox ? "RT" : sony ? "R2" : family == PadFamily::Nintendo ? "ZR" : "Gâchette droite";
    case SDL_CONTROLLER_BUTTON_A: return pad_button_name(family, PadButton::South);
    case SDL_CONTROLLER_BUTTON_B: return pad_button_name(family, PadButton::East);
    case SDL_CONTROLLER_BUTTON_X: return pad_button_name(family, PadButton::West);
    case SDL_CONTROLLER_BUTTON_Y: return pad_button_name(family, PadButton::North);
    case SDL_CONTROLLER_BUTTON_BACK: return pad_button_name(family, PadButton::Back);
    case SDL_CONTROLLER_BUTTON_START: return pad_button_name(family, PadButton::Start);
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return pad_button_name(family, PadButton::LeftShoulder);
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return pad_button_name(family, PadButton::RightShoulder);
    case SDL_CONTROLLER_BUTTON_GUIDE: return "Guide";
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return sony ? "L3" : "Clic stick gauche";
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return sony ? "R3" : "Clic stick droit";
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return "Croix haut";
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return "Croix bas";
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return "Croix gauche";
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return "Croix droite";
    case SDL_CONTROLLER_BUTTON_MISC1:
        return family == PadFamily::Xbox ? "Partager" : family == PadFamily::Nintendo ? "Capture" :
               family == PadFamily::PlayStation5 ? "Micro" : "Bouton supplémentaire";
    case SDL_CONTROLLER_BUTTON_PADDLE1: return "Palette 1";
    case SDL_CONTROLLER_BUTTON_PADDLE2: return "Palette 2";
    case SDL_CONTROLLER_BUTTON_PADDLE3: return "Palette 3";
    case SDL_CONTROLLER_BUTTON_PADDLE4: return "Palette 4";
    case SDL_CONTROLLER_BUTTON_TOUCHPAD: return "Pavé tactile";
    default: return "Bouton " + std::to_string(input);
    }
}
} // namespace ravenemudsk_ui

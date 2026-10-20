// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <ravenemudsk/ui/theme.hpp>
#include <SDL.h>
#include <imgui.h>
#include <string>
#include <vector>
namespace ravenemudsk_ui {
// Vector icons drawn with ImDrawList: no icon font or image is shipped.
enum class Icon {
    Home, Library, Recent, Trophy, Settings, Search, Play, Plus, Folder,
    Close, Minimize, Maximize, Restore, Pause, Save, Load, Reset,
    Fullscreen, Exit, Clock, Check, Lock, Gamepad, Keyboard, Info, Trash
};
void draw_icon(ImDrawList* draw, Icon icon, ImVec2 center, float size, ImU32 color);

enum class ButtonStyle { Primary, Secondary, Ghost, Danger };
// Rounded button with an optional leading icon. Width 0 fits the label.
bool button(const char* label, Icon icon, ButtonStyle style = ButtonStyle::Secondary,
            ImVec2 size = {0.f, 40.f}, bool with_icon = true);
// Square icon-only button; the tooltip doubles as its accessible label.
bool icon_button(const char* id, Icon icon, const char* tooltip, float size = 36.f,
                 ImU32 hover = color::surface_hover);
// On/off switch with the colours of the chips; returns true when toggled.
bool toggle(const char* id, bool* value);
// Segmented filter chips; returns true when the selection changes.
bool chips(const char* id, const char* const* labels, int count, int* selected);
void pill(ImDrawList* draw, ImVec2 pos, const char* text, ImU32 color, ImFont* font);
ImVec2 pill_size(const char* text, ImFont* font);
// Rounded rectangle with a vertical gradient.
void gradient_rect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom,
                   float rounding, ImDrawFlags flags = 0);
ImU32 mix(ImU32 a, ImU32 b, float t);
void progress_bar(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction, ImU32 fill);
// Focus ring for custom-drawn items when keyboard/gamepad navigation is active.
void focus_ring(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding);
// Text clipped to max_width with an ellipsis, using whole UTF-8 characters.
std::string ellipsize(ImFont* font, const std::string& text, float max_width);

// Game artwork: the user's local image, or generated platform art.
void draw_cover(ImDrawList* draw, ImVec2 min, ImVec2 max, const std::string& title,
                const std::string& system, SDL_Texture* image, const Fonts& fonts,
                float rounding = radius::md);

// Round player avatar: the chosen image, or initials on one of the colours.
inline constexpr int avatar_color_count = 8;
ImU32 avatar_color(int index);
void draw_avatar(ImDrawList* draw, ImVec2 center, float radius, const std::string& name,
                 int color, SDL_Texture* image, const Fonts& fonts);
// One or two capital initials of a title or name ("?" when empty).
std::string initials(const std::string& text);

struct Toast {
    enum class Kind { Info, Success, Achievement, Error } kind = Kind::Info;
    std::string title;
    std::string body;
    double born = -1.0;      // set on first draw
    double lifetime = 4.0;
};
// Stacked toasts in the top-right corner; expired ones are removed.
void draw_toasts(std::vector<Toast>& toasts, const Fonts& fonts, float top = space::lg);
} // namespace ravenemudsk_ui

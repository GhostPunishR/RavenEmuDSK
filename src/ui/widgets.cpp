// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/widgets.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <utility>
namespace ravenemudsk_ui {
namespace {
constexpr float pi = 3.14159265f;
} // namespace

std::string initials(const std::string& title) {
    std::string out;
    bool word_start = true;
    for (std::size_t i = 0; i < title.size() && out.size() < 2;) {
        const auto c = static_cast<unsigned char>(title[i]);
        const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        const bool letter = c >= 0x80 || std::isalnum(c);
        if (letter && word_start) {
            out += c < 0x80 ? std::string(1, static_cast<char>(std::toupper(c)))
                            : title.substr(i, len);
        }
        word_start = !letter;
        i += len;
    }
    return out.empty() ? "?" : out;
}

ImU32 mix(ImU32 a, ImU32 b, float t) {
    const ImVec4 x = ImGui::ColorConvertU32ToFloat4(a);
    const ImVec4 y = ImGui::ColorConvertU32ToFloat4(b);
    return ImGui::ColorConvertFloat4ToU32({x.x + (y.x - x.x) * t, x.y + (y.y - x.y) * t,
                                           x.z + (y.z - x.z) * t, x.w + (y.w - x.w) * t});
}
// AddRectFilledMultiColor cannot round its corners: shade a rounded shape.
void gradient_rect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 top, ImU32 bottom,
                   float rounding, ImDrawFlags flags) {
    const int start = draw->VtxBuffer.Size;
    draw->AddRectFilled(min, max, IM_COL32_WHITE, rounding, flags);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size,
                                                  min, {min.x, max.y}, top, bottom);
}

void draw_icon(ImDrawList* d, Icon icon, ImVec2 c, float size, ImU32 col) {
    const float s = size * .5f;
    const float t = std::max(1.5f, size / 11.f);
    auto line = [&](float x0, float y0, float x1, float y1) {
        d->AddLine({c.x + x0 * s, c.y + y0 * s}, {c.x + x1 * s, c.y + y1 * s}, col, t);
    };
    auto poly = [&](std::initializer_list<ImVec2> pts, bool closed = false) {
        for (const auto& p : pts) d->PathLineTo({c.x + p.x * s, c.y + p.y * s});
        d->PathStroke(col, closed ? ImDrawFlags_Closed : 0, t);
    };
    auto rect = [&](float x0, float y0, float x1, float y1, float r = 0.f) {
        d->AddRect({c.x + x0 * s, c.y + y0 * s}, {c.x + x1 * s, c.y + y1 * s}, col, r * s, 0, t);
    };
    auto tray = [&](bool up) {
        poly({{-.8f, .25f}, {-.8f, .8f}, {.8f, .8f}, {.8f, .25f}});
        line(0, -.85f, 0, .35f);
        if (up) poly({{-.4f, -.45f}, {0, -.85f}, {.4f, -.45f}});
        else poly({{-.4f, -.05f}, {0, .35f}, {.4f, -.05f}});
    };
    switch (icon) {
    case Icon::Home:
        poly({{-.85f, -.05f}, {0, -.8f}, {.85f, -.05f}});
        poly({{-.6f, -.25f}, {-.6f, .8f}, {.6f, .8f}, {.6f, -.25f}});
        poly({{-.2f, .8f}, {-.2f, .3f}, {.2f, .3f}, {.2f, .8f}});
        break;
    case Icon::Library:
        rect(-.8f, -.8f, -.1f, -.1f, .15f); rect(.1f, -.8f, .8f, -.1f, .15f);
        rect(-.8f, .1f, -.1f, .8f, .15f); rect(.1f, .1f, .8f, .8f, .15f);
        break;
    case Icon::Recent: case Icon::Clock:
        d->AddCircle(c, .82f * s, col, 0, t);
        line(0, 0, 0, -.5f); line(0, 0, .38f, .25f);
        break;
    case Icon::RetroAchievements:
        // Angular RA monogram with the same stroke, tint and sizing as all menu icons.
        // R: upright, top bowl and descending leg. A: rising diagonals and crossbar.
        poly({{-.93f, .82f}, {-.93f, -.82f}, {-.24f, -.82f},
              {-.12f, -.6f}, {-.12f, -.21f}, {-.33f, -.07f}, {-.93f, -.07f}});
        line(-.49f, -.07f, -.06f, .82f);
        poly({{.00f, .82f}, {.42f, -.82f}, {.92f, .82f}});
        line(.19f, .18f, .73f, .18f);
        break;
    case Icon::Trophy:
        d->PathLineTo({c.x - .55f * s, c.y - .78f * s});
        d->PathArcTo({c.x, c.y - .22f * s}, .55f * s, pi, 0.f, 16);
        d->PathLineTo({c.x + .55f * s, c.y - .78f * s});
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->PathArcTo({c.x - .55f * s, c.y - .45f * s}, .26f * s, pi * .5f, pi * 1.5f, 8);
        d->PathStroke(col, 0, t);
        d->PathArcTo({c.x + .55f * s, c.y - .45f * s}, .26f * s, -pi * .5f, pi * .5f, 8);
        d->PathStroke(col, 0, t);
        line(0, .33f, 0, .62f); line(-.4f, .8f, .4f, .8f);
        break;
    case Icon::Settings: {
        // Eight-tooth gear outline around a hub.
        constexpr int teeth = 8;
        const float step = 2.f * pi / teeth;
        for (int i = 0; i < teeth; ++i) {
            const float a = i * step;
            for (const auto& [angle, r] : {std::pair{a - step * .5f, .62f}, std::pair{a - step * .22f, .62f},
                                           std::pair{a - step * .16f, .88f}, std::pair{a + step * .16f, .88f},
                                           std::pair{a + step * .22f, .62f}})
                d->PathLineTo({c.x + std::cos(angle) * r * s, c.y + std::sin(angle) * r * s});
        }
        d->PathStroke(col, ImDrawFlags_Closed, t);
        d->AddCircle(c, .26f * s, col, 0, t);
        break;
    }
    case Icon::Search:
        d->AddCircle({c.x - .15f * s, c.y - .15f * s}, .55f * s, col, 0, t);
        line(.27f, .27f, .8f, .8f);
        break;
    case Icon::Play:
        d->AddTriangleFilled({c.x - .5f * s, c.y - .75f * s}, {c.x + .75f * s, c.y},
                             {c.x - .5f * s, c.y + .75f * s}, col);
        break;
    case Icon::Plus:
        line(-.75f, 0, .75f, 0); line(0, -.75f, 0, .75f);
        break;
    case Icon::Folder:
        poly({{-.85f, .7f}, {-.85f, -.65f}, {-.25f, -.65f}, {-.05f, -.4f}, {.85f, -.4f},
              {.85f, .7f}}, true);
        break;
    case Icon::Close:
        line(-.6f, -.6f, .6f, .6f); line(-.6f, .6f, .6f, -.6f);
        break;
    case Icon::Minimize:
        line(-.6f, 0, .6f, 0);
        break;
    case Icon::Maximize:
        rect(-.6f, -.6f, .6f, .6f, .1f);
        break;
    case Icon::Restore:
        rect(-.6f, -.3f, .3f, .6f, .1f);
        poly({{-.3f, -.3f}, {-.3f, -.6f}, {.6f, -.6f}, {.6f, .3f}, {.3f, .3f}});
        break;
    case Icon::Pause:
        d->AddRectFilled({c.x - .55f * s, c.y - .7f * s}, {c.x - .15f * s, c.y + .7f * s}, col, 2.f);
        d->AddRectFilled({c.x + .15f * s, c.y - .7f * s}, {c.x + .55f * s, c.y + .7f * s}, col, 2.f);
        break;
    case Icon::Save: tray(false); break;
    case Icon::Load: tray(true); break;
    case Icon::Reset:
        d->PathArcTo(c, .7f * s, -pi * .35f, pi * 1.35f, 20);
        d->PathStroke(col, 0, t);
        poly({{.25f, -.95f}, {.47f, -.55f}, {.02f, -.42f}});
        break;
    case Icon::Fullscreen:
        poly({{-.8f, -.35f}, {-.8f, -.8f}, {-.35f, -.8f}});
        poly({{.35f, -.8f}, {.8f, -.8f}, {.8f, -.35f}});
        poly({{.8f, .35f}, {.8f, .8f}, {.35f, .8f}});
        poly({{-.35f, .8f}, {-.8f, .8f}, {-.8f, .35f}});
        break;
    case Icon::Exit:
        poly({{.1f, -.8f}, {-.75f, -.8f}, {-.75f, .8f}, {.1f, .8f}});
        line(-.25f, 0, .85f, 0);
        poly({{.5f, -.35f}, {.85f, 0}, {.5f, .35f}});
        break;
    case Icon::Check:
        poly({{-.7f, .02f}, {-.22f, .5f}, {.72f, -.5f}});
        break;
    case Icon::Lock:
        rect(-.62f, -.1f, .62f, .8f, .15f);
        d->PathArcTo({c.x, c.y - .3f * s}, .38f * s, pi, 2.f * pi, 10);
        d->PathLineTo({c.x + .38f * s, c.y - .1f * s});
        d->PathStroke(col, 0, t);
        d->PathLineTo({c.x - .38f * s, c.y - .1f * s});
        d->PathLineTo({c.x - .38f * s, c.y - .3f * s});
        d->PathStroke(col, 0, t);
        break;
    case Icon::Gamepad:
        rect(-.9f, -.5f, .9f, .55f, .45f);
        line(-.55f, 0, -.15f, 0); line(-.35f, -.2f, -.35f, .2f);
        d->AddCircleFilled({c.x + .3f * s, c.y + .1f * s}, .1f * s + 1.f, col);
        d->AddCircleFilled({c.x + .55f * s, c.y - .12f * s}, .1f * s + 1.f, col);
        break;
    case Icon::Keyboard:
        rect(-.9f, -.5f, .9f, .55f, .15f);
        for (int i = 0; i < 4; ++i)
            d->AddCircleFilled({c.x + (-.55f + i * .37f) * s, c.y - .15f * s}, t * .6f, col);
        line(-.45f, .25f, .45f, .25f);
        break;
    case Icon::Info:
        d->AddCircle(c, .82f * s, col, 0, t);
        line(0, -.05f, 0, .45f);
        d->AddCircleFilled({c.x, c.y - .38f * s}, t * .7f, col);
        break;
    case Icon::Trash:
        line(-.75f, -.55f, .75f, -.55f);
        poly({{-.25f, -.55f}, {-.25f, -.8f}, {.25f, -.8f}, {.25f, -.55f}});
        poly({{-.55f, -.55f}, {-.45f, .8f}, {.45f, .8f}, {.55f, -.55f}});
        break;
    }
}

void focus_ring(ImDrawList* draw, ImVec2 min, ImVec2 max, float rounding) {
    if (!ImGui::GetIO().NavVisible || !ImGui::IsItemFocused()) return;
    draw->AddRect({min.x - 3, min.y - 3}, {max.x + 3, max.y + 3}, color::accent_hover,
                  rounding + 3.f, 0, 2.f);
}

bool button(const char* label, Icon icon, ButtonStyle style, ImVec2 size, bool with_icon) {
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 text = ImGui::CalcTextSize(label, end);
    const float icon_w = with_icon ? 16.f + space::sm : 0.f;
    if (size.x <= 0.f) size.x = text.x + icon_w + space::lg * 2.f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    ImU32 bg = 0, fg = color::text, border = 0;
    switch (style) {
    case ButtonStyle::Primary:
        bg = held ? color::accent : hovered ? color::accent_hover : color::accent;
        fg = IM_COL32_WHITE;
        break;
    case ButtonStyle::Secondary:
        bg = hovered ? color::surface_hover : color::surface;
        border = color::border_strong;
        break;
    case ButtonStyle::Ghost:
        bg = hovered ? color::surface_hover : 0;
        fg = hovered ? color::text : color::text_muted;
        break;
    case ButtonStyle::Danger:
        bg = hovered ? with_alpha(color::danger, .18f) : 0;
        fg = color::danger;
        break;
    }
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 max{pos.x + size.x, pos.y + size.y};
    if (style == ButtonStyle::Primary && hovered)
        d->AddRectFilled({pos.x, pos.y + 3}, {max.x, max.y + 3}, with_alpha(color::accent, .35f),
                         radius::md);
    if (bg) d->AddRectFilled(pos, max, bg, radius::md);
    if (border) d->AddRect(pos, max, border, radius::md);
    const float x = pos.x + (size.x - text.x - icon_w) * .5f;
    const float y = pos.y + size.y * .5f;
    if (with_icon) draw_icon(d, icon, {x + 8.f, y}, 15.f, fg);
    d->AddText({x + icon_w, y - text.y * .5f}, fg, label, end);
    focus_ring(d, pos, max, radius::md);
    return pressed;
}

bool icon_button(const char* id, Icon icon, const char* tooltip, float size, ImU32 hover) {
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, {size, size});
    const bool hovered = ImGui::IsItemHovered();
    auto* d = ImGui::GetWindowDrawList();
    const ImVec2 max{pos.x + size, pos.y + size};
    if (hovered) d->AddRectFilled(pos, max, hover, radius::md);
    draw_icon(d, icon, {pos.x + size * .5f, pos.y + size * .5f}, size * .45f,
              hovered ? color::text : color::text_muted);
    focus_ring(d, pos, max, radius::md);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", tooltip);
    return pressed;
}

bool chips(const char* id, const char* const* labels, int count, int* selected) {
    bool changed = false;
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0.f, space::sm);
        const ImVec2 text = ImGui::CalcTextSize(labels[i]);
        const ImVec2 size{text.x + space::lg * 2.f, 32.f};
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##chip", size) && *selected != i) {
            *selected = i;
            changed = true;
        }
        ImGui::PopID();
        const bool on = *selected == i;
        const bool hovered = ImGui::IsItemHovered();
        auto* d = ImGui::GetWindowDrawList();
        const ImVec2 max{pos.x + size.x, pos.y + size.y};
        d->AddRectFilled(pos, max, on ? color::accent_soft :
                         hovered ? color::surface_hover : color::surface, size.y * .5f);
        d->AddRect(pos, max, on ? with_alpha(color::accent, .8f) : color::border,
                   size.y * .5f);
        d->AddText({pos.x + space::lg, pos.y + (size.y - text.y) * .5f},
                   on ? color::text : color::text_muted, labels[i]);
        focus_ring(d, pos, max, size.y * .5f);
    }
    ImGui::PopID();
    return changed;
}

ImVec2 pill_size(const char* text, ImFont* font) {
    const ImVec2 t = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
    return {t.x + space::md * 1.5f, t.y + space::xs * 1.5f};
}
void pill(ImDrawList* d, ImVec2 pos, const char* text, ImU32 col, ImFont* font) {
    const ImVec2 size = pill_size(text, font);
    d->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y}, with_alpha(col, .16f), size.y * .5f);
    d->AddText(font, font->FontSize, {pos.x + space::md * .75f, pos.y + space::xs * .75f},
               col, text);
}

void progress_bar(ImDrawList* d, ImVec2 min, ImVec2 max, float fraction, ImU32 fill) {
    const float r = (max.y - min.y) * .5f;
    d->AddRectFilled(min, max, color::elevated, r);
    fraction = std::clamp(fraction, 0.f, 1.f);
    if (fraction > 0.f)
        d->AddRectFilled(min, {min.x + std::max(max.y - min.y, (max.x - min.x) * fraction), max.y},
                         fill, r);
}

std::string ellipsize(ImFont* font, const std::string& text, float max_width) {
    auto width = [&](const std::string& s) {
        return font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, s.c_str()).x;
    };
    if (width(text) <= max_width) return text;
    std::string out = text;
    while (!out.empty()) {
        do out.pop_back();  // drop a whole UTF-8 character
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80);
        if (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0xC0) out.pop_back();
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (width(out + "\xE2\x80\xA6") <= max_width) return out + "\xE2\x80\xA6";
    }
    return "\xE2\x80\xA6";
}

void draw_cover(ImDrawList* d, ImVec2 min, ImVec2 max, const std::string& title,
                const std::string& system, SDL_Texture* image, const Fonts& fonts,
                float rounding) {
    const float w = max.x - min.x, h = max.y - min.y;
    const ImU32 tint = system_color(system);
    int iw = 0, ih = 0;
    if (image && SDL_QueryTexture(image, nullptr, nullptr, &iw, &ih) == 0 && iw > 0 && ih > 0) {
        d->AddRectFilled(min, max, color::elevated, rounding);
        const float box = w / h, art = static_cast<float>(iw) / static_cast<float>(ih);
        const auto id = reinterpret_cast<ImTextureID>(image);
        if (std::abs(art / box - 1.f) < .25f) {
            // Close enough: fill the frame and crop the overflow evenly.
            ImVec2 uv0{0, 0}, uv1{1, 1};
            if (art > box) { const float k = box / art; uv0.x = (1 - k) * .5f; uv1.x = 1 - uv0.x; }
            else { const float k = art / box; uv0.y = (1 - k) * .5f; uv1.y = 1 - uv0.y; }
            d->AddImageRounded(id, min, max, uv0, uv1, IM_COL32_WHITE, rounding);
        } else {
            const float k = std::min(w / iw, h / ih);
            const ImVec2 a{min.x + (w - iw * k) * .5f, min.y + (h - ih * k) * .5f};
            d->AddImageRounded(id, a, {a.x + iw * k, a.y + ih * k}, {0, 0}, {1, 1},
                               IM_COL32_WHITE, rounding * .6f);
        }
        return;
    }
    // Generated art: tinted gradient and a stylised cartridge with initials.
    gradient_rect(d, min, max, mix(color::surface, tint, .38f), mix(color::background, tint, .08f),
                  rounding);
    d->PushClipRect(min, max, true);
    d->AddCircleFilled({max.x - w * .1f, min.y + h * .12f}, w * .55f, with_alpha(tint, .10f), 48);
    d->AddCircleFilled({min.x + w * .05f, max.y}, w * .35f, with_alpha(tint, .07f), 48);
    d->PopClipRect();
    const bool wide = system == "gba";
    const float cw = wide ? w * .62f : w * .46f, ch = wide ? h * .40f : h * .56f;
    const ImVec2 a{min.x + (w - cw) * .5f, min.y + (h - ch) * .46f};
    const ImVec2 b{a.x + cw, a.y + ch};
    const float notch = cw * .14f;
    d->PathLineTo(a);
    d->PathLineTo({b.x - (wide ? 0.f : notch), a.y});
    if (!wide) d->PathLineTo({b.x, a.y + notch});
    d->PathLineTo(b);
    d->PathLineTo({a.x, b.y});
    d->PathFillConvex(mix(color::elevated, tint, .25f));
    for (int i = 0; i < 4; ++i) {
        const float y = a.y + ch * (.06f + i * .035f);
        d->AddLine({a.x + cw * .2f, y}, {a.x + cw * .62f, y}, with_alpha(color::background, .45f),
                   1.f);
    }
    const ImVec2 la{a.x + cw * .12f, a.y + ch * (wide ? .28f : .26f)};
    const ImVec2 lb{b.x - cw * .12f, b.y - ch * .12f};
    gradient_rect(d, la, lb, mix(tint, IM_COL32_WHITE, .25f), tint, radius::sm * .6f);
    const std::string mark = initials(title);
    ImFont* f = fonts.display;
    const float fs = std::min(f->FontSize * (w / 160.f), (lb.y - la.y) * .7f);
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.f, mark.c_str());
    d->AddText(f, fs, {(la.x + lb.x - ts.x) * .5f, (la.y + lb.y - ts.y) * .5f},
               mix(color::background, tint, .2f), mark.c_str());
}

ImU32 avatar_color(int index) {
    static constexpr ImU32 colors[avatar_color_count] = {
        IM_COL32(124, 92, 255, 255), IM_COL32(64, 190, 255, 255), IM_COL32(74, 214, 140, 255),
        IM_COL32(244, 193, 82, 255), IM_COL32(255, 138, 101, 255), IM_COL32(236, 93, 147, 255),
        IM_COL32(45, 212, 191, 255), IM_COL32(148, 163, 184, 255)};
    return colors[((index % avatar_color_count) + avatar_color_count) % avatar_color_count];
}
void draw_avatar(ImDrawList* d, ImVec2 c, float r, const std::string& name, int color,
                 SDL_Texture* image, const Fonts& fonts) {
    int iw = 0, ih = 0;
    if (image && SDL_QueryTexture(image, nullptr, nullptr, &iw, &ih) == 0 && iw > 0 && ih > 0) {
        // Centre-crop to a square, then round it into a circle.
        ImVec2 uv0{0, 0}, uv1{1, 1};
        if (iw > ih) { uv0.x = (1.f - static_cast<float>(ih) / iw) * .5f; uv1.x = 1.f - uv0.x; }
        else { uv0.y = (1.f - static_cast<float>(iw) / ih) * .5f; uv1.y = 1.f - uv0.y; }
        d->AddImageRounded(reinterpret_cast<ImTextureID>(image), {c.x - r, c.y - r}, {c.x + r, c.y + r},
                           uv0, uv1, IM_COL32_WHITE, r);
    } else {
        const ImU32 tint = avatar_color(color);
        gradient_rect(d, {c.x - r, c.y - r}, {c.x + r, c.y + r}, mix(tint, IM_COL32_WHITE, .18f),
                      mix(tint, color::background, .25f), r);
        const std::string mark = initials(name);
        ImFont* f = fonts.display;
        const float fs = r * .9f;
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.f, mark.c_str());
        d->AddText(f, fs, {c.x - ts.x * .5f, c.y - ts.y * .5f}, IM_COL32_WHITE, mark.c_str());
    }
    d->AddCircle(c, r, color::border_strong, 0, 1.f);
}

void draw_toasts(std::vector<Toast>& toasts, const Fonts& fonts, float top) {
    const double now = ImGui::GetTime();
    auto* d = ImGui::GetForegroundDrawList();
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    const float width = std::min(360.f, screen.x - space::xxl);
    float y = top;
    for (auto& toast : toasts) if (toast.born < 0) toast.born = now;
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) {
        return now - t.born > t.lifetime;
    }), toasts.end());
    for (const auto& toast : toasts) {
        const float age = static_cast<float>(now - toast.born);
        const float in = std::clamp(age / .25f, 0.f, 1.f);
        const float out = std::clamp((static_cast<float>(toast.lifetime) - age) / .4f, 0.f, 1.f);
        const float ease = 1.f - (1.f - in) * (1.f - in);
        const float alpha = std::min(in, out);
        const bool trophy = toast.kind == Toast::Kind::Achievement;
        const float height = toast.body.empty() ? 56.f : 72.f;
        const float x = screen.x - width - space::lg + (1.f - ease) * 40.f;
        const ImVec2 a{x, y}, b{x + width, y + height};
        const ImU32 accent = trophy ? color::gold :
            toast.kind == Toast::Kind::Success ? color::success :
            toast.kind == Toast::Kind::Error ? color::danger : color::accent;
        d->AddRectFilled({a.x, a.y + 6}, {b.x, b.y + 6}, with_alpha(color::shadow, alpha), radius::lg);
        d->AddRectFilled(a, b, with_alpha(color::elevated, alpha), radius::lg);
        d->AddRect(a, b, with_alpha(trophy ? with_alpha(color::gold, .7f) : color::border_strong,
                                    alpha), radius::lg, 0, trophy ? 1.5f : 1.f);
        const ImVec2 ic{a.x + 32.f, a.y + height * .5f};
        d->AddCircleFilled(ic, 18.f, with_alpha(with_alpha(accent, .18f), alpha), 32);
        const Icon icon = trophy ? Icon::Trophy : toast.kind == Toast::Kind::Success ? Icon::Check :
            toast.kind == Toast::Kind::Error ? Icon::Close : Icon::Info;
        draw_icon(d, icon, ic, 18.f, with_alpha(accent, alpha));
        const float tx = a.x + 62.f, room = width - 62.f - space::lg;
        if (toast.body.empty()) {
            d->AddText(fonts.strong, fonts.strong->FontSize, {tx, ic.y - 9.f},
                       with_alpha(color::text, alpha), ellipsize(fonts.strong, toast.title, room).c_str());
        } else {
            d->AddText(fonts.strong, fonts.strong->FontSize, {tx, a.y + 15.f},
                       with_alpha(trophy ? color::gold : color::text, alpha),
                       ellipsize(fonts.strong, toast.title, room).c_str());
            d->AddText(fonts.body, fonts.body->FontSize, {tx, a.y + 39.f},
                       with_alpha(color::text_muted, alpha),
                       ellipsize(fonts.body, toast.body, room).c_str());
        }
        y += height + space::sm;
    }
}
} // namespace ravenemudsk_ui

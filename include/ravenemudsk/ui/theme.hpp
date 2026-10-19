// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <imgui.h>
#include <string>
namespace ravenemudsk_ui {
// Design tokens shared by the launcher and the in-game overlay.
namespace color {
inline constexpr ImU32 background = IM_COL32(11, 12, 18, 255);
inline constexpr ImU32 sidebar = IM_COL32(15, 16, 25, 255);
inline constexpr ImU32 surface = IM_COL32(25, 27, 40, 255);
inline constexpr ImU32 surface_hover = IM_COL32(37, 39, 57, 255);
inline constexpr ImU32 elevated = IM_COL32(44, 45, 65, 255);
inline constexpr ImU32 border = IM_COL32(255, 255, 255, 20);
inline constexpr ImU32 border_strong = IM_COL32(255, 255, 255, 40);
inline constexpr ImU32 text = IM_COL32(245, 242, 255, 255);
inline constexpr ImU32 text_muted = IM_COL32(203, 196, 220, 255);
inline constexpr ImU32 text_faint = IM_COL32(104, 114, 140, 255);
inline constexpr ImU32 accent = IM_COL32(167, 139, 250, 255);
inline constexpr ImU32 accent_hover = IM_COL32(187, 164, 255, 255);
inline constexpr ImU32 accent_soft = IM_COL32(167, 139, 250, 46);
inline constexpr ImU32 gold = IM_COL32(244, 193, 82, 255);
inline constexpr ImU32 gold_soft = IM_COL32(244, 193, 82, 40);
inline constexpr ImU32 success = IM_COL32(74, 214, 140, 255);
inline constexpr ImU32 danger = IM_COL32(232, 72, 85, 255);
inline constexpr ImU32 shadow = IM_COL32(0, 0, 0, 90);
} // namespace color

// Spacing follows a 4 px grid.
namespace space {
inline constexpr float xs = 4.f, sm = 8.f, md = 12.f, lg = 16.f, xl = 24.f, xxl = 32.f;
} // namespace space
namespace radius {
inline constexpr float sm = 6.f, md = 10.f, lg = 14.f;
} // namespace radius

struct Fonts {
    ImFont* body = nullptr;      // 16 px, default UI text
    ImFont* strong = nullptr;    // 16 px semibold
    ImFont* caption = nullptr;   // 13 px captions and labels ("small" is a Win32 macro)
    ImFont* title = nullptr;     // 20 px section titles
    ImFont* display = nullptr;   // 30 px page and game titles
    // TV mode, read from a couch: loaded only when requested.
    ImFont* tv_body = nullptr;   // 22 px
    ImFont* tv_title = nullptr;  // 28 px semibold
    ImFont* tv_display = nullptr;  // 50 px semibold
};

// Platform colors, used for badges and generated cover art.
ImU32 system_color(const std::string& system);
const char* system_short(const std::string& system);
const char* system_name(const std::string& system);

void apply_theme();
// Loads system UI fonts (Segoe UI on Windows, DejaVu/Noto elsewhere) with
// French glyphs; falls back to the ImGui default font if none is found.
Fonts load_fonts(bool tv_sizes = false);
ImU32 with_alpha(ImU32 color, float alpha);
} // namespace ravenemudsk_ui

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/theme.hpp>
#include <ravenemudsk/ui/widgets.hpp>
#include <filesystem>
#include <initializer_list>
namespace ravenemudsk_ui {
namespace {
ImVec4 vec(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
const char* first_existing(std::initializer_list<const char*> paths) {
    std::error_code ec;
    for (const char* path : paths)
        if (std::filesystem::is_regular_file(path, ec)) return path;
    return nullptr;
}
} // namespace

ImU32 with_alpha(ImU32 color, float alpha) {
    const auto a = static_cast<ImU32>(((color >> IM_COL32_A_SHIFT) & 0xFF) * alpha);
    return (color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
}
ImU32 system_color(const std::string& system) {
    if (system == "gba") return IM_COL32(255, 138, 101, 255);
    if (system == "gbc") return IM_COL32(64, 190, 255, 255);
    return IM_COL32(139, 204, 82, 255);
}
const char* system_short(const std::string& system) {
    return system == "gba" ? "GBA" : system == "gbc" ? "GBC" : "GB";
}
const char* system_name(const std::string& system) {
    return system == "gba" ? "Game Boy Advance" :
           system == "gbc" ? "Game Boy Color" : "Game Boy";
}

const char* theme_name(Theme theme) {
    return theme == Theme::Nvidia ? "NVIDIA" : "RavenEmu";
}
void set_theme(Theme theme) {
    using namespace color;
    if (theme == Theme::Nvidia) {
        // NVIDIA's green (#76B900) on black and neutral grays; dark text on
        // the green, which white text would not stand out from.
        background = IM_COL32(12, 12, 12, 255);
        sidebar = IM_COL32(18, 18, 18, 255);
        surface = IM_COL32(30, 30, 30, 255);
        surface_hover = IM_COL32(42, 42, 42, 255);
        elevated = IM_COL32(52, 52, 52, 255);
        text = IM_COL32(245, 245, 245, 255);
        text_muted = IM_COL32(204, 204, 204, 255);
        text_faint = IM_COL32(128, 128, 128, 255);
        accent = IM_COL32(118, 185, 0, 255);
        accent_hover = IM_COL32(141, 207, 34, 255);
        accent_soft = IM_COL32(118, 185, 0, 46);
        on_accent = IM_COL32(0, 0, 0, 255);
    } else {
        // Exact palette from RavenEmu RavenColors.kt.
        background = IM_COL32(11, 12, 18, 255);
        sidebar = IM_COL32(15, 16, 25, 255);
        surface = IM_COL32(25, 27, 40, 255);
        surface_hover = IM_COL32(37, 39, 57, 255);
        elevated = IM_COL32(44, 45, 65, 255);
        text = IM_COL32(245, 242, 255, 255);
        text_muted = IM_COL32(203, 196, 220, 255);
        text_faint = IM_COL32(104, 114, 140, 255);
        accent = IM_COL32(167, 139, 250, 255);
        accent_hover = IM_COL32(187, 164, 255, 255);
        accent_soft = IM_COL32(167, 139, 250, 46);
        on_accent = IM_COL32(255, 255, 255, 255);
    }
    if (ImGui::GetCurrentContext()) apply_theme();
}

void apply_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 0.f;
    s.ChildRounding = radius::lg;
    s.FrameRounding = radius::md;
    s.PopupRounding = radius::lg;
    s.GrabRounding = radius::md;
    s.ScrollbarRounding = radius::md;
    s.WindowBorderSize = 0.f;
    s.ChildBorderSize = 0.f;
    s.PopupBorderSize = 1.f;
    s.FrameBorderSize = 0.f;
    s.WindowPadding = {space::xl, space::xl};
    s.FramePadding = {space::md, space::sm + 2.f};
    s.ItemSpacing = {space::md, space::md};
    s.ItemInnerSpacing = {space::sm, space::sm};
    s.ScrollbarSize = 8.f;
    s.SelectableTextAlign = {0.f, .5f};
    auto* c = s.Colors;
    c[ImGuiCol_WindowBg] = vec(color::background);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = vec(color::elevated);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.06f, .72f);
    c[ImGuiCol_Border] = vec(color::border_strong);
    c[ImGuiCol_FrameBg] = vec(color::surface);
    c[ImGuiCol_FrameBgHovered] = vec(color::surface_hover);
    c[ImGuiCol_FrameBgActive] = vec(color::elevated);
    c[ImGuiCol_Text] = vec(color::text);
    c[ImGuiCol_TextDisabled] = vec(color::text_faint);
    c[ImGuiCol_TextSelectedBg] = vec(with_alpha(color::accent, .45f));
    c[ImGuiCol_Button] = vec(color::surface);
    c[ImGuiCol_ButtonHovered] = vec(color::surface_hover);
    c[ImGuiCol_ButtonActive] = vec(color::elevated);
    c[ImGuiCol_Header] = vec(color::accent_soft);
    c[ImGuiCol_HeaderHovered] = vec(color::surface_hover);
    c[ImGuiCol_HeaderActive] = vec(color::elevated);
    c[ImGuiCol_CheckMark] = vec(color::accent);
    c[ImGuiCol_SliderGrab] = vec(color::accent);
    c[ImGuiCol_Separator] = vec(color::border);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = vec(color::elevated);
    c[ImGuiCol_ScrollbarGrabHovered] = vec(color::border_strong);
    c[ImGuiCol_ScrollbarGrabActive] = vec(color::accent);
    c[ImGuiCol_NavCursor] = vec(color::accent_hover);
}

Fonts load_fonts(bool tv_sizes) {
    ImGuiIO& io = ImGui::GetIO();
#ifdef _WIN32
    const char* regular = first_existing({"C:\\Windows\\Fonts\\segoeui.ttf"});
    const char* semibold = first_existing({"C:\\Windows\\Fonts\\seguisb.ttf",
                                           "C:\\Windows\\Fonts\\segoeuib.ttf"});
#elif defined(__APPLE__)
    const char* regular = first_existing({"/System/Library/Fonts/Supplemental/Arial.ttf",
                                          "/Library/Fonts/Arial.ttf"});
    const char* semibold = first_existing({"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                                           "/Library/Fonts/Arial Bold.ttf"});
#else
    const char* regular = first_existing({
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf"});
    const char* semibold = first_existing({
        "/usr/share/fonts/truetype/noto/NotoSans-SemiBold.ttf",
        "/usr/share/fonts/noto/NotoSans-SemiBold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf"});
#endif
    if (!semibold) semibold = regular;
    Fonts fonts;
    if (!regular) {
        fonts.body = io.Fonts->AddFontDefault();
        fonts.strong = fonts.caption = fonts.title = fonts.display = fonts.body;
        fonts.tv_body = fonts.tv_title = fonts.tv_display = fonts.body;
        register_icons(io.Fonts);
        return fonts;
    }
    // Latin-1 plus the typographic signs used in French UI text.
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x0152, 0x0153, 0x2013, 0x2014, 0x2019, 0x2019,
        0x201C, 0x201D, 0x2022, 0x2022, 0x2026, 0x2026, 0x20AC, 0x20AC, 0};
    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    auto add = [&](const char* file, float size) {
        return io.Fonts->AddFontFromFileTTF(file, size, &config, ranges);
    };
    fonts.body = add(regular, 16.f);
    fonts.strong = add(semibold, 16.f);
    fonts.caption = add(regular, 13.f);
    fonts.title = add(semibold, 20.f);
    fonts.display = add(semibold, 30.f);
    if (tv_sizes) {
        fonts.tv_body = add(regular, 22.f);
        fonts.tv_title = add(semibold, 28.f);
        fonts.tv_display = add(semibold, 50.f);
    }
    if (!fonts.body) fonts.body = io.Fonts->AddFontDefault();
    for (ImFont** f : {&fonts.strong, &fonts.caption, &fonts.title, &fonts.display,
                       &fonts.tv_body, &fonts.tv_title, &fonts.tv_display})
        if (!*f) *f = fonts.body;
    io.FontDefault = fonts.body;
    register_icons(io.Fonts);
    return fonts;
}
} // namespace ravenemudsk_ui

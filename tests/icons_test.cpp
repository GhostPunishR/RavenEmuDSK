// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/widgets.hpp>
#include <imgui.h>
#include <cstdlib>
#include <iostream>
#include <numeric>
using namespace ravenemudsk_ui;

namespace {
int failures = 0;
void check(bool ok, const char* what, int icon, int size) {
    if (ok) return;
    std::cerr << what << " (icône " << icon << ", " << size << " px)\n";
    ++failures;
}
constexpr int icon_count = static_cast<int>(Icon::Volume) + 1;

void every_icon_is_drawn_inside_its_square() {
    for (int icon = 0; icon < icon_count; ++icon)
        for (int size : {28, 32, 44, 56}) {
            const auto alpha = rasterize_icon(static_cast<Icon>(icon), size);
            check(std::accumulate(alpha.begin(), alpha.end(), 0) > size * 255, "icône vide", icon, size);
            bool border_clear = true;
            for (int i = 0; i < size; ++i)
                border_clear = border_clear && !alpha[i] && !alpha[(size - 1) * size + i] &&
                               !alpha[i * size] && !alpha[i * size + size - 1];
            check(border_clear, "icône coupée par le bord", icon, size);
        }
}

void symmetric_icons_stay_symmetric() {
    for (Icon icon : {Icon::Plus, Icon::Close, Icon::Maximize, Icon::Library, Icon::Fullscreen, Icon::Pause,
                      Icon::Minimize}) {
        const int size = 48;
        const auto alpha = rasterize_icon(icon, size);
        int worst = 0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                worst = std::max(worst, std::abs(alpha[y * size + x] - alpha[y * size + size - 1 - x]));
        check(worst <= 8, "icône symétrique déformée", static_cast<int>(icon), size);
    }
}

// Same two-unit strokes at every size: the ink grows with the area.
void stroke_weight_follows_the_size() {
    for (int icon = 0; icon < icon_count; ++icon) {
        const auto small = rasterize_icon(static_cast<Icon>(icon), 36);
        const auto large = rasterize_icon(static_cast<Icon>(icon), 56);
        const double a = std::accumulate(small.begin(), small.end(), 0.0) / (36.0 * 36.0);
        const double b = std::accumulate(large.begin(), large.end(), 0.0) / (56.0 * 56.0);
        check(std::abs(a - b) <= .12 * b, "épaisseur irrégulière selon la taille", icon, 36);
    }
}

// The atlas holds the same pixels, and drawing is one tinted quad.
void icons_draw_from_the_font_atlas() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {200, 200};
    io.Fonts->AddFontDefault();
    register_icons(io.Fonts);
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();
    ImDrawList* d = ImGui::GetForegroundDrawList();
    for (int icon = 0; icon < icon_count; ++icon) {
        const int before = d->VtxBuffer.Size;
        draw_icon(d, static_cast<Icon>(icon), {100.3f, 99.6f}, 16.f, IM_COL32(255, 0, 0, 128));
        check(d->VtxBuffer.Size == before + 4, "icône absente de l'atlas", icon, 16);
        if (d->VtxBuffer.Size != before + 4) continue;
        const ImDrawVert& corner = d->VtxBuffer[before];
        check(corner.pos.x == 92.f && corner.pos.y == 92.f, "icône hors de la grille des pixels", icon, 16);
        check(corner.col == IM_COL32(255, 0, 0, 128), "teinte perdue", icon, 16);
        // The quad's texels are the rasterized icon, twice the drawn size.
        const int x0 = static_cast<int>(corner.uv.x * width + .5f), y0 = static_cast<int>(corner.uv.y * height + .5f);
        const auto alpha = rasterize_icon(static_cast<Icon>(icon), 32);
        bool same = true;
        for (int y = 0; y < 32 && same; ++y)
            for (int x = 0; x < 32 && same; ++x)
                same = pixels[((y0 + y) * width + x0 + x) * 4 + 3] == alpha[y * 32 + x];
        check(same, "pixels de l'atlas différents du tracé", icon, 16);
    }
    ImGui::EndFrame();
    ImGui::DestroyContext();
}
} // namespace

int main() {
    every_icon_is_drawn_inside_its_square();
    symmetric_icons_stay_symmetric();
    stroke_weight_follows_the_size();
    icons_draw_from_the_font_atlas();
    if (failures) return EXIT_FAILURE;
    std::cout << "Icons: inside their square, symmetric, even strokes, drawn from the atlas\n";
    return EXIT_SUCCESS;
}

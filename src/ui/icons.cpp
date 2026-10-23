// SPDX-License-Identifier: AGPL-3.0-only
// Interface icons: RavenEmu's own line drawings on a 24-unit grid, strokes
// two units wide with round ends and corners. Each icon is rasterized once
// per size from exact distances to its strokes (no overlapping AA seams) and
// kept in the ImGui font atlas, so it draws as one textured quad tinted with
// any colour, transparent ones included.
#include <ravenemudsk/ui/widgets.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>
namespace ravenemudsk_ui {
namespace {
constexpr float pi = 3.14159265358979f;
constexpr int icon_count = static_cast<int>(Icon::Link) + 1;
// Sizes the screens use; others draw from the nearest one.
constexpr std::array<int, 9> atlas_sizes{14, 15, 16, 17, 18, 20, 22, 24, 28};
// Rasterized at twice the size: sharp on 2x screens, and an exact 2x2
// average through the atlas's linear filter on 1x screens.
constexpr int oversample = 2;

struct Pt { float x, y; };
// A stroked polyline, a stroked circle, or a filled polygon or disc.
struct Shape {
    enum Kind { Path, Ring, Polygon, Disc } kind;
    std::vector<Pt> pts;
    bool closed = false;
    Pt c{};
    float r = 0.f;
};
using Drawing = std::vector<Shape>;

Shape path(std::vector<Pt> pts, bool closed = false) { return {Shape::Path, std::move(pts), closed}; }
Shape ring(Pt c, float r) { return {Shape::Ring, {}, false, c, r}; }
Shape disc(Pt c, float r) { return {Shape::Disc, {}, false, c, r}; }
Shape polygon(std::vector<Pt> pts) { return {Shape::Polygon, std::move(pts), true}; }
// Arc points from a0 to a1 (degrees, y down, so increasing angles turn clockwise).
void arc(std::vector<Pt>& pts, Pt c, float r, float a0, float a1) {
    const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(a1 - a0) / 7.5f)));
    for (int i = 0; i <= steps; ++i) {
        const float a = (a0 + (a1 - a0) * i / steps) * pi / 180.f;
        pts.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
    }
}
std::vector<Pt> rounded_rect(float x0, float y0, float x1, float y1, float r) {
    std::vector<Pt> pts;
    arc(pts, {x1 - r, y0 + r}, r, -90, 0);
    arc(pts, {x1 - r, y1 - r}, r, 0, 90);
    arc(pts, {x0 + r, y1 - r}, r, 90, 180);
    arc(pts, {x0 + r, y0 + r}, r, 180, 270);
    return pts;
}

Drawing drawing(Icon icon) {
    switch (icon) {
    case Icon::Home:
        return {path({{3, 11}, {12, 3.5f}, {21, 11}}),
                path({{5.5f, 9.5f}, {5.5f, 20.5f}, {18.5f, 20.5f}, {18.5f, 9.5f}}),
                path({{10, 20.5f}, {10, 15}, {14, 15}, {14, 20.5f}})};
    case Icon::Library:
        return {path(rounded_rect(3.5f, 3.5f, 10.5f, 10.5f, 1.5f), true),
                path(rounded_rect(13.5f, 3.5f, 20.5f, 10.5f, 1.5f), true),
                path(rounded_rect(3.5f, 13.5f, 10.5f, 20.5f, 1.5f), true),
                path(rounded_rect(13.5f, 13.5f, 20.5f, 20.5f, 1.5f), true)};
    case Icon::Recent:
    case Icon::Clock:
        return {ring({12, 12}, 9), path({{12, 7}, {12, 12}, {15.5f, 14}})};
    case Icon::Trophy: {
        std::vector<Pt> cup{{7, 9}, {7, 3.5f}, {17, 3.5f}, {17, 9}};
        arc(cup, {12, 9}, 5, 0, 180);
        std::vector<Pt> left, right;
        arc(left, {7, 7.5f}, 2.5f, 90, 270);
        arc(right, {17, 7.5f}, 2.5f, -90, 90);
        return {path(cup, true), path(left), path(right),
                path({{12, 14}, {12, 20}}), path({{8, 20.5f}, {16, 20.5f}})};
    }
    case Icon::Settings: {
        // Eight teeth around a hub.
        std::vector<Pt> gear;
        for (int i = 0; i < 8; ++i) {
            const float a = i * 45.f;
            for (const auto [da, r] : {std::pair{-15.f, 7.f}, std::pair{-8.5f, 9.6f},
                                       std::pair{8.5f, 9.6f}, std::pair{15.f, 7.f}}) {
                const float t = (a + da) * pi / 180.f;
                gear.push_back({12 + r * std::cos(t), 12 + r * std::sin(t)});
            }
        }
        return {path(gear, true), ring({12, 12}, 3)};
    }
    case Icon::Search:
        return {ring({10.5f, 10.5f}, 6.5f), path({{15.5f, 15.5f}, {20.5f, 20.5f}})};
    case Icon::Play:
        return {polygon({{7.5f, 5}, {19, 12}, {7.5f, 19}}), path({{7.5f, 5}, {19, 12}, {7.5f, 19}}, true)};
    case Icon::Plus:
        return {path({{12, 5}, {12, 19}}), path({{5, 12}, {19, 12}})};
    case Icon::Folder:
        return {path({{3.5f, 19.5f}, {3.5f, 5}, {9.5f, 5}, {11.5f, 7.5f}, {20.5f, 7.5f}, {20.5f, 19.5f}}, true)};
    case Icon::Close:
        return {path({{6, 6}, {18, 18}}), path({{18, 6}, {6, 18}})};
    case Icon::Minimize:
        return {path({{5, 12}, {19, 12}})};
    case Icon::Maximize:
        return {path(rounded_rect(5, 5, 19, 19, 2), true)};
    case Icon::Restore:
        return {path(rounded_rect(4.5f, 8.5f, 15.5f, 19.5f, 1.5f), true),
                path({{8.5f, 8.5f}, {8.5f, 4.5f}, {19.5f, 4.5f}, {19.5f, 15.5f}, {15.5f, 15.5f}})};
    case Icon::Pause:
        return {polygon(rounded_rect(6, 5, 10, 19, 1.2f)), polygon(rounded_rect(14, 5, 18, 19, 1.2f))};
    case Icon::Save:
    case Icon::Load: {
        Drawing d{path({{4, 15}, {4, 20}, {20, 20}, {20, 15}})};
        if (icon == Icon::Save) {
            d.push_back(path({{12, 3.5f}, {12, 14.5f}}));
            d.push_back(path({{7.5f, 10}, {12, 14.5f}, {16.5f, 10}}));
        } else {
            d.push_back(path({{12, 14.5f}, {12, 3.5f}}));
            d.push_back(path({{7.5f, 8}, {12, 3.5f}, {16.5f, 8}}));
        }
        return d;
    }
    case Icon::Reset: {
        // Counter-clockwise turn: the arrow sits where the circle opens.
        std::vector<Pt> turn;
        arc(turn, {12, 13}, 7.5f, 225, 225 + 300);
        const Pt tip = turn.front();
        return {path(turn), path({{tip.x, tip.y - 4.5f}, tip, {tip.x + 4.5f, tip.y}})};
    }
    case Icon::Fullscreen:
        return {path({{4, 9}, {4, 4}, {9, 4}}), path({{15, 4}, {20, 4}, {20, 9}}),
                path({{20, 15}, {20, 20}, {15, 20}}), path({{9, 20}, {4, 20}, {4, 15}})};
    case Icon::Exit:
        return {path({{11, 4}, {5.5f, 4}, {5.5f, 20}, {11, 20}}),
                path({{10, 12}, {20, 12}}), path({{16, 8}, {20, 12}, {16, 16}})};
    case Icon::Check:
        return {path({{5, 12.5f}, {10, 17.5f}, {19.5f, 7}})};
    case Icon::Lock: {
        std::vector<Pt> shackle{{8, 11}};
        arc(shackle, {12, 8}, 4, 180, 360);
        shackle.push_back({16, 11});
        return {path(rounded_rect(5, 11, 19, 20.5f, 2), true), path(shackle)};
    }
    case Icon::Gamepad:
        return {path(rounded_rect(2.5f, 7, 21.5f, 18, 5), true),
                path({{6, 12.5f}, {10, 12.5f}}), path({{8, 10.5f}, {8, 14.5f}}),
                disc({15.5f, 11.3f}, 1.25f), disc({18, 13.8f}, 1.25f)};
    case Icon::Keyboard:
        return {path(rounded_rect(2.5f, 6, 21.5f, 18, 2), true),
                disc({6.6f, 10}, 1.1f), disc({10.2f, 10}, 1.1f), disc({13.8f, 10}, 1.1f), disc({17.4f, 10}, 1.1f),
                path({{8, 14}, {16, 14}})};
    case Icon::Info:
        return {ring({12, 12}, 9), path({{12, 11}, {12, 16.5f}}), disc({12, 7.8f}, 1.3f)};
    case Icon::Trash:
        return {path({{4, 6.5f}, {20, 6.5f}}), path({{9, 6.5f}, {9, 4}, {15, 4}, {15, 6.5f}}),
                path({{6, 6.5f}, {7, 20}, {17, 20}, {18, 6.5f}}),
                path({{10, 10.5f}, {10, 16}}), path({{14, 10.5f}, {14, 16}})};
    case Icon::Volume: {
        // A speaker and two sound waves.
        std::vector<Pt> near, far;
        arc(near, {12, 12}, 4.5f, -45, 45);
        arc(far, {12, 12}, 8.f, -50, 50);
        return {path({{4, 9.5f}, {7.5f, 9.5f}, {12, 5.5f}, {12, 18.5f}, {7.5f, 14.5f}, {4, 14.5f}}, true),
                path(near), path(far)};
    }
    case Icon::Link: {
        // Two chain links holding each other, on the diagonal.
        const auto turned = [](std::vector<Pt> pts) {
            const float c = std::cos(-pi / 4), s = std::sin(-pi / 4);
            for (auto& p : pts) p = {12 + (p.x - 12) * c - (p.y - 12) * s, 12 + (p.x - 12) * s + (p.y - 12) * c};
            return pts;
        };
        return {path(turned(rounded_rect(2.5f, 8.5f, 13.5f, 15.5f, 3.5f)), true),
                path(turned(rounded_rect(10.5f, 8.5f, 21.5f, 15.5f, 3.5f)), true)};
    }
    }
    return {};
}

float segment_distance(Pt p, Pt a, Pt b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = dx * dx + dy * dy;
    const float t = len > 0.f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.f, 1.f) : 0.f;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}
// Distance to a polyline, signed (negative inside) for a filled polygon.
float polyline_distance(Pt p, const std::vector<Pt>& pts, bool closed, bool signed_inside) {
    float d = 1e9f;
    bool inside = false;
    const std::size_t n = pts.size();
    for (std::size_t i = 0; i + (closed ? 0 : 1) < n; ++i) {
        const Pt a = pts[i], b = pts[(i + 1) % n];
        d = std::min(d, segment_distance(p, a, b));
        if (signed_inside && ((a.y > p.y) != (b.y > p.y)) &&
            p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y))
            inside = !inside;
    }
    return inside ? -d : d;
}
} // namespace

std::vector<std::uint8_t> rasterize_icon(Icon icon, int pixels) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(pixels) * pixels, 0);
    if (pixels <= 0) return out;
    const Drawing shapes = drawing(icon);
    const float scale = pixels / 24.f; // pixels per grid unit
    // Strokes are two units wide, never thinner than 1.5 screen pixels.
    const float half = std::max(scale, 0.75f * oversample);
    for (int y = 0; y < pixels; ++y)
        for (int x = 0; x < pixels; ++x) {
            const Pt p{(x + .5f) / scale, (y + .5f) / scale};
            float coverage = 0.f;
            for (const Shape& s : shapes) {
                float edge = 0.f; // signed distance to the shape's edge, in pixels
                switch (s.kind) {
                case Shape::Path: edge = polyline_distance(p, s.pts, s.closed, false) * scale - half; break;
                case Shape::Ring: edge = std::abs(std::hypot(p.x - s.c.x, p.y - s.c.y) - s.r) * scale - half; break;
                case Shape::Polygon: edge = polyline_distance(p, s.pts, true, true) * scale; break;
                case Shape::Disc: edge = (std::hypot(p.x - s.c.x, p.y - s.c.y) - s.r) * scale; break;
                }
                coverage = std::max(coverage, std::clamp(.5f - edge, 0.f, 1.f));
            }
            out[static_cast<std::size_t>(y) * pixels + x] = static_cast<std::uint8_t>(coverage * 255.f + .5f);
        }
    return out;
}

namespace {
// Atlas rectangles of each icon and size, per ImGui context's atlas.
struct AtlasIcons {
    std::array<std::array<int, atlas_sizes.size()>, icon_count> rects{};
};
std::map<const ImFontAtlas*, AtlasIcons>& registry() {
    static std::map<const ImFontAtlas*, AtlasIcons> atlases;
    return atlases;
}
} // namespace

void register_icons(ImFontAtlas* atlas) {
    AtlasIcons icons;
    for (auto& sizes : icons.rects)
        for (std::size_t i = 0; i < atlas_sizes.size(); ++i)
            sizes[i] = atlas->AddCustomRectRegular(atlas_sizes[i] * oversample, atlas_sizes[i] * oversample);
    unsigned char* pixels = nullptr;
    int width = 0, height = 0;
    atlas->GetTexDataAsRGBA32(&pixels, &width, &height); // builds the atlas
    if (!pixels) return;
    // Shared by every atlas: the launcher and the game window rasterize once.
    static std::map<std::pair<int, int>, std::vector<std::uint8_t>> cache;
    for (int icon = 0; icon < icon_count; ++icon)
        for (std::size_t i = 0; i < atlas_sizes.size(); ++i) {
            const int size = atlas_sizes[i] * oversample;
            auto& alpha = cache[{icon, size}];
            if (alpha.empty()) alpha = rasterize_icon(static_cast<Icon>(icon), size);
            const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex(icons.rects[icon][i]);
            if (!rect || !rect->IsPacked()) return;
            for (int y = 0; y < size; ++y) {
                unsigned char* row = pixels + (static_cast<std::size_t>(rect->Y + y) * width + rect->X) * 4;
                for (int x = 0; x < size; ++x) {
                    row[x * 4 + 0] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
                    row[x * 4 + 3] = alpha[static_cast<std::size_t>(y) * size + x];
                }
            }
        }
    registry()[atlas] = icons;
}

void draw_icon(ImDrawList* d, Icon icon, ImVec2 center, float size, ImU32 color) {
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const auto found = registry().find(atlas);
    const int index = static_cast<int>(icon);
    if (found == registry().end() || index < 0 || index >= icon_count || size <= 0.f) return;
    std::size_t best = 0;
    for (std::size_t i = 1; i < atlas_sizes.size(); ++i)
        if (std::abs(atlas_sizes[i] - size) < std::abs(atlas_sizes[best] - size)) best = i;
    const ImFontAtlasCustomRect* rect = atlas->GetCustomRectByIndex(found->second.rects[index][best]);
    if (!rect || !rect->IsPacked()) return;
    ImVec2 uv0, uv1;
    atlas->CalcCustomRectUV(rect, &uv0, &uv1);
    // Whole pixels at the drawn size keep each icon pixel on a screen pixel.
    const float drawn = std::round(size);
    const ImVec2 min{std::round(center.x - drawn * .5f), std::round(center.y - drawn * .5f)};
    d->AddImage(atlas->TexID, min, {min.x + drawn, min.y + drawn}, uv0, uv1, color);
}
} // namespace ravenemudsk_ui

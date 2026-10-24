// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/image_loader.hpp>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
namespace fs = std::filesystem;
static void require(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
int main() {
    const auto folder = fs::temp_directory_path() /
        ("ravenemudsk-screenshot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    struct Cleanup { fs::path folder; ~Cleanup() { std::error_code ignored; fs::remove_all(folder, ignored); } } cleanup{folder};
    // A 3 x 2 picture of the core, ARGB8888: each pixel its own color.
    const std::array<std::uint32_t, 6> argb{0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFF123456, 0x00ABCDEF, 0xFFFFFFFF};
    const auto file = folder / "capture.png";
    require(ravenemudsk_ui::save_png(file, argb.data(), 3, 2), "The PNG is written");
    SDL_Surface* image = ravenemudsk_ui::load_local_surface(file);
    require(image && image->w == 3 && image->h == 2, "It reads back at the console's size");
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) {
            const auto* p = static_cast<const unsigned char*>(image->pixels) + y * image->pitch + x * 4;
            const auto expected = argb[static_cast<std::size_t>(y * 3 + x)];
            require(p[0] == ((expected >> 16) & 0xFF) && p[1] == ((expected >> 8) & 0xFF) && p[2] == (expected & 0xFF),
                    "Every pixel keeps its color");
            require(p[3] == 0xFF, "Opaque, whatever the core's alpha byte");
        }
    SDL_FreeSurface(image);
    require(!ravenemudsk_ui::save_png(folder / "none.png", nullptr, 3, 2), "No picture, no file");
    require(!ravenemudsk_ui::save_png(folder / "empty.png", argb.data(), 0, 2), "No size, no file");
    require(!ravenemudsk_ui::save_png(folder / "missing" / "x.png", argb.data(), 3, 2), "An unwritable path fails");
    std::cout << "Screenshot PNG passed\n";
}

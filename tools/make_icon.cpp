// SPDX-License-Identifier: AGPL-3.0-only
// Build-time tool: turns the provided RavenEmu logo (PNG) into a Windows icon
// holding the usual sizes, each stored as PNG (supported since Windows Vista).
// Usage: ravenemudsk_make_icon logo.png out.ico
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <stb_image_write.h>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void append(std::vector<unsigned char>& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<unsigned char>(value >> (8 * i)));
}
void collect(void* context, void* data, int size) {
    auto& out = *static_cast<std::vector<unsigned char>*>(context);
    const auto* bytes = static_cast<const unsigned char*>(data);
    out.insert(out.end(), bytes, bytes + size);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: ravenemudsk_make_icon logo.png out.ico\n";
        return 2;
    }
    int width = 0, height = 0, channels = 0;
    unsigned char* logo = stbi_load(argv[1], &width, &height, &channels, 4);
    if (!logo || width <= 0 || height <= 0) {
        std::cerr << "Cannot read " << argv[1] << '\n';
        return 1;
    }
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    std::vector<std::vector<unsigned char>> images;
    for (const int size : sizes) {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * size * 4);
        // Premultiplied resampling keeps the transparent corners clean.
        if (!stbir_resize_uint8_srgb(logo, width, height, 0, pixels.data(), size, size, 0, STBIR_RGBA)) {
            std::cerr << "Cannot resize the logo\n";
            stbi_image_free(logo);
            return 1;
        }
        std::vector<unsigned char> png;
        if (!stbi_write_png_to_func(collect, &png, size, size, 4, pixels.data(), size * 4)) {
            std::cerr << "Cannot encode the icon\n";
            stbi_image_free(logo);
            return 1;
        }
        images.push_back(std::move(png));
    }
    stbi_image_free(logo);

    // ICONDIR, then one ICONDIRENTRY per image, then the PNG data.
    std::vector<unsigned char> ico;
    append(ico, 0, 2);
    append(ico, 1, 2);
    append(ico, static_cast<std::uint32_t>(images.size()), 2);
    std::uint32_t offset = 6 + 16 * static_cast<std::uint32_t>(images.size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        const int size = sizes[i];
        append(ico, size >= 256 ? 0 : static_cast<std::uint32_t>(size), 1); // 0 means 256
        append(ico, size >= 256 ? 0 : static_cast<std::uint32_t>(size), 1);
        append(ico, 0, 1);  // no palette
        append(ico, 0, 1);  // reserved
        append(ico, 1, 2);  // planes
        append(ico, 32, 2); // bits per pixel
        append(ico, static_cast<std::uint32_t>(images[i].size()), 4);
        append(ico, offset, 4);
        offset += static_cast<std::uint32_t>(images[i].size());
    }
    for (const auto& image : images) ico.insert(ico.end(), image.begin(), image.end());
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(ico.data()), static_cast<std::streamsize>(ico.size()));
    if (!out) {
        std::cerr << "Cannot write " << argv[2] << '\n';
        return 1;
    }
    return 0;
}

// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/storage.hpp>
#include <ravenemudsk/ui/library.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
namespace ravenemudsk_ui {
namespace fs = std::filesystem;
namespace {
constexpr std::array<const char*, 4> image_extensions{".png", ".jpg", ".jpeg", ".bmp"};
bool writable(const fs::path& folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec || !fs::is_directory(folder, ec)) return false;
    const auto probe = folder / ".ravenemudsk-write-test";
    { std::ofstream out(probe, std::ios::binary | std::ios::trunc); if (!(out << 'x')) return false; }
    fs::remove(probe, ec);
    return true;
}
std::string lower_extension(const fs::path& file) {
    auto ext = utf8(file.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}
} // namespace

Folders prepare_folders(const std::vector<fs::path>& roots) {
    for (const auto& root : roots) {
        if (root.empty()) continue;
        Folders folders{root / "Sauvegardes", root / "Jaquettes"};
        if (writable(folders.saves) && writable(folders.covers)) return folders;
    }
    throw std::runtime_error("Impossible de créer les dossiers Sauvegardes et Jaquettes");
}

fs::path save_path(const Folders& folders, const fs::path& rom, const char* suffix) {
    const auto target = sidecar(folders.saves / rom.filename(), suffix);
    const auto legacy = sidecar(rom, suffix);
    std::error_code ec;
    if (fs::exists(target, ec) || !fs::is_regular_file(legacy, ec)) return target;
    // Keep the original next to the ROM as a backup; never move it.
    fs::copy_file(legacy, target, fs::copy_options::skip_existing, ec);
    return ec ? legacy : target;
}

std::vector<fs::path> cover_candidates(const Folders& folders, const fs::path& rom) {
    std::vector<fs::path> paths;
    const auto stem = rom.stem();
    for (const auto& base : {folders.covers, rom.parent_path()})
        for (const char* ext : image_extensions) {
            paths.push_back(sidecar(base / rom.filename(), ext));
            paths.push_back(sidecar(base / stem, ext));
        }
    return paths;
}

bool is_image(const fs::path& file) {
    const auto ext = lower_extension(file);
    return std::any_of(image_extensions.begin(), image_extensions.end(),
                       [&](const char* e) { return ext == e; });
}

fs::path install_cover(const Folders& folders, const fs::path& rom, const fs::path& image) {
    if (!is_image(image)) throw std::invalid_argument("Image PNG, JPEG ou BMP attendue");
    std::error_code ec;
    if (!fs::is_regular_file(image, ec)) throw std::invalid_argument("Image introuvable");
    const auto target = sidecar(folders.covers / rom.stem(), lower_extension(image).c_str());
    if (fs::exists(target, ec) && fs::equivalent(image, target, ec)) return target;
    // Copy first (the image may itself be an older cover), then keep a
    // single cover per game by dropping the other variants.
    fs::copy_file(image, target, fs::copy_options::overwrite_existing);
    for (const char* ext : image_extensions) {
        for (const auto& old : {sidecar(folders.covers / rom.filename(), ext),
                                sidecar(folders.covers / rom.stem(), ext)})
            if (old != target) fs::remove(old, ec);
    }
    return target;
}
} // namespace ravenemudsk_ui

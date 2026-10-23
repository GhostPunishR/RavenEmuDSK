// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/library/storage.hpp>
#include <ravenemudsk/library/library.hpp>
#include <ravenemu/sha256.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <iterator>
namespace ravenemudsk_ui {
namespace fs = std::filesystem;
namespace {
constexpr std::array<const char*, 4> image_extensions{".png", ".jpg", ".jpeg", ".bmp"};
std::string path_id(const std::string& value) {
    const auto hash = ravenemu::detail::sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
    constexpr char hex[] = "0123456789abcdef";
    std::string id;
    for (const auto byte : hash) {
        id += hex[byte >> 4];
        id += hex[byte & 15];
    }
    return id;
}
bool owns_legacy(const Folders& folders, const fs::path& rom, const std::string& identity) {
    const auto registry = folders.saves / ".legacy-owners";
    fs::create_directories(registry);
    const auto claim = registry / path_id(utf8(rom.filename()));
    const auto owner = claim / "rom-path.txt";
    // Directory creation claims the old basename atomically across processes.
    if (fs::create_directory(claim)) {
        std::ofstream out(owner, std::ios::binary);
        out << identity;
        out.close();
        if (!out) throw std::runtime_error("Impossible d'attribuer l'ancienne sauvegarde");
    }
    std::ifstream in(owner, std::ios::binary);
    if (!in) throw std::runtime_error("Attribution de l'ancienne sauvegarde illisible");
    const std::string recorded{std::istreambuf_iterator<char>(in), {}};
    if (recorded.empty()) throw std::runtime_error("Attribution de l'ancienne sauvegarde incomplète");
    return recorded == identity;
}
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
    const auto normalized = fs::weakly_canonical(fs::absolute(rom));
    const auto identity = utf8(normalized);
    const auto directory = folders.saves / "roms" / path_id(identity);
    fs::create_directories(directory);
    const auto target = sidecar(directory / normalized.filename(), suffix);
    if (fs::exists(target)) return target;
    const auto legacy = sidecar(rom, suffix);
    const auto shared = sidecar(folders.saves / rom.filename(), suffix);
    if (fs::is_regular_file(legacy)) {
        fs::copy_file(legacy, target, fs::copy_options::skip_existing);
    } else if (fs::is_regular_file(shared) && owns_legacy(folders, rom, identity)) {
        fs::copy_file(shared, target, fs::copy_options::skip_existing);
    }
    return target;
}

fs::path preview_path(const Folders& folders, const fs::path& rom) {
    const auto target = save_path(folders, rom, ".ravenemu-preview.bmp");
    if (!fs::exists(target)) {
        const auto legacy = save_path(folders, rom, ".ravenemudsk-preview.bmp");
        if (fs::is_regular_file(legacy))
            fs::copy_file(legacy, target, fs::copy_options::skip_existing);
    }
    return target;
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

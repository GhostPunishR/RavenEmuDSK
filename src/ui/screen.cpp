// SPDX-License-Identifier: AGPL-3.0-only
#include <ravenemudsk/ui/screen.hpp>
#include <ravenemudsk/settings/settings.hpp>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#endif
namespace ravenemudsk_ui {
std::vector<std::string> display_names() {
    std::vector<std::string> names;
    const int count = SDL_GetNumVideoDisplays();
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetDisplayName(i);
        names.push_back(name && *name ? name : "Écran " + std::to_string(i + 1));
    }
    return names;
}

int chosen_display(const Settings& settings) {
    const int display = settings.number("display", 0);
    return display >= 0 && display < SDL_GetNumVideoDisplays() ? display : 0;
}

void move_to_display(SDL_Window* window, int display) {
    if (!window || SDL_GetWindowDisplayIndex(window) == display) return;
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

namespace {
std::string label_of(std::string_view name) {
    if (name == "direct3d") return "Direct3D 9";
    if (name == "direct3d11") return "Direct3D 11";
    if (name == "direct3d12") return "Direct3D 12";
    if (name == "opengl") return "OpenGL";
    if (name == "opengles2") return "OpenGL ES 2";
    if (name == "opengles") return "OpenGL ES";
    if (name == "metal") return "Metal";
    if (name == "software") return "Logiciel (sans carte graphique)";
    return std::string(name);
}
} // namespace

std::vector<RenderDriver> render_drivers() {
    const auto automatic = automatic_render_driver();
    std::vector<RenderDriver> drivers{{"auto", automatic.empty() ? "Automatique" : "Automatique (" + label_of(automatic) + ")"}};
    const int count = SDL_GetNumRenderDrivers();
    for (int i = 0; i < count; ++i) {
        SDL_RendererInfo info{};
        if (SDL_GetRenderDriverInfo(i, &info) == 0 && info.name) drivers.push_back({info.name, label_of(info.name)});
    }
    return drivers;
}

std::string automatic_render_driver() {
#ifdef _WIN32
    return "direct3d11";
#else
    return {};
#endif
}

void choose_render_driver(const Settings& settings) {
    auto driver = settings.get("render_driver", "auto");
    if (driver == "auto") driver = automatic_render_driver();
    if (driver.empty()) return;
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, driver.c_str());
    // Naming an engine turns SDL 2's batching off, as for a program drawing
    // with the engine itself: RavenEmu draws only through SDL.
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
}

void limit_frame_queue([[maybe_unused]] SDL_Renderer* renderer) {
#ifdef _WIN32
    SDL_RendererInfo info{};
    if (!renderer || SDL_GetRendererInfo(renderer, &info) != 0 || std::string_view(info.name) != "direct3d11") return;
    ID3D11Device* device = SDL_RenderGetD3D11Device(renderer);
    if (!device) return;
    IDXGIDevice1* dxgi = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgi))) && dxgi) {
        dxgi->SetMaximumFrameLatency(1);
        dxgi->Release();
    }
    // SDL hands the device with one more reference.
    device->Release();
#endif
}

std::vector<std::string> audio_outputs() {
    std::vector<std::string> outputs;
    const int count = SDL_GetNumAudioDevices(0);
    for (int i = 0; i < count; ++i)
        if (const char* name = SDL_GetAudioDeviceName(i, 0)) outputs.emplace_back(name);
    return outputs;
}
} // namespace ravenemudsk_ui

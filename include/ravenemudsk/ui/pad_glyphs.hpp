// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <imgui.h>
#include <SDL.h>
#include <optional>
#include <string>
namespace ravenemudsk_ui {
// Controller buttons as each family prints them, so that hints read like the
// controller in hand. SDL reports buttons by position (South is SDL "A");
// with its default label hint, Nintendo controllers are remapped by label.
enum class PadFamily { Xbox, PlayStation, PlayStation5, Nintendo, Generic };
enum class PadButton { South, East, West, North, LeftShoulder, RightShoulder, Start, Back };
PadFamily pad_family(SDL_GameControllerType type);
// Family of the first connected controller; nullopt when none is connected.
std::optional<PadFamily> connected_pad();
// Printed name: "A", "Croix", "LB", "L1", "Menu", "Options", "+"...
const char* pad_button_name(PadFamily family, PadButton button);
// Draws the button as the controller shows it, `height` pixels tall, and
// returns its width; with no draw list it only measures.
float pad_glyph(ImDrawList* d, ImVec2 pos, PadFamily family, PadButton button, ImFont* font, float height,
                float alpha = 1.f);
// Back and Start pressed together open the in-game menu: the Guide button
// is kept for the system (Xbox Game Bar, Steam), which would open as well.
float pad_menu_combo(ImDrawList* d, ImVec2 pos, PadFamily family, ImFont* font, float height, float alpha = 1.f);
// A bound controller input (see PadBindings) as the controller prints it:
// "A", "Croix", "LT", "L2", "ZL", "Croix haut"... "Non attribué" when unbound.
std::string pad_input_name(int input, PadFamily family);
} // namespace ravenemudsk_ui

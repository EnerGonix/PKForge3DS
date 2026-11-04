// PKForge's chrome on the 3DS screens: header bars, key-hint footers, buttons, rows, chips.
#pragma once

#include "gfx.hpp"
#include "input.hpp"
#include <initializer_list>
#include <string>
#include <utility>

namespace widgets
{
    constexpr int HEADER_H = 20;
    constexpr int FOOTER_Y = 216;

    // The dark title strip: title centred, status at the left, battery at the right.
    void header(int width, const std::string& title, const std::string& status = "");
    // The original's two top strips: the black "PKForge" title bar, then "<context> • <state>" with the battery.
    constexpr int TITLE_H = 27;
    void titleBar(int width, const std::string& context, const std::string& state);
    // Key hints in the footer panel, centred: {"A", "Open"}, {"B", "Back"}...
    void footer(int width, std::initializer_list<std::pair<const char*, const char*>> hints);
    void battery(float x, float y);

    // A PKForge button: navy body, cobalt rim, cyan when focused. True when tapped.
    bool button(Input& in, int x, int y, int w, int h, const std::string& label, bool focused = false,
        int icon = -1, bool enabled = true);
    void buttonFace(int x, int y, int w, int h, const std::string& label, bool focused, bool pressed, int icon = -1,
        bool enabled = true);
    // A small square arrow button (box headers, pagers).
    bool arrow(Input& in, int x, int y, int w, int h, bool left);

    // An editor row: label column and value, focused rows get the chip gradient.
    void row(int x, int y, int w, int h, const std::string& label, const std::string& value, bool focused,
        bool chevron = true, u32 valueColor = gfx::T::Value);
    // A section strip like PKForge's summary headers ("Trainer", "Stats").
    void section(int x, int y, int w, const std::string& title, u32 color);
    void typeChip(int x, int y, int w, int type);
    void chip(int x, int y, const std::string& label, u32 fill, u32 ink);
    // A vertical scrollbar for lists.
    void scrollbar(int x, int y, int h, int first, int visible, int total);
    // The red corner brackets of the storage cursor.
    void cursor(int x, int y, int w, int h, u32 color);
    // Dims the whole current screen (behind overlays).
    void dim(int width);
}

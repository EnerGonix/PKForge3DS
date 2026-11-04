#include "widgets.hpp"
#include "app.hpp"
#include "pkdata.hpp"

#include <3ds.h>
#include <algorithm>

using namespace gfx;

namespace widgets
{
    void header(int width, const std::string& title, const std::string& status)
    {
        rect(0, 0, width, HEADER_H, T::Void);
        rect(0, HEADER_H - 1, width, 1, T::Grid);
        text(width / 2, 4, title, T::Ink, Center);
        if (!status.empty())
        {
            text(6, 4, status, T::InkSoft, Left, 1, width / 2 - 50);
        }
        battery(width - 24, 6);
    }

    void titleBar(int width, const std::string& context, const std::string& state)
    {
        rect(0, 0, width, 14, rgb(0x10, 0x10, 0x18));
        rect(0, 13, width, 1, T::Grid);
        text(width / 2, 1, "PKForge", T::Ink, Center);
        rect(0, 14, width, 13, T::Deep);
        float x = 6 + text(6, 15, context, T::Ink, Left, 1, width - 130);
        if (!state.empty())
        {
            rect(x + 5, 20, 3, 3, T::InkSoft);
            text(x + 13, 15, state, T::InkSoft, Left, 1, width - x - 50);
        }
        battery(width - 24, 17);
    }

    void battery(float x, float y)
    {
        u8 level = 5;
        PTMU_GetBatteryLevel(&level);
        u8 charging = 0;
        PTMU_GetBatteryChargeState(&charging);
        outline(x, y, 16, 8, T::InkSoft);
        rect(x + 16, y + 2, 2, 4, T::InkSoft);
        u32 fill = charging ? S::Legal : level <= 1 ? S::Illegal : S::Legal;
        rect(x + 2, y + 2, std::min<int>(level, 5) * 12 / 5, 4, fill);
    }

    void footer(int width, std::initializer_list<std::pair<const char*, const char*>> hints)
    {
        panel(2, FOOTER_Y, width - 4, 240 - FOOTER_Y - 1, T::Deck, T::FrameEdge);
        float total = 0;
        for (auto& [key, label] : hints)
        {
            total += keyDiscWidth(key) + 3 + textWidth(label) + 9;
        }
        float x = std::max(6.0f, (width - total + 9) / 2);
        for (auto& [key, label] : hints)
        {
            keyDisc(x, FOOTER_Y + 5, key);
            x += keyDiscWidth(key) + 3;
            x += text(x, FOOTER_Y + 6, label, T::Ink) + 9;
        }
    }

    void buttonFace(int x, int y, int w, int h, const std::string& label, bool focused, bool pressed, int icon,
        bool enabled)
    {
        u32 fill = pressed ? T::Deep : focused ? T::Grid : T::Deck;
        u32 rim  = focused ? T::Cyan : T::FrameEdge;
        panel(x, y, w, h, fill, rim);
        u32 ink = enabled ? T::Ink : T::SubInk;
        if (icon >= 0)
        {
            C2D_Image img = ui(icon);
            int iw = img.subtex->width, ih = img.subtex->height;
            if (h >= ih + 18)
            {
                // Icon above the label.
                image(icon, x + (w - iw) / 2, y + (h - ih - 14) / 2);
                text(x + w / 2, y + h - 15, label, ink, Center, 1, w - 6);
                return;
            }
            float tw = textWidth(label);
            float lx = x + (w - (iw + 4 + tw)) / 2;
            image(icon, lx, y + (h - ih) / 2);
            text(lx + iw + 4, y + (h - 12) / 2, label, ink, Left, 1, w - iw - 10);
            return;
        }
        text(x + w / 2, y + (h - 12) / 2, label, ink, Center, 1, w - 6);
    }

    bool button(Input& in, int x, int y, int w, int h, const std::string& label, bool focused, int icon, bool enabled)
    {
        bool pressed = in.touching && in.inside(x, y, w, h) && x <= in.startX && in.startX < x + w &&
                       y <= in.startY && in.startY < y + h;
        buttonFace(x, y, w, h, label, focused, pressed && enabled, icon, enabled);
        return enabled && in.tapped(x, y, w, h);
    }

    bool arrow(Input& in, int x, int y, int w, int h, bool left)
    {
        bool pressed = in.touching && in.inside(x, y, w, h);
        panel(x, y, w, h, pressed ? T::Deep : T::Frame, T::FrameEdge);
        chevron(x + (w - 4) / 2, y + (h - 8) / 2, T::Ink, left);
        return in.tapped(x, y, w, h);
    }

    void row(int x, int y, int w, int h, const std::string& label, const std::string& value, bool focused, bool chev,
        u32 valueColor)
    {
        if (focused)
        {
            panelGrad(x, y, w, h - 1, T::PartySelected, T::ChipTop, T::Cyan);
        }
        else
        {
            u32 fill = chev ? mix(T::CardRow, T::Grid, 0.22f) : T::CardFill;
            panel(x, y, w, h - 2, fill, fill);
        }
        int ty = y + (h - 13) / 2;
        text(x + 8, ty, label, focused ? T::ChipInk : T::InkSoft, Left, 1, 82);
        u32 ink = focused ? T::Bright : chev ? (valueColor == T::Value ? T::Ink : valueColor) : T::SubInk;
        text(x + 94, ty, value, ink, Left, 1, w - 94 - (chev ? 16 : 8));
        if (chev)
        {
            chevron(x + w - 10, y + (h - 9) / 2, focused ? T::Bright : T::SubInk, false);
        }
    }

    void section(int x, int y, int w, const std::string& title, u32 color)
    {
        panelGrad(x, y, w, 13, mix(color, T::Bright, 0.18f), color, mix(color, T::Void, 0.45f));
        text(x + 5, y + 1, title, T::Bright);
    }

    void typeChip(int x, int y, int w, int type)
    {
        u32 fill = typeColor(type);
        panel(x, y, w, 12, fill, mix(fill, T::Void, 0.4f));
        std::string name = pkdata::type(type);
        text(x + w / 2, y, name, inkOn(fill), Center, 1, w - 4);
    }

    void chip(int x, int y, const std::string& label, u32 fill, u32 ink)
    {
        int w = int(textWidth(label)) + 8;
        panel(x, y, w, 12, fill, mix(fill, T::Void, 0.4f));
        text(x + 4, y, label, ink);
    }

    void scrollbar(int x, int y, int h, int first, int visible, int total)
    {
        if (total <= visible)
        {
            return;
        }
        rect(x, y, 3, h, T::Band);
        int knob = std::max(10, h * visible / total);
        int pos  = (h - knob) * first / std::max(1, total - visible);
        rect(x, y + pos, 3, knob, T::Cyan);
    }

    void cursor(int x, int y, int w, int h, u32 c)
    {
        const int l = 5;
        rect(x, y, l, 2, c);
        rect(x, y, 2, l, c);
        rect(x + w - l, y, l, 2, c);
        rect(x + w - 2, y, 2, l, c);
        rect(x, y + h - 2, l, 2, c);
        rect(x, y + h - l, 2, l, c);
        rect(x + w - l, y + h - 2, l, 2, c);
        rect(x + w - 2, y + h - l, 2, l, c);
    }

    void dim(int width) { rect(0, 0, width, 240, alpha(T::Void, 170)); }
}

// About: what this is, who made what, and under which licence.
#include "gfx.hpp"
#include "screens.hpp"
#include "ui_ids.hpp"
#include "version.hpp"
#include "widgets.hpp"

#include <algorithm>

using namespace gfx;

namespace
{
    const char* CREDITS[] = {
        "En3rGonix (github.com/EnerGonix): PKForge for Nintendo 3DS and R4 PokéLink, the R4 save link. "
        "Source: github.com/EnerGonix/PKForge3DS",
        "PKForge by sofianeelhor, GPLv3 or later. This is a native 3DS port: PKForge's screens and Bank "
        "rebuilt in C++ for the 3DS.",
        "PKSM-Core by FlagBrew (Bernardo Giordano, Admiral Fish, piepie62 and contributors), GPLv3 with "
        "Additional Terms 7.b and 7.c: the save engine. Modified only through its build configuration.",
        "PKHeX by Kaphotics and contributors: the research PKSM-Core translates.",
        "memecrypto by SciresM; pcg-cpp by Melissa O'Neill (MIT / Apache-2.0).",
        "PKSM's pixel chrome (GPLv3): the icons and the storage look PKForge builds on.",
        "spritedmistery: PKForge's logo, look and color schemes.",
        "Pokémon Showdown / Smogon and its sprite artists: front sprites and box icons.",
        "PokeAPI (BSD-3): move, ability and Pokédex facts.",
        "game-icons.net (CC-BY 3.0): Lorc, Delapouite and contributors.",
        "SteamGridDB community artists: cartridge art.",
        "NDS12 font by Caveras (CC BY-NC-SA 3.0); its licence and readme are in romfs:/licenses.",
        "Pokémon and Pokémon character names are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc. "
        "Not affiliated with Nintendo, Game Freak or The Pokémon Company.",
        "Source code: github.com/EnerGonix/PKForge3DS, GPLv3.",
    };
    constexpr int COUNT = sizeof(CREDITS) / sizeof(CREDITS[0]);

    class About : public Screen
    {
    public:
        void update(Input& in) override
        {
            if (in.repeat & KEY_DDOWN)
            {
                first = std::min(COUNT - 1, first + 1);
            }
            if (in.repeat & KEY_DUP)
            {
                first = std::max(0, first - 1);
            }
            if (in.touching && in.dragged)
            {
                drag += in.dy;
                if (drag < -24 && first < COUNT - 1)
                {
                    first++;
                    drag = 0;
                }
                if (drag > 24 && first > 0)
                {
                    first--;
                    drag = 0;
                }
            }
            if (in.down & (KEY_B | KEY_A))
            {
                app::pop();
            }
        }

        void drawTop() override
        {
            housing(0, 0, TOP_W, H);
            image(ui::logo, 24, 40);
            text(130, 56, "PKForge", T::Ink, Left, 2);
            text(130, 86, "for Nintendo 3DS  -  v" PORT_VERSION "  (PKForge " PKFORGE_BASE ")", T::Value);
            text(130, 100, "3DS port and R4 PokéLink by En3rGonix", S::ShinyGold);
            wrap(130, 120, 250,
                "A Pokémon save editor and Bank. Saves are found anywhere on the SD card; every write keeps a "
                "restore point in sdmc:/3ds/PKForge/backups.",
                T::InkSoft);
            text(TOP_W / 2, 220, "Not affiliated with Nintendo, Game Freak or The Pokémon Company.", T::SubInk, Center);
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, "About");
            panel(6, 24, BOTTOM_W - 12, 188, T::PanelFill, T::FrameEdge);
            clip(7, 25, BOTTOM_W - 14, 186);
            float y = 30;
            for (int i = first; i < COUNT && y < 210; i++)
            {
                y += wrap(14, y, BOTTOM_W - 30, CREDITS[i], i % 2 ? T::Value : T::Ink) + 6;
            }
            unclip();
            widgets::scrollbar(BOTTOM_W - 10, 28, 180, first, 4, COUNT);
            widgets::footer(BOTTOM_W, {{"B", "Back"}});
        }

    private:
        int first = 0, drag = 0;
    };
}

namespace screens
{
    std::unique_ptr<Screen> about() { return std::make_unique<About>(); }
}

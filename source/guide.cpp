#include "guide.hpp"
#include "audio.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"

using namespace gfx;

namespace guide
{
    namespace
    {
        constexpr const char* SEEN = "sdmc:/3ds/PKForge/guide-seen.txt";

        struct Page
        {
            const char* title;
            const char* text;
        };

        const Page PAGES[] = {
            {"Welcome to PKForge 3DS",
                "Edit your Pokémon saves, move Pokémon between games and keep them in a Bank. These pages show "
                "the basics; Settings > Quick guide shows them again."},
            {"Home",
                "Every save PKForge finds is a card: the game card, installed games, Virtual Console and saves "
                "anywhere on the SD card. A opens one, L filters by system, B says which game a shared save is. "
                "The tiles open the Bank, Dex Autopilot, Poképark, Events and Settings."},
            {"Boxes",
                "A grabs a Pokémon and A again drops it, or drag it with the stylus. L/R change box, SELECT "
                "switches between the save and the Bank. X opens the chosen Pokémon's tools (edit, RNG, QR, "
                "multi-select...), START the save's menu (search, box manager, bag...)."},
            {"The top screen",
                "It shows the chosen Pokémon: Info, Stats, Moves, Origin and Legal. ZL/ZR turn the pages on a "
                "New 3DS; in the editor the page follows the section you are in."},
            {"Keeping your changes",
                "Changes stay in PKForge until you write the save (Y, Save data). Every write keeps a restore "
                "point first, in 3ds/PKForge/backups, and moves to the Bank become final then too."},
            {"R4 PokéLink",
                "Playing on an R4 card? The R4 button starts R4 PokéLink: it copies the R4's Pokémon saves to the "
                "3DS for PKForge to edit, and writes them back to the R4, with a backup first."},
            {"Settings",
                "Sound and music, color themes, Hardcore mode for challenge runs, the legality server, and "
                "updates: new versions come from GitHub and install from Settings. Have fun!"},
        };
        constexpr int COUNT = sizeof(PAGES) / sizeof(PAGES[0]);

        class Guide : public Screen
        {
        public:
            void update(Input& in) override
            {
                if ((in.down & KEY_A) || (in.repeat & KEY_DRIGHT) || in.tapped(BOTTOM_W - 92, 186, 84, 24))
                {
                    if (page + 1 < COUNT)
                    {
                        page++;
                        audio::sfx(audio::Sfx::Blip);
                    }
                    else if (in.down & KEY_A || in.touchUp)
                    {
                        finish();
                    }
                    return;
                }
                if (((in.repeat & KEY_DLEFT) || in.tapped(8, 186, 84, 24)) && page > 0)
                {
                    page--;
                    audio::sfx(audio::Sfx::Blip);
                    return;
                }
                if (in.down & KEY_B)
                {
                    finish();
                }
            }

            void drawTop() override
            {
                housing(0, 0, TOP_W, H);
                image(ui::logo, TOP_W / 2 - 42, 40);
                text(TOP_W / 2, 152, "Quick guide", T::Ink, Center, 2);
                char line[16];
                snprintf(line, sizeof(line), "%d / %d", page + 1, COUNT);
                text(TOP_W / 2, 186, line, T::InkSoft, Center);
            }

            void drawBottom() override
            {
                housing(0, 0, BOTTOM_W, H);
                widgets::titleBar(BOTTOM_W, "PKForge", "Quick guide");
                panel(6, 32, BOTTOM_W - 12, 146, T::PanelFill, T::FrameEdge);
                panelGrad(12, 38, BOTTOM_W - 24, 18, T::PartySelected, T::ChipTop, T::PartyRim);
                text(20, 41, PAGES[page].title, T::Bright);
                wrap(18, 64, BOTTOM_W - 36, PAGES[page].text, T::Ink, 1, 8);
                // Page dots.
                for (int i = 0; i < COUNT; i++)
                {
                    circle(BOTTOM_W / 2 - (COUNT - 1) * 6 + i * 12, 170, i == page ? 3.5f : 2.5f, i == page ? T::Cyan : T::SubInk);
                }
                if (page > 0)
                {
                    widgets::buttonFace(8, 186, 84, 24, "Back", false, false);
                }
                widgets::buttonFace(BOTTOM_W - 92, 186, 84, 24, page + 1 < COUNT ? "Next" : "Done", true, false);
                widgets::footer(BOTTOM_W, {{"A", page + 1 < COUNT ? "Next" : "Done"}, {"B", "Skip"}});
            }

        private:
            int page = 0;

            void finish()
            {
                files::writeText(SEEN, "1");
                audio::sfx(audio::Sfx::Open);
                app::pop();
            }
        };
    }

    bool seen() { return files::exists(SEEN); }

    std::unique_ptr<Screen> open() { return std::make_unique<Guide>(); }
}

// Home, as PKForge's HomePage: the title bar and status strip, "Games" with the filter chip over the saves found
// as cartridge cards, the Bank, Dex Autopilot, Poképark, Events and Settings tiles below. Y (Link) starts
// R4 PokéLink.
#include "files.hpp"
#include "hax.hpp"
#include "legality_online.hpp"
#include "autopilot.hpp"
#include "livingdex.hpp"
#include "guide.hpp"
#include "updater.hpp"
#include "version.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "r4link.hpp"
#include "saves.hpp"
#include "titles.hpp"
#include "screens.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <map>
#include <cstdio>

using namespace gfx;

namespace
{
    constexpr int CARD_Y = 46, CARD_W = 58, CARD_H = 100, CARD_GAP = 3; // five in view, as the original
    constexpr int BUTTON_Y = 152, BUTTON_W = 60, BUTTON_H = 58, BUTTONS = 5;

    // L cycles the filter, as the original's "Filter" chip.
    const char* FILTERS[] = {"All", "Game Boy", "GBA", "DS", "3DS"};
    bool passes(const SaveRef& s, int filter)
    {
        switch (filter)
        {
            case 1: return s.gen <= 2;
            case 2: return s.gen == 3;
            case 3: return s.gen == 4 || s.gen == 5;
            case 4: return s.gen == 6 || s.gen == 7;
            default: return true;
        }
    }

    // "Edit game", as the original's: a save two games share (Ruby / Sapphire, FireRed / LeafGreen) can say which
    // one it is, for its card's name and art. Kept in sdmc:/3ds/PKForge/editions.txt, "<path>\t<name>\t<art>".
    struct Edition
    {
        std::string name, art;
    };
    constexpr const char* EDITIONS = "sdmc:/3ds/PKForge/editions.txt";

    std::map<std::string, Edition>& editions()
    {
        static std::map<std::string, Edition> map;
        static bool loaded = false;
        if (!loaded)
        {
            loaded = true;
            std::string text;
            files::readText(EDITIONS, text);
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? text.size() : end + 1;
                size_t a = line.find('\t'), b = a == std::string::npos ? a : line.find('\t', a + 1);
                if (b != std::string::npos)
                {
                    map[line.substr(0, a)] = {line.substr(a + 1, b - a - 1), line.substr(b + 1)};
                }
            }
        }
        return map;
    }

    void saveEditions()
    {
        std::string text;
        for (auto& [path, e] : editions())
        {
            text += path + "\t" + e.name + "\t" + e.art + "\n";
        }
        files::writeText(EDITIONS, text);
    }

    std::string gameName(const SaveRef& s)
    {
        auto it = editions().find(s.path);
        return it != editions().end() ? it->second.name : pkdata::game(s.version);
    }

    std::string gameArtOf(const SaveRef& s)
    {
        auto it = editions().find(s.path);
        return it != editions().end() ? it->second.art : pkdata::gameArt(s.version);
    }

    // The tiles' pixel icons, drawn as the original's (white on the tile).
    void dexIcon(float cx, float cy)
    {
        rect(cx - 12, cy - 9, 24, 18, T::Bright);
        rect(cx - 9, cy - 6, 18, 7, T::Deck);
        for (int i = 0; i < 3; i++)
        {
            rect(cx - 8 + i * 7, cy + 4, 3, 2, T::Deck);
        }
    }

    void pawIcon(float cx, float cy)
    {
        circle(cx, cy + 4, 6, T::Bright);
        circle(cx - 8, cy - 3, 3, T::Bright);
        circle(cx - 3, cy - 8, 3, T::Bright);
        circle(cx + 3, cy - 8, 3, T::Bright);
        circle(cx + 8, cy - 3, 3, T::Bright);
    }

    void eventsIcon(float cx, float cy)
    {
        // A Wonder Card: a white card with a Poké Ball window.
        panel(cx - 14, cy - 9, 28, 18, T::Bright, T::Bright);
        tri(cx - 14, cy + 9, cx - 3, cy - 9, cx - 14, cy - 9, T::Deck);
        circle(cx + 4, cy, 6, T::Deck);
        circle(cx + 4, cy, 4.5f, T::Bright);
        rect(cx - 2, cy - 0.5f, 12, 1, T::Deck);
        circle(cx + 4, cy, 1.5f, T::Deck);
    }

    // The cartridge's shell by system, as the original's cards: Game Boy grey, GBA violet, DS and 3DS slate.
    u32 shellColor(int gen)
    {
        return gen <= 2 ? rgb(0x9A, 0xA0, 0xAC) : gen == 3 ? rgb(0x8A, 0x7C, 0xE0) : gen <= 5 ? rgb(0x5C, 0x64, 0x78)
                                                                                                : rgb(0x44, 0x4A, 0x5E);
    }

    class Home : public Screen
    {
    public:
        Home() { saves::startScan(); }

        void update(Input& in) override
        {
            std::string scripted;
            if (autotest::takeOpen(scripted))
            {
                SaveRef ref;
                u64 id;
                u8 media;
                bool known = titles::parse(scripted, id, media) ? titles::describe(id, media, ref)
                                                                 : saves::describe(scripted, ref);
                if (known)
                {
                    openRef(ref);
                }
                else
                {
                    app::message("Could not open", scripted);
                }
                return;
            }
            if (!updateNoticed && !updater::available().empty())
            {
                updateNoticed = true;
                app::toast("PKForge " + updater::available() + " is out: START > Update");
            }
            if (saves::scanning() || wasScanning)
            {
                if (app::frame() % 20 == 0 || !saves::scanning())
                {
                    refresh();
                }
                wasScanning = saves::scanning();
            }

            int buttons = BUTTONS;
            if (in.repeat & KEY_DLEFT)
            {
                if (row == 0 && focus > 0)
                {
                    focus--;
                }
                else if (row == 1 && button > 0)
                {
                    button--;
                }
            }
            if (in.repeat & KEY_DRIGHT)
            {
                if (row == 0 && focus + 1 < int(list.size()))
                {
                    focus++;
                }
                else if (row == 1 && button + 1 < buttons)
                {
                    button++;
                }
            }
            if (in.down & KEY_DDOWN)
            {
                row = 1;
            }
            if ((in.down & KEY_DUP) && !list.empty())
            {
                row = 0;
            }
            if (list.empty())
            {
                row = 1;
            }

            // Cards: tap to focus, tap the focused one to open; drag sideways to scroll.
            if (in.touching && in.dragged && in.startY >= CARD_Y && in.startY < CARD_Y + CARD_H)
            {
                scrollX = std::clamp(scrollX - in.dx, 0, maxScroll());
                dragging = true;
            }
            if (in.touchUp)
            {
                dragging = false;
            }
            for (int i = 0; i < int(list.size()); i++)
            {
                if (in.tapped(cardX(i), CARD_Y, CARD_W, CARD_H))
                {
                    row = 0;
                    if (focus == i)
                    {
                        open();
                        return;
                    }
                    focus = i;
                }
            }
            if (!dragging && row == 0)
            {
                // Keep the focused card on screen.
                int x = 8 + focus * (CARD_W + CARD_GAP);
                if (x - scrollX < 8)
                {
                    scrollX = x - 8;
                }
                if (x + CARD_W - scrollX > BOTTOM_W - 8)
                {
                    scrollX = x + CARD_W - (BOTTOM_W - 8);
                }
            }

            for (int i = 0; i < buttons; i++)
            {
                if (in.tapped(buttonX(i), BUTTON_Y, BUTTON_W, BUTTON_H))
                {
                    row    = 1;
                    button = i;
                    press(i);
                    return;
                }
            }
            if (in.down & KEY_A)
            {
                if (row == 0)
                {
                    open();
                }
                else
                {
                    press(button);
                }
                return;
            }
            if (in.down & KEY_X)
            {
                press(0);
            }
            if (in.down & KEY_Y)
            {
                r4link::start(); // Link: R4 PokéLink
                return;
            }
            if (in.down & KEY_L)
            {
                filter = (filter + 1) % 5;
                refresh();
                scrollX = 0;
            }
            if ((in.down & KEY_SELECT) && row == 0 && !list.empty())
            {
                askDelete(list[focus]);
                return;
            }
            if (in.down & KEY_START)
            {
                settings();
            }
            if ((in.down & KEY_B) && row == 0 && !list.empty())
            {
                editGame(list[focus]);
            }
        }

        void drawTop() override
        {
            housing(0, 0, TOP_W, H);
            image(ui::logo_small, 8, 6);
            text(32, 10, "PKForge", T::Ink);
            text(TOP_W - 8, 10, "3DS", T::InkSoft, Right);
            rect(0, 31, TOP_W, 1, T::Grid);

            if (list.empty())
            {
                image(ui::logo, TOP_W / 2 - 42, 50);
                if (saves::scanning())
                {
                    char line[64];
                    snprintf(line, sizeof(line), "Looking for saves...  %d folders", saves::foldersSeen());
                    text(TOP_W / 2, 166, line, T::Ink, Center);
                }
                else
                {
                    text(TOP_W / 2, 160, filter ? "No saves for this filter (L)" : "No saves found", T::Ink, Center);
                    wrap(60, 178, 280,
                        "Put .sav files anywhere on the SD card (sdmc:/3ds/PKForge/saves works), then START > "
                        "Rescan saves.",
                        T::InkSoft);
                }
                return;
            }

            const SaveRef& s = list[std::min(focus, int(list.size()) - 1)];
            panel(12, 44, 120, 120, T::Well, T::FrameEdge);
            if (!cartridge(gameArtOf(s), 72, 104, 2))
            {
                image(ui::logo, 30, 54);
            }
            text(146, 46, gameName(s), T::Ink, Left, 1, 244);
            char line[96];
            snprintf(line, sizeof(line), "Generation %d", s.gen);
            text(146, 61, line, T::SubInk);
            int y = 82;
            auto detail = [&](const char* label, const std::string& value)
            {
                rect(144, y - 2, 246, 15, T::LabelColumn);
                text(150, y, label, T::InkSoft);
                text(236, y, value, T::Value, Left, 1, 150);
                y += 17;
            };
            detail("Trainer", s.ot);
            snprintf(line, sizeof(line), "%05u", unsigned(s.tid));
            detail("ID No.", line);
            snprintf(line, sizeof(line), "%d:%02d:%02d", s.hours, s.minutes, s.seconds);
            detail("Play time", line);
            snprintf(line, sizeof(line), "%d seen  %d caught", s.seen, s.caught);
            detail("Pokedex", line);
            snprintf(line, sizeof(line), "%d", s.badges);
            detail("Badges", line);
            detail("From", s.source);
            std::string where = s.path;
            if (titles::isNdsCard(s))
            {
                where = "DS game card  " + s.path.substr(s.path.find(':') + 1);
            }
            else if (s.isTitle())
            {
                where = "Title " + s.path.substr(s.path.find(':') + 1) +
                        (s.media == MEDIATYPE_GAME_CARD ? "  -  game card" : "  -  installed on the SD card");
            }
            text(12, 222, where, T::SubInk, Left, 1, 376);
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::titleBar(BOTTOM_W, "PKForge", saves::scanning() ? "Scanning..." : "Offline");
            // "Games" and the filter chip.
            text(8, 30, "Games", T::Ink);
            std::string chip = std::string("Filter - ") + FILTERS[filter];
            int cw = int(textWidth(chip)) + 12;
            panel(BOTTOM_W - 6 - cw, 28, cw, 15, T::Deep, T::FrameEdge);
            text(BOTTOM_W - 6 - cw / 2, 29, chip, T::Ink, Center);

            clip(0, CARD_Y - 2, BOTTOM_W, CARD_H + 4);
            for (int i = 0; i < int(list.size()); i++)
            {
                int x = cardX(i);
                if (x + CARD_W < 0 || x > BOTTOM_W)
                {
                    continue;
                }
                drawCard(list[i], x, row == 0 && i == focus);
            }
            unclip();
            if (list.empty())
            {
                panel(8, CARD_Y, BOTTOM_W - 16, CARD_H, T::Deep, T::FrameEdge);
                text(BOTTOM_W / 2, CARD_Y + 44, saves::scanning() ? "Looking for saves..." : filter ? "No saves here" : "No saves yet",
                    T::InkSoft, Center);
            }

            const char* labels[] = {"Bank", "Dex Autopilot", "Poképark", "Events", "Settings"};
            for (int i = 0; i < BUTTONS; i++)
            {
                bool on = row == 1 && i == button;
                if (i == 0 || i == 4)
                {
                    widgets::buttonFace(buttonX(i), BUTTON_Y, BUTTON_W, BUTTON_H, labels[i], on, false,
                        i == 0 ? ui::bank : ui::settings);
                    continue;
                }
                widgets::buttonFace(buttonX(i), BUTTON_Y, BUTTON_W, BUTTON_H, "", on, false);
                float cx = buttonX(i) + BUTTON_W / 2.0f, cy = BUTTON_Y + 22;
                if (i == 1)
                {
                    dexIcon(cx, cy);
                }
                else if (i == 2)
                {
                    pawIcon(cx, cy);
                }
                else
                {
                    eventsIcon(cx, cy);
                }
                // Two-word labels sit on two lines on the 3DS's narrower tiles.
                if (i == 1)
                {
                    text(cx, BUTTON_Y + BUTTON_H - 24, "Dex", T::Ink, Center);
                    text(cx, BUTTON_Y + BUTTON_H - 13, "Autopilot", T::Ink, Center, 1, BUTTON_W - 4);
                }
                else
                {
                    text(cx, BUTTON_Y + BUTTON_H - 15, labels[i], T::Ink, Center, 1, BUTTON_W - 6);
                }
            }
            widgets::footer(BOTTOM_W, {{"A", "Open"}, {"B", "Edit game"}, {"L", "Filter"}, {"Y", "Link"}, {"+", "Settings"}});
        }

    private:
        std::vector<SaveRef> list;
        int focus = 0, row = 0, button = 0, scrollX = 0, filter = 0;
        bool wasScanning = true, dragging = false, updateNoticed = false;

        void refresh()
        {
            std::string current = list.empty() ? "" : list[focus].path;
            bool wasEmpty       = list.empty();
            list = saves::found();
            std::erase_if(list, [this](const SaveRef& s) { return !passes(s, filter); });
            if (wasEmpty && !list.empty())
            {
                row = 0; // the first saves found take the focus from the buttons
            }
            std::sort(list.begin(), list.end(), [](const SaveRef& a, const SaveRef& b)
                {
                    // The cartridge, then installed games, then PKForge's own folder, then by generation.
                    auto rank = [](const SaveRef& s)
                    {
                        if (s.isTitle())
                        {
                            return s.media == MEDIATYPE_GAME_CARD ? 0 : 1;
                        }
                        return s.path.rfind("sdmc:/3ds/PKForge/saves/", 0) == 0 ? 2 : 3;
                    };
                    if (rank(a) != rank(b))
                    {
                        return rank(a) < rank(b);
                    }
                    if (a.gen != b.gen)
                    {
                        return a.gen < b.gen;
                    }
                    return a.path < b.path;
                });
            focus = 0;
            for (int i = 0; i < int(list.size()); i++)
            {
                if (list[i].path == current)
                {
                    focus = i;
                }
            }
        }

        int maxScroll() const
        {
            int total = 8 + int(list.size()) * (CARD_W + CARD_GAP) + 2;
            return std::max(0, total - BOTTOM_W);
        }

        int cardX(int i) const { return 8 + i * (CARD_W + CARD_GAP) - scrollX; }

        int buttonX(int i) const { return 4 + i * (BUTTON_W + 3); }

        void drawCard(const SaveRef& s, int x, bool focused)
        {
            panel(x, CARD_Y, CARD_W, CARD_H, focused ? T::Grid : T::CardFill, focused ? T::Cyan : T::FrameEdge);
            // The cartridge: a shell in the system's colour, a black label strip, the game's art in its window.
            int cx    = x + CARD_W / 2;
            u32 shell = shellColor(s.gen);
            // The black label strip runs the shell's width on top, as the original's.
            panel(cx - 22, CARD_Y + 6, 44, 50, shell, mix(shell, T::Void, 0.5f));
            panel(cx - 22, CARD_Y + 6, 44, 6, rgb(0x14, 0x14, 0x18), rgb(0x14, 0x14, 0x18));
            panel(cx - 19, CARD_Y + 14, 38, 38, rgb(0x16, 0x16, 0x1C), mix(shell, T::Void, 0.6f));
            if (!cartridge(gameArtOf(s), cx, CARD_Y + 33, 0.75f))
            {
                image(ui::logo_small, cx - 10, CARD_Y + 21);
            }
            text(cx, CARD_Y + 59, gameName(s), T::Ink, Center, 1, CARD_W - 4);
            char line[64];
            snprintf(line, sizeof(line), "%d:%02d:%02d", s.hours, s.minutes, s.seconds);
            text(cx, CARD_Y + 72, s.ot, T::Value, Center, 1, CARD_W - 4);
            text(cx, CARD_Y + 84, line, T::InkSoft, Center, 1, CARD_W - 4);
        }

        void open()
        {
            if (!list.empty())
            {
                openRef(list[focus]);
            }
        }

        void openRef(const SaveRef& ref)
        {
            std::string error;
            auto opened = OpenSave::open(ref, error);
            if (!opened)
            {
                app::message("Could not open", error);
                return;
            }
            app::push(screens::storage(std::shared_ptr<OpenSave>(std::move(opened))));
        }

        // Save files only: a game's own save data stays (the cartridge or the installed title).
        void askDelete(const SaveRef& ref)
        {
            if (ref.isTitle())
            {
                app::message("Delete", "This is the game's own save data. PKForge deletes save files on the SD card only.");
                return;
            }
            if (autotest::active() && ref.path.rfind(autotest::TEST_SAVES, 0) != 0)
            {
                app::message("Delete", "Test scripts only delete saves in " + std::string(autotest::TEST_SAVES));
                return;
            }
            app::confirm("Delete " + files::baseName(ref.path) + "? A restore point stays in 3ds/PKForge/backups.",
                [this, ref]
                {
                    std::string error;
                    if (!backups::make(ref, error))
                    {
                        app::message("Delete", error);
                        return;
                    }
                    if (remove(ref.path.c_str()) != 0)
                    {
                        app::message("Delete", "Could not delete " + ref.path + ".");
                        return;
                    }
                    saves::forget(ref.path);
                    std::erase_if(list, [&](const SaveRef& r) { return !r.isTitle() && r.path == ref.path; });
                    focus = std::clamp(focus, 0, std::max(0, int(list.size()) - 1));
                    app::toast("Deleted " + files::baseName(ref.path));
                },
                "Delete", "Cancel");
        }

        void press(int i)
        {
            switch (i)
            {
                case 0: app::push(screens::storage(nullptr)); break;
                case 1: autopilot::open(); break;
                case 2: app::push(screens::park()); break;
                case 3: events(); break;
                default: settings(); break;
            }
        }

        // The focused game's events: its save opens on its Mystery Gift and World & events menu.
        void events()
        {
            if (list.empty())
            {
                app::message("Events", "Pick a game first: its Mystery Gift and world events open from here.");
                return;
            }
            std::string error;
            auto opened = OpenSave::open(list[focus], error);
            if (!opened)
            {
                app::message("Could not open", error);
                return;
            }
            app::push(screens::storageEvents(std::shared_ptr<OpenSave>(std::move(opened))));
        }

        void editGame(const SaveRef& ref)
        {
            using E = pksm::GameVersion::EnumType;
            std::vector<std::pair<std::string, std::string>> choices; // name, art
            switch (E(ref.version))
            {
                case E::R:
                case E::S: choices = {{"Ruby", "ruby"}, {"Sapphire", "sapphire"}}; break;
                case E::FR:
                case E::LG: choices = {{"FireRed", "firered"}, {"LeafGreen", "leafgreen"}}; break;
                default: break;
            }
            if (choices.empty())
            {
                app::message("Edit game", "This save already names its own game: " + pkdata::game(ref.version) + ".");
                return;
            }
            std::vector<app::Option> o;
            for (auto& [name, art] : choices)
            {
                std::string n = name, a = art, path = ref.path;
                o.push_back({n, [n, a, path]()
                    {
                        editions()[path] = {n, a};
                        saveEditions();
                        app::toast("This save is " + n + " now.");
                    }});
            }
            std::string path = ref.path;
            o.push_back({"Both (as found)", [path]()
                {
                    editions().erase(path);
                    saveEditions();
                }});
            o.push_back({"Close", []() {}});
            app::menu("Which game is it?", std::move(o));
        }

        void rescan()
        {
            saves::startScan();
            wasScanning = true;
            list.clear();
            focus = scrollX = 0;
        }

        void settings()
        {
            std::vector<app::Option> o;
            std::string newer = updater::available();
            o.push_back({newer.empty() ? "Check for updates" : "Update to " + newer, []() { app::push(updater::open()); },
                std::string("This is PKForge 3DS ") + PORT_VERSION + ". New versions come from GitHub and install here."});
            o.push_back({"Sound", []() { screens::soundMenu(); }, "Music and sound effects, on or off."});
            o.push_back({std::string("Color theme: ") + themeName(theme()), []()
                {
                    std::vector<screens::PickItem> items;
                    for (int i = 0; i < themeCount(); i++)
                    {
                        items.push_back({i, themeName(i), i == theme() ? "now" : ""});
                    }
                    app::push(screens::picker("Color theme", std::move(items), theme(), [](int i) { theme(i); }));
                },
                "The Android PKForge's colour schemes: Default, then one per type."});
            o.push_back({hax::hardcore() ? "Hardcore mode: on" : "Hardcore mode: off", []()
                {
                    hax::hardcore(!hax::hardcore());
                    app::toast(hax::hardcore() ? "Hardcore on: editing is off, moving stays on." : "Hardcore off.");
                },
                "Data editing off for challenge runs and RetroAchievements; moving Pokémon stays possible."});
            std::string at = online::server();
            o.push_back({"Legality server", []() { online::askServer(); },
                at.empty() ? "Where local-gpss runs, for Legalize with PKHeX. Not set yet." : at});
            o.push_back({"R4 PokéLink", []() { r4link::start(); }, "Saves between an R4 card and the SD card (Y)."});
            o.push_back({"Rescan saves", [this]() { rescan(); }, "Look through the games and the SD card again."});
            if (!list.empty())
            {
                SaveRef ref = list[focus];
                o.push_back({"Delete this save file", [this, ref]() { askDelete(ref); }, "Also SELECT on its card."});
            }
            o.push_back({"Quick guide", []() { app::push(guide::open()); }, "The basics in a few pages."});
            o.push_back({"About", []() { app::push(screens::about()); }});
            o.push_back({"Exit PKForge", []() { app::quit(); }});
            o.push_back({"Close", []() {}});
            app::menu("Settings", std::move(o));
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> home() { return std::make_unique<Home>(); }
}

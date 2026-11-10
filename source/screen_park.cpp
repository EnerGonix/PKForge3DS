// The Poképark, after the Android PKForge's: up to 24 Pokémon from the Bank living on the park map
// (AlchemyBats' tiles, converted by tools/park.py). Each has a pace, a range around its home and a
// favourite gesture from its species; two that meet react to each other (ParkLife.Meetings); they sleep
// at night (but for a campfire's few night owls); some days it rains, dry nights have fireflies. A thrown
// ball sends the awake ones running; now and then two of them race; the park keeps a journal on the SD.
// Each has fun and energy: tired ones nap at Blissey's Daycare by day. Hide and seek: the awake ones hide
// and only show when the view comes close. The chosen one says a short line (written for this port).
// Emote glyphs are the Android ParkEmotes'. The top screen is the park, the bottom the map and the area's
// name with a card (Play, Call) or, after SELECT, the journal.
#include "app.hpp"
#include "audio.hpp"
#include "bank.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include <citro2d.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

using namespace gfx;

namespace
{
    constexpr int TILE = 256, MAX_RESIDENTS = 24, MINI_X = 8, MINI_Y = 26;

    // ---------------------------------------------------------------- emotes (Android ParkEmotes)

    enum class Emote
    {
        None,
        Heart,
        Note,
        Exclaim,
        Question,
        Anger,
        Sparkle,
        Ellipsis,
        Sleep,
        Sweat
    };

    struct Glyph
    {
        const char* rows[6];
        u32 color;
    };

    const Glyph* glyphFor(Emote e)
    {
        static const Glyph heart{{".##.##.", "#######", "#######", ".#####.", "..###..", "...#..."}, rgb(0xe0, 0x4f, 0x6a)};
        static const Glyph note{{"...##..", "...#.#.", "...#..#", "...#...", ".###...", ".###..."}, rgb(0x3d, 0x6f, 0xc9)};
        static const Glyph exclaim{{"...#...", "...#...", "...#...", "...#...", ".......", "...#..."}, rgb(0xd9, 0x3a, 0x3a)};
        static const Glyph question{{"..###..", ".#...#.", "....#..", "...#...", ".......", "...#..."}, rgb(0x3d, 0x6f, 0xc9)};
        static const Glyph anger{{"##...##", ".#...#.", ".......", ".#...#.", "##...##", nullptr}, rgb(0xd9, 0x3a, 0x3a)};
        static const Glyph sparkle{{"...#...", "..###..", "#######", "..###..", ".##.##.", "##...##"}, rgb(0xe8, 0xb0, 0x20)};
        static const Glyph ellipsis{{".......", ".......", "#..#..#", nullptr, nullptr, nullptr}, rgb(0x55, 0x58, 0x6e)};
        switch (e)
        {
            case Emote::Heart: return &heart;
            case Emote::Note: return &note;
            case Emote::Exclaim: return &exclaim;
            case Emote::Question: return &question;
            case Emote::Anger: return &anger;
            case Emote::Sparkle: return &sparkle;
            case Emote::Ellipsis: return &ellipsis;
            default: return nullptr;
        }
    }

    void pixels(const char* const* rows, int count, float left, float top, u32 color)
    {
        for (int y = 0; y < count && rows[y]; y++)
        {
            for (int x = 0; rows[y][x]; x++)
            {
                if (rows[y][x] == '#')
                {
                    rect(left + x, top + y, 1, 1, color);
                }
            }
        }
    }

    // x, y: just above the head; age in frames since the emote started.
    void drawEmote(Emote e, float x, float y, int age)
    {
        const u32 outline = rgb(0x2b, 0x2d, 0x42), paper = rgb(0xff, 0xfb, 0xec);
        x = std::floor(x);
        y = std::floor(y);
        if (e == Emote::Sleep)
        {
            static const char* zed[] = {"####", "..#.", ".#..", "####"};
            for (int i = 0; i < 2; i++)
            {
                float t  = std::fmod(age / 84.0f + i * 0.5f, 1.0f);
                u8 a     = u8(255 * std::min(1.0f, 2 * (1 - t)));
                float zx = x + 2 + i * 3 + std::round(t * 3), zy = y - 4 - std::round(t * 8);
                pixels(zed, 4, zx + 1, zy + 1, alpha(outline, a));
                pixels(zed, 4, zx, zy, rgb(0xe9, 0xf1, 0xff, a));
            }
            return;
        }
        if (e == Emote::Sweat)
        {
            static const char* drop[] = {"..#", ".##", "###", "###", ".#."};
            pixels(drop, 5, x + 1, y - 6 + std::round(std::min(age, 36) / 18.0f), rgb(0x5d, 0xa9, 0xe9));
            return;
        }
        const Glyph* g = glyphFor(e);
        if (!g)
        {
            return;
        }
        int rowsUsed = 0;
        while (rowsUsed < 6 && g->rows[rowsUsed])
        {
            rowsUsed++;
        }
        float lift = age < 7 ? 2 : age < 14 ? 1 : 0; // the quick pop as it appears
        float left = x - 5, top = y - 11 + lift;
        rect(left + 1, top, 9, 9, outline);
        rect(left, top + 1, 11, 7, outline);
        rect(left + 2, top + 9, 2, 1, outline); // tail
        rect(left + 1, top + 1, 9, 7, paper);
        pixels(g->rows, rowsUsed, left + 2, top + 1 + (7 - rowsUsed) / 2, g->color);
    }

    // ---------------------------------------------------------------- the areas (ParkMap.World)

    struct Area
    {
        const char* name;
        int x, y, w, h;
    };

    const char* areaAt(float px, float py)
    {
        static const Area areas[] = {{"Sunlit Commons", 336, 768, 336, 192}, {"Ember Peak's crater", 0, 0, 240, 290},
            {"Ember Peak", 0, 0, 304, 472}, {"Harvest Shrine", 408, 0, 168, 300}, {"Snowcap Summit", 728, 0, 280, 330},
            {"Lighthouse Point", 0, 1040, 200, 210}, {"The open sea", 0, 1236, 720, 140}, {"Frostfall", 672, 330, 336, 270},
            {"Amber Woods", 0, 472, 400, 568}, {"Blissey's Daycare", 680, 600, 328, 330}, {"Splash Pool", 520, 930, 488, 446},
            {"Tidewater Coast", 0, 1040, 560, 336}, {"Crossroads Meadow", 0, 0, 1008, 1376}};
        for (const Area& a : areas)
        {
            if (px >= a.x && px < a.x + a.w && py >= a.y && py < a.y + a.h)
            {
                return a.name;
            }
        }
        return "Poképark";
    }

    // ---------------------------------------------------------------- residents

    enum class Act
    {
        Rest,
        Walk,
        Gesture,
        Meet,
        Sleep,
        Play,
        Come,
        Chase, // after the ball
        Race,
        Fire, // warming up at the campfire
        Nap,  // a daytime nap at the daycare
        Hide, // walking to its hiding place
        Hidden
    };

    enum class Move
    {
        None,
        Hop,
        Shake
    };

    struct Resident
    {
        int species = 0, form = 0, level = 0;
        bool shiny = false, nightOwl = false;
        std::string name;
        float x = 0, y = 0, tx = 0, ty = 0, homeX = 0, homeY = 0;
        float pace = 0.45f, range = 80;
        Move favourite       = Move::Hop;
        Emote favouriteEmote = Emote::Note;
        Act act              = Act::Rest;
        int timer = 0, phase = 0, cooldown = 0;
        Move move   = Move::None;
        Emote emote = Emote::None;
        int emoteAge = 0;
        std::string with; // who it is meeting
        float fun = 60, energy = 80; // 0-100: play raises fun, sleep restores energy
        std::string line;            // what it last said, shown on its card a while
        int lineAge = 0;
    };

    u32 hashOf(u32 v)
    {
        v ^= v >> 16;
        v *= 0x7feb352d;
        v ^= v >> 15;
        v *= 0x846ca68b;
        return v ^ (v >> 16);
    }

    class Park : public Screen
    {
    public:
        bool cursorSounds() const override { return false; } // the D-pad moves the camera here

        Park()
        {
            app::Timed timed("park ctor");
            std::string text;
            if (files::readText("romfs:/park/grid.txt", text))
            {
                std::stringstream in(text);
                in >> mapW >> mapH >> cols >> rows;
                std::string line;
                std::getline(in, line);
                while (std::getline(in, line))
                {
                    if (!line.empty())
                    {
                        grid.push_back(line);
                    }
                }
            }
            // The map's 256x256 pieces load when first seen, and the rest one a frame after: opening the park
            // no longer waits for all of them (about 3 MB).
            sheets.assign(cols * rows, nullptr);
            tried.assign(cols * rows, false);
            mini = C2D_SpriteSheetLoad("romfs:/gfx/parkmini.t3x");
            for (int y = 0; y < int(grid.size()); y++)
            {
                for (int x = 0; x < int(grid[y].size()); x++)
                {
                    if (grid[y][x] == '.')
                    {
                        land.push_back({x, y});
                    }
                }
            }
            time_t now = time(nullptr);
            day        = int(now / 86400);
            rainy      = hashOf(u32(day) * 2654435761u) % 5 == 0; // about one day in five
            chooseResidents();
            placeFire();
            time_t clock = time(nullptr);
            int startHour = localtime(&clock)->tm_hour;
            audio::place(startHour >= 21 || startHour < 6 ? audio::Place::ParkNight : audio::Place::ParkDay);
            std::string old;
            if (files::readText(JOURNAL, old))
            {
                std::stringstream lines(old);
                std::string line;
                while (std::getline(lines, line))
                {
                    if (!line.empty())
                    {
                        journal.push_back(line);
                    }
                }
            }
            if (rainy)
            {
                note("It is raining over the park today.");
            }
            if (!residents.empty())
            {
                follow = true;
                centreOn(residents[0].x, residents[0].y);
            }
            else
            {
                centreOn(mapW / 2.0f, mapH / 2.0f);
            }
        }

        ~Park() override
        {
            audio::place(audio::Place::Menu);
            std::string out;
            for (size_t i = journal.size() > 200 ? journal.size() - 200 : 0; i < journal.size(); i++)
            {
                out += journal[i] + "\n";
            }
            files::writeText(JOURNAL, out);
            for (C2D_SpriteSheet s : sheets)
            {
                if (s)
                {
                    C2D_SpriteSheetFree(s);
                }
            }
            if (mini)
            {
                C2D_SpriteSheetFree(mini);
            }
        }

        C2D_SpriteSheet tile(int i)
        {
            if (!tried[i])
            {
                tried[i] = true;
                char path[48];
                snprintf(path, sizeof(path), "romfs:/gfx/park%d.t3x", i);
                sheets[i] = C2D_SpriteSheetLoad(path);
            }
            return sheets[i];
        }

        void update(Input& in) override
        {
            for (int i = 0; i < int(tried.size()); i++)
            {
                if (!tried[i])
                {
                    tile(i); // one piece a frame, in the background of play
                    break;
                }
            }
            if (in.down & KEY_B)
            {
                if (seeking)
                {
                    endSeek(false);
                    return;
                }
                app::pop();
                return;
            }
            if (!residents.empty() && !showJournal && in.tapped(seekX(), BUTTON_Y, BUTTON_W, BUTTON_H))
            {
                seeking ? endSeek(false) : startSeek();
            }
            if (!residents.empty() && (in.down & (KEY_L | KEY_R)))
            {
                int n    = int(residents.size());
                selected = (selected + ((in.down & KEY_R) ? 1 : n - 1)) % n;
                follow   = true;
                audio::cry(residents[selected].species, 0.8f);
            }
            if (in.down & KEY_SELECT)
            {
                showJournal  = !showJournal;
                journalFirst = std::max(0, int(journal.size()) - JOURNAL_ROWS);
            }
            if (showJournal && (in.repeat & (KEY_DUP | KEY_DDOWN)) && !journal.empty())
            {
                journalFirst = std::clamp(journalFirst + ((in.repeat & KEY_DDOWN) ? 1 : -1), 0,
                    std::max(0, int(journal.size()) - JOURNAL_ROWS));
                return;
            }
            if (in.down & KEY_A)
            {
                seeking ? look() : throwBall();
            }
            if (!residents.empty() && ((in.down & KEY_X) || (!showJournal && in.tapped(playX(), BUTTON_Y, BUTTON_W, BUTTON_H))))
            {
                play(residents[selected]);
            }
            if (!residents.empty() && ((in.down & KEY_Y) || (!showJournal && in.tapped(callX(), BUTTON_Y, BUTTON_W, BUTTON_H))))
            {
                call(residents[selected]);
            }
            if (!residents.empty() && !showJournal && in.tapped(feedX(), BUTTON_Y, BUTTON_W, BUTTON_H))
            {
                feed(residents[selected]);
            }
            if (in.down & KEY_START)
            {
                photo();
            }
            if (in.held & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT | KEY_CPAD_UP |
                              KEY_CPAD_DOWN))
            {
                follow = false;
                camX += (in.held & (KEY_DRIGHT | KEY_CPAD_RIGHT) ? 4 : 0) - (in.held & (KEY_DLEFT | KEY_CPAD_LEFT) ? 4 : 0);
                camY += (in.held & (KEY_DDOWN | KEY_CPAD_DOWN) ? 4 : 0) - (in.held & (KEY_DUP | KEY_CPAD_UP) ? 4 : 0);
            }
            if (in.touching && in.inside(MINI_X, MINI_Y, mapW / 8, mapH / 8))
            {
                follow = false;
                centreOn(float((in.tx - MINI_X) * 8), float((in.ty - MINI_Y) * 8));
            }

            time_t now = time(nullptr);
            int hour   = localtime(&now)->tm_hour;
            night      = hour >= 21 || hour < 6;
            if (tick > 0 && night != wasNight)
            {
                audio::place(night ? audio::Place::ParkNight : audio::Place::ParkDay);
                note(night ? "Night falls; the park goes to sleep." : "Morning: the park wakes up.");
            }
            wasNight = night;
            for (Resident& r : residents)
            {
                live(r);
            }
            if (ball)
            {
                if (++ballAge == BALL_FLIGHT)
                {
                    audio::sfx(audio::Sfx::Land);
                }
            }
            raceTick();
            if (tick % 30 == 0)
            {
                meetings();
            }
            if (follow && !seeking && !residents.empty())
            {
                const Resident& r = residents[selected];
                camX += (r.x - TOP_W / 2.0f - camX) * 0.12f;
                camY += (r.y - H / 2.0f - camY) * 0.12f;
            }
            camX = std::clamp(camX, 0.0f, float(mapW - TOP_W));
            camY = std::clamp(camY, 0.0f, float(mapH - H));
            tick++;
        }

        void drawTop() override
        {
            rect(0, 0, TOP_W, H, rgb(0, 0, 0));
            int x0 = int(camX), y0 = int(camY);
            for (int ty = y0 / TILE; ty <= (y0 + H) / TILE && ty < rows; ty++)
            {
                for (int tx = x0 / TILE; tx <= (x0 + TOP_W) / TILE && tx < cols; tx++)
                {
                    if (C2D_SpriteSheet s = tile(ty * cols + tx))
                    {
                        C2D_DrawImageAt(C2D_SpriteSheetGetImage(s, 0), float(tx * TILE - x0), float(ty * TILE - y0), 0.5f);
                    }
                }
            }
            if (night && !rainy)
            {
                drawFire(fireX - x0, fireY - y0);
            }
            std::vector<const Resident*> order;
            for (const Resident& r : residents)
            {
                order.push_back(&r);
            }
            std::sort(order.begin(), order.end(), [](const Resident* a, const Resident* b) { return a->y < b->y; });
            for (const Resident* r : order)
            {
                float sx = r->x - x0, sy = r->y - y0;
                if (sx < -30 || sx > TOP_W + 30 || sy < -40 || sy > H + 30)
                {
                    continue;
                }
                if (r->act == Act::Hidden && std::hypot(sx - TOP_W / 2.0f, sy - H / 2.0f) > 70)
                {
                    continue; // out of sight until the view comes close
                }
                float lift = 0, side = 0;
                if (r->act == Act::Walk || r->act == Act::Come)
                {
                    lift = std::fabs(std::sin((tick + r->phase) * 0.25f)) * 2;
                }
                if (r->move == Move::Hop)
                {
                    lift = std::fabs(std::sin((tick + r->phase) * 0.18f)) * 7;
                }
                if (r->move == Move::Shake)
                {
                    side = std::sin((tick + r->phase) * 0.6f) * 2;
                }
                if (&residents[selected] == r)
                {
                    circle(sx, sy - 1, 9, alpha(T::Cyan, 90));
                }
                rect(sx - 7, sy - 2, 14, 3, rgb(0, 0, 0, 60)); // its shadow
                icon(r->species, r->form, sx + side, sy - 12 - lift, 1);
                if (r->shiny && (tick + r->phase) % 120 < 10)
                {
                    star(sx + 10, sy - 26 - lift, 3, S::ShinyGold);
                }
                drawEmote(r->emote, sx + 2, sy - 26 - lift, r->emoteAge);
            }
            if (ball)
            {
                // Flying in an arc, then lying where it landed.
                float t  = std::min(1.0f, ballAge / float(BALL_FLIGHT));
                float bx = bx0 + (bx1 - bx0) * t, gy = by0 + (by1 - by0) * t;
                float by = gy - std::sin(t * 3.14159f) * 50;
                rect(bx - x0 - 4, gy - y0 - 1, 8, 2, rgb(0, 0, 0, 70)); // its shadow on the ground
                drawBall(bx - x0, by - y0 - 5);
            }
            weather();
            time_t now = time(nullptr);
            int hour   = localtime(&now)->tm_hour;
            if (night)
            {
                rect(0, 0, TOP_W, H, rgb(10, 20, 70, 120));
            }
            else if (hour >= 17)
            {
                rect(0, 0, TOP_W, H, rgb(255, 120, 40, 45));
            }
            else if (hour < 8)
            {
                rect(0, 0, TOP_W, H, rgb(255, 150, 190, 35)); // a pink morning
            }
            if (night && !rainy)
            {
                fireflies();
            }
        }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::header(BOTTOM_W, "Poképark", areaAt(camX + TOP_W / 2.0f, camY + H / 2.0f));
            int mw = mapW / 8, mh = mapH / 8;
            panel(MINI_X - 2, MINI_Y - 2, mw + 4, mh + 4, T::Void, T::FrameEdge);
            if (mini)
            {
                C2D_DrawImageAt(C2D_SpriteSheetGetImage(mini, 0), MINI_X, MINI_Y, 0.5f);
            }
            for (int i = 0; i < int(residents.size()); i++)
            {
                const Resident& r = residents[i];
                if (seeking && (r.act == Act::Hide || r.act == Act::Hidden))
                {
                    continue; // no peeking on the map
                }
                rect(MINI_X + r.x / 8 - 1, MINI_Y + r.y / 8 - 1, 3, 3, i == selected ? T::Cyan : rgb(255, 255, 255));
            }
            outline(MINI_X + camX / 8, MINI_Y + camY / 8, TOP_W / 8.0f, H / 8.0f, S::ShinyGold, 1);

            int cx = MINI_X + mw + 10, cw = BOTTOM_W - cx - 6;
            panel(cx, MINI_Y - 2, cw, mh + 4, T::PanelFill, T::FrameEdge);
            if (showJournal)
            {
                text(cx + 8, MINI_Y + 2, "Park journal", T::Ink);
                if (journal.empty())
                {
                    wrap(cx + 8, MINI_Y + 22, cw - 16, "Nothing has happened yet.", T::InkSoft, 1, 4);
                }
                for (int i = 0; i < JOURNAL_ROWS && journalFirst + i < int(journal.size()); i++)
                {
                    const std::string& line = journal[journalFirst + i];
                    int y = MINI_Y + 20 + i * 36;
                    text(cx + 6, y, line.substr(0, 11), T::SubInk);
                    wrap(cx + 6, y + 11, cw - 12, line.size() > 13 ? line.substr(13) : "", T::Value, 1, 2);
                }
            }
            else if (residents.empty())
            {
                wrap(cx + 8, MINI_Y + 8, cw - 16, "Nobody lives here yet: Pokémon in the Bank move into the park.",
                    T::InkSoft, 1, 8);
            }
            else
            {
                const Resident& r = residents[selected];
                icon(r.species, r.form, cx + 34, MINI_Y + 22, 2);
                text(cx + 8, MINI_Y + 46, r.name, T::Ink, Left, 1, cw - 16);
                text(cx + 8, MINI_Y + 62, pkdata::species(r.species) + "  Lv. " + std::to_string(r.level), T::SubInk,
                    Left, 1, cw - 16);
                text(cx + 8, MINI_Y + 80, doing(r), r.shiny ? S::ShinyGold : T::Value, Left, 1, cw - 16);
                char line[40];
                snprintf(line, sizeof(line), "%d of %d living here", selected + 1, int(residents.size()));
                if (seeking)
                {
                    snprintf(line, sizeof(line), "Found %d of %d", seekFound, seekTotal);
                    text(cx + 8, MINI_Y + 96, line, S::ShinyGold);
                }
                else if (!r.line.empty() && r.lineAge < 300)
                {
                    wrap(cx + 8, MINI_Y + 96, cw - 16, "\"" + r.line + "\"", T::Ink, 1, 2);
                }
                else
                {
                    text(cx + 8, MINI_Y + 96, line, T::SubInk);
                    text(cx + 8, MINI_Y + 110, rainy ? "Rain today" : night ? "Night" : "Fair weather", T::InkSoft);
                }
                // Fun and energy.
                for (int k = 0; k < 2; k++)
                {
                    float v = k ? r.energy : r.fun;
                    int bx = cx + 8 + k * 78, by = MINI_Y + 124;
                    text(bx, by - 2, k ? "Energy" : "Fun", T::SubInk, Left, 0.7f);
                    rect(bx + 34, by + 2, 36, 5, T::Void);
                    rect(bx + 34, by + 2, 36 * v / 100, 5, k ? T::Cyan : S::ShinyGold);
                }
                widgets::buttonFace(playX(), BUTTON_Y, BUTTON_W, BUTTON_H, "Play", false, false);
                widgets::buttonFace(callX(), BUTTON_Y, BUTTON_W, BUTTON_H, "Call", false, false);
                widgets::buttonFace(feedX(), BUTTON_Y, BUTTON_W, BUTTON_H, "Feed", false, false);
                widgets::buttonFace(seekX(), BUTTON_Y, BUTTON_W, BUTTON_H, seeking ? "Stop" : "Seek", seeking, false);
            }
            if (seeking)
            {
                widgets::footer(BOTTOM_W, {{"A", "Found you!"}, {"+", "Look"}, {"B", "Stop"}});
            }
            else
            {
                widgets::footer(BOTTOM_W, {{"A", "Ball"}, {"SELECT", "Journal"}, {"START", "Photo"}, {"B", "Back"}});
            }
        }

    private:
        static constexpr int BUTTON_Y = MINI_Y + 136, BUTTON_W = 38, BUTTON_H = 24;
        int mapW = 1008, mapH = 1376, cols = 4, rows = 6;
        std::vector<std::string> grid;
        std::vector<C2D_SpriteSheet> sheets;
        std::vector<bool> tried;
        C2D_SpriteSheet mini = nullptr;
        std::vector<std::pair<int, int>> land;
        std::vector<Resident> residents;
        int selected = 0, tick = 0, day = 0;
        float camX = 0, camY = 0;
        bool follow = false, night = false, rainy = false, wasNight = false, showJournal = false;
        // The ball: flying from (bx0, by0) to (bx1, by1), then lying there until someone gets it.
        bool ball = false;
        float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
        int ballAge = 0;
        // The race: two runners, the finish line, and when the next one may start.
        int racer1 = -1, racer2 = -1, nextRace = 3600;
        float finishX = 0, finishY = 0;
        // The campfire, in Sunlit Commons.
        float fireX = 0, fireY = 0;
        std::vector<std::string> journal;
        int journalFirst = 0;
        // Hide and seek: who is still hidden, how many were found, when it began.
        bool seeking = false;
        int seekFound = 0, seekTotal = 0, seekStart = 0;

        int playX() const { return MINI_X + mapW / 8 + 16; }
        int callX() const { return playX() + BUTTON_W + 4; }
        int feedX() const { return callX() + BUTTON_W + 4; }
        int seekX() const { return feedX() + BUTTON_W + 4; }

        bool isLand(int px, int py) const
        {
            int cx = px / 8, cy = py / 8;
            return cy >= 0 && cy < int(grid.size()) && cx >= 0 && cx < int(grid[cy].size()) && grid[cy][cx] == '.';
        }

        void centreOn(float x, float y)
        {
            camX = x - TOP_W / 2.0f;
            camY = y - H / 2.0f;
        }

        void say(Resident& r, Emote e)
        {
            r.emote    = e;
            r.emoteAge = 0;
        }

        // Up to 24 residents from the Bank, one of each species, in the Bank's order; their ways come
        // from their species, so the same Pokémon is always the same character.
        void chooseResidents()
        {
            Bank& bank = app::bank();
            std::vector<int> seen;
            for (int b = 0; b < bank.boxes() && int(residents.size()) < MAX_RESIDENTS; b++)
            {
                for (int s = 0; s < Bank::SLOTS && int(residents.size()) < MAX_RESIDENTS; s++)
                {
                    const BankEntry* e = bank.at(b, s);
                    if (!e || e->species <= 0 || land.empty() ||
                        std::find(seen.begin(), seen.end(), e->species) != seen.end())
                    {
                        continue;
                    }
                    seen.push_back(e->species);
                    Resident r;
                    r.species = e->species;
                    r.form    = e->form;
                    r.level   = e->level;
                    r.shiny   = e->shiny;
                    r.name    = e->nickname.empty() ? pkdata::species(e->species) : e->nickname;
                    u32 h     = hashOf(u32(e->species) * 40503u + 17);
                    r.pace    = 0.3f + (h % 100) / 250.0f;      // 0.30 to 0.70
                    r.range   = 32.0f + float((h >> 8) % 129); // homebodies to roamers
                    r.favourite = (h >> 16) % 2 ? Move::Hop : Move::Shake;
                    static const Emote gestures[] = {Emote::Note, Emote::Heart, Emote::Sparkle, Emote::Note};
                    r.favouriteEmote = gestures[(h >> 20) % 4];
                    r.nightOwl       = (h >> 24) % 6 == 0;
                    auto [cx, cy]    = land[(h >> 4) % land.size()];
                    r.x = r.tx = r.homeX = cx * 8 + 4.0f;
                    r.y = r.ty = r.homeY = cy * 8 + 7.0f;
                    r.timer = 30 + rand() % 120;
                    r.phase = rand() % 100;
                    residents.push_back(r);
                }
            }
        }

        bool pickTarget(Resident& r, float aroundX, float aroundY, float radius)
        {
            for (int tries = 0; tries < 16; tries++)
            {
                float nx = aroundX + float(rand() % int(2 * radius + 1)) - radius;
                float ny = aroundY + float(rand() % int(2 * radius + 1)) - radius;
                if (isLand(int(nx), int(ny)))
                {
                    r.tx = nx;
                    r.ty = ny;
                    return true;
                }
            }
            return false;
        }

        // One step along a straight line; false when it arrived or met a wall.
        bool step(Resident& r, float speed)
        {
            float dx = r.tx - r.x, dy = r.ty - r.y, d = std::sqrt(dx * dx + dy * dy);
            if (d < 0.8f)
            {
                return false;
            }
            float nx = r.x + dx / d * speed, ny = r.y + dy / d * speed;
            if (!isLand(int(nx), int(ny)))
            {
                r.tx = r.x;
                r.ty = r.y;
                return false;
            }
            r.x = nx;
            r.y = ny;
            return true;
        }

        void live(Resident& r)
        {
            r.emoteAge++;
            r.lineAge++;
            // Needs drift: awake ones tire and get bored, sleepers rest.
            if (tick % 60 == 0)
            {
                bool resting = r.act == Act::Sleep || r.act == Act::Nap;
                r.energy     = std::clamp(r.energy + (resting ? 2.5f : -0.4f), 0.0f, 100.0f);
                r.fun        = std::clamp(r.fun - 0.3f, 0.0f, 100.0f);
            }
            if (r.act == Act::Hide)
            {
                if (!step(r, r.pace * 1.5f))
                {
                    r.act  = Act::Hidden;
                    r.move = Move::None;
                    say(r, Emote::None);
                }
                return;
            }
            if (r.act == Act::Hidden)
            {
                return;
            }
            if (r.act == Act::Nap)
            {
                if (night || r.energy >= 85)
                {
                    r.act   = Act::Rest;
                    r.timer = 60;
                    say(r, Emote::Note);
                    if (!night)
                    {
                        note(r.name + " woke up from a nap at the daycare.");
                    }
                }
                return;
            }
            // Worn out by day: off to Blissey's Daycare for a nap.
            if (!night && r.energy < 20 && r.act == Act::Rest && pickTarget(r, 840, 760, 70))
            {
                r.act = Act::Walk;
                r.with = "daycare";
            }
            if (r.cooldown > 0)
            {
                r.cooldown--;
            }
            if (night && !r.nightOwl && r.act != Act::Play && r.act != Act::Come)
            {
                if (r.act != Act::Sleep)
                {
                    r.act  = Act::Sleep;
                    r.move = Move::None;
                    say(r, Emote::Sleep);
                }
                return;
            }
            // Night owls gather round the campfire.
            if (night && r.nightOwl && r.act == Act::Rest && rand() % 400 == 0 &&
                pickTarget(r, fireX, fireY + 10, 18))
            {
                r.act = Act::Fire;
            }
            if (r.act == Act::Sleep)
            {
                r.act   = Act::Rest; // morning
                r.timer = 60 + rand() % 200;
                say(r, Emote::None);
            }
            switch (r.act)
            {
                case Act::Walk:
                    if (!step(r, r.pace) && r.with == "daycare")
                    {
                        r.with.clear();
                        r.act = Act::Nap;
                        say(r, Emote::Sleep);
                        note(r.name + " is napping at Blissey's Daycare.");
                        break;
                    }
                    if (r.act == Act::Walk && std::hypot(r.tx - r.x, r.ty - r.y) < 0.8f)
                    {
                        // At the end of a walk, now and then, its own little gesture.
                        if (rand() % 100 < 35)
                        {
                            r.act   = Act::Gesture;
                            r.move  = r.favourite;
                            r.timer = 60 + rand() % 40;
                            say(r, r.favouriteEmote);
                        }
                        else
                        {
                            r.act   = Act::Rest;
                            r.timer = 90 + rand() % 220;
                        }
                    }
                    break;
                case Act::Come:
                    if (!step(r, r.pace * 1.8f))
                    {
                        r.act   = Act::Play;
                        r.move  = Move::Hop;
                        r.timer = 90;
                        say(r, Emote::Heart);
                    }
                    break;
                case Act::Rest:
                    if (--r.timer <= 0)
                    {
                        // Back towards home now and then, else anywhere within its range.
                        bool homeward = rand() % 3 == 0;
                        if (pickTarget(r, homeward ? r.homeX : r.x, homeward ? r.homeY : r.y, homeward ? 24 : r.range))
                        {
                            r.act = Act::Walk;
                        }
                        r.timer = 60;
                        if (r.emoteAge > 90)
                        {
                            say(r, Emote::None);
                        }
                    }
                    break;
                case Act::Gesture:
                case Act::Meet:
                case Act::Play:
                    if (--r.timer <= 0)
                    {
                        r.act   = Act::Rest;
                        r.move  = Move::None;
                        r.timer = 120 + rand() % 200;
                        say(r, Emote::None);
                        r.with.clear();
                    }
                    break;
                case Act::Fire:
                    if (!step(r, r.pace))
                    {
                        r.act   = Act::Gesture;
                        r.move  = Move::None;
                        r.timer = 400;
                        say(r, Emote::Note);
                    }
                    break;
                case Act::Chase:
                    r.tx = bx1;
                    r.ty = by1;
                    if (!ball || ballAge < BALL_FLIGHT)
                    {
                        if (!ball)
                        {
                            r.act   = Act::Rest;
                            r.timer = 60;
                            say(r, Emote::Ellipsis); // someone else got it
                        }
                        break;
                    }
                    if (!step(r, r.pace * 1.6f))
                    {
                        ball    = false;
                        r.fun   = std::min(100.0f, r.fun + 25);
                        r.act   = Act::Play;
                        r.move  = Move::Hop;
                        r.timer = 150;
                        say(r, Emote::Heart);
                        note(r.name + " got the ball first.");
                        audio::cry(r.species, 0.7f);
                    }
                    break;
                case Act::Race: break; // raceTick moves the runners
                case Act::Sleep:
                case Act::Nap:
                case Act::Hide:
                case Act::Hidden: break; // handled before the switch
            }
        }

        // ---------------------------------------------------------------- the ball, races, the fire, the journal

        static constexpr const char* JOURNAL = "sdmc:/3ds/PKForge/park-journal.txt";
        static constexpr int JOURNAL_ROWS = 4, BALL_FLIGHT = 40;

        void note(const std::string& what)
        {
            time_t now = time(nullptr);
            char when[16];
            strftime(when, sizeof(when), "%d/%m %H:%M", localtime(&now));
            journal.push_back(std::string(when) + "  " + what);
            journalFirst = std::max(0, int(journal.size()) - JOURNAL_ROWS);
        }

        // A toy ball thrown into the middle of the view; the awake within reach run for it.
        void throwBall()
        {
            float cx = camX + TOP_W / 2.0f, cy = camY + H / 2.0f;
            Resident probe;
            if (!pickTarget(probe, cx, cy, 40))
            {
                return;
            }
            ball    = true;
            ballAge = 0;
            audio::sfx(audio::Sfx::Throw);
            bx0     = cx - 60;
            by0     = camY + H + 10;
            bx1     = probe.tx;
            by1     = probe.ty;
            int chasers = 0;
            for (Resident& r : residents)
            {
                float dx = r.x - bx1, dy = r.y - by1;
                if (r.act != Act::Sleep && r.act != Act::Race && dx * dx + dy * dy < 220 * 220)
                {
                    r.act  = Act::Chase;
                    r.move = Move::None;
                    say(r, Emote::Exclaim);
                    chasers++;
                }
            }
            note(chasers ? "You threw a ball; " + std::to_string(chasers) + " ran after it." : "You threw a ball; nobody was awake to chase it.");
        }

        // Now and then two awake residents race to a spot far away: the quicker one wins.
        void raceTick()
        {
            if (racer1 < 0)
            {
                if (--nextRace > 0 || night || residents.size() < 2)
                {
                    return;
                }
                nextRace = 5400 + rand() % 5400;
                int a = rand() % residents.size(), b = rand() % residents.size();
                Resident& ra = residents[a];
                Resident& rb = residents[b];
                if (a == b || ra.act != Act::Rest || rb.act != Act::Rest)
                {
                    return;
                }
                Resident probe;
                if (!pickTarget(probe, (ra.x + rb.x) / 2, (ra.y + rb.y) / 2, 140))
                {
                    return;
                }
                racer1 = a;
                racer2 = b;
                finishX = probe.tx;
                finishY = probe.ty;
                for (Resident* r : {&ra, &rb})
                {
                    r->act  = Act::Race;
                    r->move = Move::None;
                    say(*r, Emote::Exclaim);
                }
                note(ra.name + " and " + rb.name + " are racing!");
                return;
            }
            bool done = false;
            for (int idx : {racer1, racer2})
            {
                Resident& r = residents[idx];
                r.tx        = finishX;
                r.ty        = finishY;
                if (!step(r, r.pace * 2.2f))
                {
                    done = true;
                }
            }
            if (!done)
            {
                return;
            }
            Resident& a = residents[racer1];
            Resident& b = residents[racer2];
            auto dist   = [this](const Resident& r) { return std::hypot(r.x - finishX, r.y - finishY); };
            Resident& winner = dist(a) <= dist(b) ? a : b;
            Resident& loser  = &winner == &a ? b : a;
            winner.act = loser.act = Act::Gesture;
            winner.timer = loser.timer = 150;
            winner.move               = Move::Hop;
            loser.move                = Move::None;
            say(winner, Emote::Sparkle);
            say(loser, Emote::Sweat);
            winner.fun = std::min(100.0f, winner.fun + 20);
            loser.fun  = std::min(100.0f, loser.fun + 10);
            winner.energy -= 10;
            loser.energy -= 10;
            talk(winner, "won");
            note(winner.name + " won the race against " + loser.name + ".");
            audio::sfx(audio::Sfx::Win);
            racer1 = racer2 = -1;
        }

        // A short line for the moment, by how it feels (written for this port).
        void talk(Resident& r, const std::string& moment)
        {
            static const std::pair<const char*, const char*> lines[] = {{"play", "Again! Again!"},
                {"play", "That was fun!"}, {"play", "Catch me if you can!"}, {"call", "Coming!"},
                {"call", "You called?"}, {"woken", "Five more minutes..."}, {"woken", "Hm? Is it morning?"},
                {"tired", "I need a little rest first."}, {"tired", "So sleepy..."}, {"won", "Too slow!"},
                {"won", "I win!"}, {"found", "Aw, you found me!"}, {"found", "How did you see me?"},
                {"bored", "Is anything happening today?"}};
            std::vector<const char*> pool;
            for (auto& [m, text] : lines)
            {
                if (moment == m)
                {
                    pool.push_back(text);
                }
            }
            if (moment == "play" && r.fun < 25)
            {
                pool = {"Finally, someone to play with!"};
            }
            if (!pool.empty())
            {
                r.line    = pool[rand() % pool.size()];
                r.lineAge = 0;
            }
        }

        void startSeek()
        {
            seekFound = seekTotal = 0;
            for (Resident& r : residents)
            {
                if (r.act == Act::Sleep || r.act == Act::Nap || r.act == Act::Race)
                {
                    continue;
                }
                // Somewhere well away from where the player is looking.
                for (int tries = 0; tries < 20; tries++)
                {
                    auto [cx, cy] = land[rand() % land.size()];
                    float px = cx * 8 + 4.0f, py = cy * 8 + 7.0f;
                    if (std::hypot(px - (camX + TOP_W / 2.0f), py - (camY + H / 2.0f)) > 220)
                    {
                        r.tx  = px;
                        r.ty  = py;
                        r.act = Act::Hide;
                        say(r, Emote::Note);
                        seekTotal++;
                        break;
                    }
                }
            }
            if (!seekTotal)
            {
                note("Nobody was awake for hide and seek.");
                return;
            }
            seeking   = true;
            seekStart = tick;
            follow    = false;
            ball      = false;
            note("Hide and seek: " + std::to_string(seekTotal) + " went to hide.");
        }

        // A: anyone hidden near the middle of the view is found.
        void look()
        {
            bool any = false;
            for (Resident& r : residents)
            {
                // Only once it is really hiding: catching it on its way there would not count.
                if (r.act == Act::Hidden && std::hypot(r.x - (camX + TOP_W / 2.0f), r.y - (camY + H / 2.0f)) < 50)
                {
                    r.act   = Act::Play;
                    r.move  = Move::Hop;
                    r.timer = 120;
                    r.fun   = std::min(100.0f, r.fun + 20);
                    say(r, Emote::Exclaim);
                    talk(r, "found");
                    audio::sfx(audio::Sfx::Found);
                    audio::cry(r.species, 0.8f);
                    selected = int(&r - residents.data());
                    seekFound++;
                    any = true;
                }
            }
            if (!any)
            {
                audio::sfx(audio::Sfx::Error);
                app::toast("Nobody here...");
            }
            if (seekFound == seekTotal)
            {
                endSeek(true);
            }
        }

        void endSeek(bool won)
        {
            int seconds = (tick - seekStart) / 60;
            if (won)
            {
                note("Hide and seek: found everyone in " + std::to_string(seconds / 60) + " min " +
                     std::to_string(seconds % 60) + " s.");
            }
            else
            {
                note("Hide and seek: stopped with " + std::to_string(seekFound) + " of " + std::to_string(seekTotal) +
                     " found.");
            }
            for (Resident& r : residents)
            {
                if (r.act == Act::Hide || r.act == Act::Hidden)
                {
                    r.act   = Act::Rest;
                    r.timer = 30;
                    say(r, won ? Emote::None : Emote::Note);
                }
            }
            seeking = false;
            follow  = true;
        }

        void placeFire()
        {
            // The middle of Sunlit Commons, or the nearest land to it.
            float best = 1e9f;
            for (auto [x, y] : land)
            {
                float px = x * 8 + 4.0f, py = y * 8 + 4.0f;
                float d  = std::hypot(px - 504, py - 880);
                if (d < best)
                {
                    best  = d;
                    fireX = px;
                    fireY = py;
                }
            }
        }

        void drawFire(float sx, float sy)
        {
            // A warm pool of light, two crossed logs and flickering flames.
            for (int ring = 3; ring > 0; ring--)
            {
                circle(sx, sy, 10.0f + ring * 8, rgb(255, 160, 60, u8(18 * ring)));
            }
            rect(sx - 7, sy + 1, 14, 3, rgb(0x6b, 0x44, 0x23));
            rect(sx - 5, sy - 1, 10, 3, rgb(0x8a, 0x5a, 0x2e));
            for (int i = 0; i < 5; i++)
            {
                float f = std::sin((tick + i * 17) * 0.3f) * 1.5f;
                float h = 5 + std::fabs(std::sin((tick + i * 11) * 0.21f)) * 6;
                rect(sx - 5 + i * 2.2f + f * 0.3f, sy - h, 2, h, i % 2 ? rgb(255, 200, 60) : rgb(240, 100, 40));
            }
            rect(sx - 1, sy - 4, 2, 3, rgb(255, 245, 190));
        }

        void drawBall(float sx, float sy)
        {
            // A plain toy ball: blue with a yellow band, a highlight on top.
            circle(sx, sy, 5.0f, rgb(0x2b, 0x2d, 0x42));
            circle(sx, sy, 4.2f, rgb(0x4a, 0x9c, 0xf0));
            rect(sx - 4.2f, sy - 1, 8.4f, 2, rgb(0xf4, 0xd0, 0x3f));
            rect(sx - 2, sy - 3, 2, 1, rgb(0xff, 0xff, 0xff));
        }

        // Two resting neighbours notice each other (Android ParkLife.Meetings, simplified): a shiny and a
        // plain one of its kind, twins, or two strangers saying hello. The same one meets again only later.
        void meetings()
        {
            for (size_t i = 0; i < residents.size(); i++)
            {
                for (size_t j = i + 1; j < residents.size(); j++)
                {
                    Resident& a = residents[i];
                    Resident& b = residents[j];
                    if (a.act != Act::Rest || b.act != Act::Rest || a.cooldown || b.cooldown)
                    {
                        continue;
                    }
                    float dx = a.x - b.x, dy = a.y - b.y;
                    if (dx * dx + dy * dy > 32 * 32 || rand() % 4)
                    {
                        continue;
                    }
                    a.fun = std::min(100.0f, a.fun + 10);
                    b.fun = std::min(100.0f, b.fun + 10);
                    a.act = b.act = Act::Meet;
                    a.timer = b.timer = 150;
                    a.cooldown = b.cooldown = 1800;
                    a.with                  = b.name;
                    b.with                  = a.name;
                    note(a.name + " met " + b.name + ".");
                    if (std::hypot(a.x - (camX + TOP_W / 2.0f), a.y - (camY + H / 2.0f)) < 220)
                    {
                        audio::cry(a.species, 0.45f);
                    }
                    if (a.species == b.species && a.shiny != b.shiny)
                    {
                        say(a.shiny ? a : b, Emote::Sparkle);
                        say(a.shiny ? b : a, Emote::Question);
                        (a.shiny ? b : a).move = Move::Shake;
                    }
                    else if (a.species == b.species)
                    {
                        say(a, Emote::Note);
                        say(b, Emote::Note);
                        a.move = b.move = Move::Hop; // copying each other
                    }
                    else
                    {
                        say(a, Emote::Exclaim);
                        say(b, Emote::Heart);
                    }
                    return;
                }
            }
        }

        void play(Resident& r)
        {
            if (r.act == Act::Sleep || r.act == Act::Nap)
            {
                say(r, rand() % 3 ? Emote::Ellipsis : Emote::Anger); // woken for nothing
                talk(r, "woken");
                return;
            }
            if (r.energy < 15)
            {
                say(r, Emote::Sweat);
                talk(r, "tired");
                return;
            }
            r.fun = std::min(100.0f, r.fun + 30);
            talk(r, "play");
            audio::sfx(audio::Sfx::Hop);
            audio::cry(r.species);
            r.act   = Act::Play;
            r.move  = Move::Hop;
            r.timer = 120;
            say(r, rand() % 2 ? Emote::Heart : Emote::Note);
            note("You played with " + r.name + ".");
        }

        // Feeding, as the original's: a berry for the chosen one, more energy and a little fun.
        void feed(Resident& r)
        {
            if (r.act == Act::Sleep || r.act == Act::Nap)
            {
                say(r, Emote::Ellipsis); // asleep: it will eat later
                return;
            }
            r.energy = std::min(100.0f, r.energy + 35);
            r.fun    = std::min(100.0f, r.fun + 10);
            say(r, Emote::Heart);
            audio::sfx(audio::Sfx::Sparkle);
            audio::cry(r.species, 0.7f);
            note("You gave " + r.name + " a berry.");
        }

        // Photos, as the original's: the park as it is now, saved in sdmc:/3ds/PKForge/photos.
        void photo()
        {
            std::string folder = std::string(files::ROOT) + "/photos";
            files::mkdirs(folder);
            gfx::requestShot(folder + "/park-" + files::stamp() + ".bmp");
            audio::sfx(audio::Sfx::Found);
            app::toast("Photo saved in 3ds/PKForge/photos.");
            note("You took a photo.");
        }

        void call(Resident& r)
        {
            if (r.act == Act::Sleep)
            {
                say(r, Emote::Ellipsis);
                return;
            }
            if (pickTarget(r, camX + TOP_W / 2.0f, camY + H / 2.0f + 20, 24))
            {
                r.act = Act::Come;
                say(r, Emote::Exclaim);
                talk(r, "call");
                audio::cry(r.species);
                follow = true;
            }
            else
            {
                say(r, Emote::Question); // it cannot get there from here
            }
        }

        std::string doing(const Resident& r) const
        {
            switch (r.act)
            {
                case Act::Walk: return "Wandering";
                case Act::Gesture: return "Pleased with itself";
                case Act::Meet: return "Meeting " + r.with;
                case Act::Sleep: return "Asleep";
                case Act::Play: return "Playing with you";
                case Act::Come: return "Coming over";
                case Act::Chase: return "After the ball";
                case Act::Race: return "Racing!";
                case Act::Fire: return "Going to the campfire";
                case Act::Nap: return "Napping at the daycare";
                case Act::Hide: return "Looking for a hiding place";
                case Act::Hidden: return "Hiding somewhere...";
                default: return r.shiny ? "Resting, gleaming" : "Resting";
            }
        }

        void weather()
        {
            if (!rainy)
            {
                return;
            }
            rect(0, 0, TOP_W, H, rgb(30, 40, 60, 50));
            for (int i = 0; i < 70; i++)
            {
                u32 seed  = hashOf(u32(i) * 977u);
                int speed = 5 + seed % 3;
                float x   = float((seed >> 8) % (TOP_W + 60)) - float((tick * speed / 3) % (TOP_W + 60)) * 0.3f;
                float y   = float(((seed >> 16) % H + tick * speed) % (H + 20)) - 10;
                for (int k = 0; k < 6; k++)
                {
                    rect(x - k * 0.5f, y + k, 1, 1, rgb(190, 210, 255, 150));
                }
            }
        }

        void fireflies()
        {
            for (int i = 0; i < 14; i++)
            {
                u32 seed   = hashOf(u32(i) * 7919u + 3);
                float x    = float(seed % TOP_W) + std::sin((tick + i * 40) * 0.02f) * 12;
                float y    = float((seed >> 10) % H) + std::cos((tick + i * 30) * 0.017f) * 8;
                float glow = 0.5f + 0.5f * std::sin((tick + i * 23) * 0.08f);
                if (glow > 0.35f)
                {
                    rect(x, y, 2, 2, rgb(250, 240, 120, u8(255 * glow)));
                }
            }
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> park() { return std::make_unique<Park>(); }
}

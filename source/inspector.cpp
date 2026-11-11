#include "inspector.hpp"
#include "romhack.hpp"
#include "facts.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "ui_ids.hpp"
#include "widgets.hpp"

#include "enums/Species.hpp"
#include "enums/Type.hpp"
#include <algorithm>
#include <cstdio>
#include <vector>

using namespace gfx;

namespace inspector
{
    pksm::Stat statAt(int i)
    {
        static constexpr pksm::Stat order[] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPATK,
            pksm::Stat::SPDEF, pksm::Stat::SPD};
        return order[i];
    }

    const char* statName(int i)
    {
        static constexpr const char* names[] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
        return names[i];
    }

    namespace
    {
        // Nature arrows index Atk Def Spe SpA SpD; our rows are HP Atk Def SpA SpD Spe.
        int natureRow(int natureStat)
        {
            static constexpr int rows[] = {1, 2, 5, 3, 4};
            return natureStat < 0 ? -1 : rows[natureStat];
        }

        u32 baseColor(int base)
        {
            if (base < 50)
            {
                return rgb(0xF0, 0x80, 0x60);
            }
            if (base < 80)
            {
                return rgb(0xF2, 0xC1, 0x4E);
            }
            if (base < 110)
            {
                return rgb(0x8C, 0xD6, 0x5C);
            }
            return rgb(0x42, 0xBF, 0xE8);
        }

        void spriteCard(int species, int form, bool shiny, bool female, bool egg)
        {
            panel(8, 8, 104, 104, T::Well, T::FrameEdge);
            vgrad(9, 70, 102, 41, alpha(T::Well, 0), alpha(T::WallMid, 255));
            // The light pool the sprite stands in.
            rect(26, 92, 68, 4, alpha(T::PoolLight, 40));
            rect(32, 96, 56, 2, alpha(T::PoolLight, 28));
            if (egg)
            {
                imageCentered(ui::egg, 60, 60, 2);
            }
            else if (!front(species, form, shiny, female, 60, 58) && !icon(species, form, 60, 60, 2))
            {
                text(60, 54, "?", T::InkSoft, Center, 2);
            }
            if (shiny && !egg)
            {
                star(100, 20, 6, S::ShinyGold);
            }
        }

        void nameBanner(const std::string& name, int gender)
        {
            panelGrad(118, 8, 274, 22, T::BannerTop, T::BannerBottom, T::NameFrame);
            rect(119, 9, 272, 1, alpha(T::Bright, 40));
            text(126, 13, name, T::Ink, Left, 1, 230);
            genderMark(374, 13, gender);
        }
    }

    void drawStub(int species, int form, bool shiny, const std::string& nickname, int level,
        const std::string& format, const std::string& where)
    {
        housing(0, 0, TOP_W, H);
        spriteCard(species, form, shiny, false, false);
        nameBanner(nickname.empty() ? pkdata::species(species) : nickname, 2);
        char line[96];
        snprintf(line, sizeof(line), "No. %03d  %s", species, pkdata::species(species).c_str());
        text(122, 36, line, T::Value, Left, 1, 200);
        snprintf(line, sizeof(line), "Lv. %d", level);
        text(390, 36, line, T::Ink, Right);
        widgets::chip(122, 52, format.empty() ? "?" : format, T::ToolFill, T::Ink);
        text(122, 70, where, T::InkSoft);
        panel(8, 122, 384, 108, T::PanelFill, T::FrameEdge);
        wrap(20, 136, 360,
            "This Pokémon's format is kept safely in the Bank, but the 3DS port cannot open it. "
            "PKForge on Android can.",
            T::Value);
    }

    namespace
    {
        int current = 0; // the page shown: Info, Stats, Moves, Origin, Legal

        // The summary's page panel and its rows: the caption in a darker column, the value beside it, an
        // optional quiet detail at the right, as MonSummaryView's Row.
        constexpr int PAGE_X = 8, PAGE_Y = 134, PAGE_W = 384, PAGE_H = 102, ROW_H = 13, VALUE_X = 92;

        void pageRow(int& y, const std::string& label, const std::string& value, const std::string& detail = "",
            u32 tone = T::Value)
        {
            rect(PAGE_X + 2, y, VALUE_X - PAGE_X - 6, ROW_H - 1, T::LabelColumn);
            text(PAGE_X + 8, y + 1, label, T::InkSoft, Left, 1,
                VALUE_X - PAGE_X - 12);
            float w = 0;
            if (!detail.empty())
            {
                w = textWidth(detail) + 8;
                text(PAGE_X + PAGE_W - 8, y + 1, detail, T::SubInk, Right, 1, 150);
            }
            text(VALUE_X, y + 1, value, tone, Left, 1, PAGE_X + PAGE_W - 12 - VALUE_X - std::min(w, 158.0f));
            rect(PAGE_X + 4, y + ROW_H - 1, PAGE_W - 8, 1, alpha(T::Grid, 70));
            y += ROW_H;
        }

        void pageSection(int& y, const std::string& title, u32 color)
        {
            widgets::section(PAGE_X + 4, y + 1, PAGE_W - 8, title, color);
            y += 16;
        }

        std::string genderWord(pksm::Gender g)
        {
            return g == pksm::Gender::Female ? "Female" : g == pksm::Gender::Male ? "Male" : "";
        }

        std::string natureLine(const pksm::PKX& pk, std::string& effect)
        {
            int up, down;
            pkdata::natureEffect(u8(pk.nature()), up, down);
            static const char* shortStat[] = {"Atk", "Def", "Spe", "SpA", "SpD"};
            effect = up >= 0 ? std::string("+") + shortStat[up] + " -" + shortStat[down] : "Neutral";
            return pkdata::nature(u8(pk.nature()));
        }

        // What can be judged without the save: limits PKSM-Core knows. One line per finding.
        std::vector<std::pair<std::string, bool>> checks(const pksm::PKX& pk)
        {
            std::vector<std::pair<std::string, bool>> out;
            int ivOver = 0, evTotal = 0, evOver = 0;
            for (int i = 0; i < 6; i++)
            {
                ivOver += pk.iv(statAt(i)) > pk.maxIV();
                evTotal += pk.ev(statAt(i));
                evOver += pk.ev(statAt(i)) > pk.maxEV();
            }
            out.push_back({ivOver ? "IVs above the maximum" : "IVs within limits", ivOver == 0});
            bool evOk = evOver == 0 && (pk.generation() <= pksm::Generation::TWO || evTotal <= 510);
            out.push_back({evOk ? "EVs within limits (" + std::to_string(evTotal) + ")"
                                : "EVs over the limit (" + std::to_string(evTotal) + ")",
                evOk});
            int moves = 0;
            bool twice = false;
            for (int i = 0; i < 4; i++)
            {
                int m = u16(pk.move(i));
                moves += m != 0;
                for (int j = 0; j < i; j++)
                {
                    twice |= m != 0 && m == u16(pk.move(j));
                }
            }
            out.push_back({moves == 0 ? "No moves" : twice ? "A move appears twice" : "Moves set", moves > 0 && !twice});
            bool levelOk = pk.level() >= 1 && pk.level() <= 100;
            out.push_back({levelOk ? "Level " + std::to_string(pk.level()) : "Level out of range", levelOk});
            return out;
        }
    }

    void drawCard(const pksm::PKX* pk, const std::string& where, u8 version)
    {
        housing(0, 0, TOP_W, H);
        if (!pk || u16(pk->species()) == 0)
        {
            image(ui::logo, TOP_W / 2 - 42, 44);
            text(TOP_W / 2, 156, "Empty slot", T::Ink, Center);
            text(TOP_W / 2, 172, where, T::InkSoft, Center);
            return;
        }
        int species = u16(pk->species());
        bool egg    = pk->egg();
        int gender  = u8(pk->gender());
        char line[128];

        panel(6, 6, TOP_W - 12, 206, T::PanelFill, T::Cyan);
        // The gold name plate.
        u32 goldTop = rgb(0xD2, 0xB8, 0x62), goldBottom = rgb(0x8C, 0x74, 0x2E);
        panelGrad(12, 12, TOP_W - 24, 22, goldTop, goldBottom, rgb(0xE8, 0xD8, 0x96));
        std::string name = egg ? "Egg" : pk->nickname();
        text(21, 17, name, rgb(0x2A, 0x20, 0x08), Left, 1, 220);
        float nx = 20 + text(20, 16, name, T::Bright, Left, 1, 220) + 6;
        if (!egg)
        {
            genderMark(nx, 17, gender);
        }
        snprintf(line, sizeof(line), "Lv. %d", pk->level());
        text(TOP_W - 21, 17, line, rgb(0x2A, 0x20, 0x08), Right);
        text(TOP_W - 22, 16, line, T::Bright, Right);

        // The sprite in its well.
        panel(12, 40, 112, 92, T::Well, T::FrameEdge);
        if (egg)
        {
            imageCentered(ui::egg, 68, 86, 2);
        }
        else if (!front(species, pk->alternativeForm(), pk->shiny(), gender == 1, 68, 84))
        {
            icon(species, pk->alternativeForm(), 68, 86, 2);
        }
        if (pk->shiny() && !egg)
        {
            star(114, 50, 5, S::ShinyGold);
        }

        // The facts, in alternating rows: No. and genus, types, nature, ability.
        const facts::Entry* dex = facts::species(species);
        int y = 40;
        auto factRow = [&](const char* label, const std::string& value, int i)
        {
            rect(130, y, TOP_W - 142, 22, i % 2 ? T::PanelFill : T::LabelColumn);
            text(138, y + 5, label, T::InkSoft);
            if (!value.empty())
            {
                text(TOP_W - 18, y + 5, value, T::Ink, Right, 1, 170);
            }
            y += 23;
        };
        snprintf(line, sizeof(line), "No. %03d", species);
        factRow(line, dex && !dex->genus.empty() ? dex->genus : pkdata::species(species), 0);
        factRow("Type", "", 1);
        int t1 = u8(pk->type1()), t2 = u8(pk->type2());
        if (t2 != t1)
        {
            widgets::typeChip(TOP_W - 82, y - 18, 62, t2);
            widgets::typeChip(TOP_W - 148, y - 18, 62, t1);
        }
        else
        {
            widgets::typeChip(TOP_W - 82, y - 18, 62, t1);
        }
        factRow("Nature", pkdata::nature(u8(pk->nature())), 0);
        int ability = u16(pk->ability());
        factRow("Ability", ability ? pkdata::ability(ability) : "-", 1);

        // The Pokédex entry and the game it is from.
        rect(14, 138, TOP_W - 28, 1, alpha(T::Grid, 120));
        if (dex && !dex->text.empty() && !egg)
        {
            wrap(16, 144, TOP_W - 32, dex->text, T::Ink, 1, 4);
            text(TOP_W - 16, 196, dex->game, T::InkSoft, Right, 1, 200);
        }
        else
        {
            text(TOP_W / 2, 160, egg ? "An egg: what hatches is a surprise." : "No Pokédex entry", T::InkSoft, Center);
        }

        // The status line under the card.
        panel(6, 216, TOP_W - 12, 20, T::Deep, T::FrameEdge);
        snprintf(line, sizeof(line), "#%03d %s Lv.%d  -  Gen %d  -  %s", species, pkdata::species(species).c_str(),
            pk->level(), pkdata::genNumber(pk->generation()), where.c_str());
        text(12, 219, line, T::Ink, Left, 1, TOP_W - 24);
        (void)version;
    }

    void page(int p) { current = (p % PAGES + PAGES) % PAGES; }
    int page() { return current; }

    void draw(const pksm::PKX* pk, const std::string& where, u8 version)
    {
        housing(0, 0, TOP_W, H);
        if (!pk || u16(pk->species()) == 0)
        {
            image(ui::logo, TOP_W / 2 - 42, 44);
            text(TOP_W / 2, 156, "Empty slot", T::Ink, Center);
            text(TOP_W / 2, 172, where, T::InkSoft, Center);
            return;
        }

        int species = u16(pk->species());
        int form    = pk->alternativeForm();
        bool egg    = pk->egg();
        int gender  = u8(pk->gender());
        spriteCard(species, form, pk->shiny(), gender == 1, egg);
        if (!egg)
        {
            image(ui::ball, 12, 12); // the ball it was caught in, in the card's corner
        }
        nameBanner(egg ? "Egg" : pk->nickname(), egg ? 2 : gender);

        // No. 003  Venusaur  Lv. 100
        char line[128];
        snprintf(line, sizeof(line), "No. %03d", species);
        float x = 122 + text(122, 36, line, T::Ink) + 8;
        std::string speciesName = pkdata::species(species);
        if (form)
        {
            std::string formName = pkdata::form(version, species, form);
            if (!formName.empty() && formName != speciesName)
            {
                speciesName += " (" + formName + ")";
            }
        }
        x += text(x, 36, speciesName, T::Ink, Left, 1, 190) + 10;
        snprintf(line, sizeof(line), "Lv. %d", pk->level());
        text(x, 36, line, T::Ink);

        // Type plates.
        int t1 = u8(pk->type1()), t2 = u8(pk->type2());
        widgets::typeChip(122, 52, 76, t1);
        if (t2 != t1)
        {
            widgets::typeChip(202, 52, 76, t2);
        }

        // Generation, format and the quick verdict; where it sits.
        auto found = romhack::is(*pk) ? decltype(checks(*pk)){} : checks(*pk);
        bool clean = std::all_of(found.begin(), found.end(), [](auto& c) { return c.second; });
        snprintf(line, sizeof(line), "G%d", pkdata::genNumber(pk->generation()));
        x = 122 + text(122, 70, line, T::SubInk) + 8;
        x += text(x, 70, pkdata::format(*pk), T::Value) + 10;
        if (romhack::is(*pk))
        {
            text(x, 70, "ROM hack: not checked", T::SubInk);
        }
        else if (clean)
        {
            image(ui::check, x, 70);
            text(x + 17, 70, "No problems", S::Legal);
        }
        else
        {
            text(x, 70, "! Check the Legal tab", S::Illegal);
        }
        if (pk->pkrs())
        {
            widgets::chip(330, 69, pk->pkrsDays() ? "PKRS" : "Cured", pk->pkrsDays() ? S::GiftPink : T::Deep, T::Ink);
        }
        text(122, 86, where, T::Value, Left, 1, 268);

        // The tabs, as the second screen's: the open one red.
        static const char* TABS[] = {"Info", "Stats", "Moves", "Origin", "Legal"};
        for (int i = 0; i < PAGES; i++)
        {
            int tx = 8 + i * 77, tw = 74;
            bool on = i == current;
            panelGrad(tx, 114, tw, 17, on ? mix(S::BandInfo, T::Bright, 0.2f) : T::TabTop, on ? S::BandInfo : T::TabBottom,
                on ? mix(S::BandInfo, T::Bright, 0.45f) : T::FrameEdge);
            text(tx + tw / 2, 116, TABS[i], on ? T::Bright : T::InkSoft, Center);
        }

        // The page.
        panel(PAGE_X, PAGE_Y, PAGE_W, PAGE_H, T::PanelFill, T::Cyan);
        clip(PAGE_X + 1, PAGE_Y + 1, PAGE_W - 2, PAGE_H - 2);
        int y = PAGE_Y + 2;
        std::string detail;
        switch (current)
        {
            case 0: // Info
            {
                pageSection(y, "Trainer", S::BandInfo);
                pageRow(y, "OT", pk->otName(), genderWord(pk->otGender()));
                char sid[24] = "";
                if (pk->generation() >= pksm::Generation::THREE)
                {
                    snprintf(sid, sizeof(sid), "SID %05u", unsigned(pk->formatSID()));
                }
                snprintf(line, sizeof(line), "%05u", unsigned(pk->formatTID()));
                pageRow(y, "ID No.", line, sid);
                pageSection(y, "Character", S::BandInfo);
                std::string nature = natureLine(*pk, detail);
                pageRow(y, "Nature", nature, detail);
                int ability = u16(pk->ability());
                pageRow(y, "Ability", ability ? pkdata::ability(ability) : "-");
                int item = pk->heldItem();
                pageRow(y, "Held item", item ? pkdata::item(item) : "None", "", item ? T::Value : T::SubInk);
                break;
            }
            case 1: // Stats
            {
                pageSection(y, "Stats", S::BandStats);
                text(250, y - 15, "IV", T::Bright, Right);
                text(300, y - 15, "EV", T::Bright, Right);
                int up, down;
                pkdata::natureEffect(u8(pk->nature()), up, down);
                const int base[] = {pk->baseHP(), pk->baseAtk(), pk->baseDef(), pk->baseSpa(), pk->baseSpd(), pk->baseSpe()};
                int ivTotal = 0, evTotal = 0;
                for (int i = 0; i < 6; i++, y += 14)
                {
                    rect(PAGE_X + 2, y, 74, 13, T::LabelColumn);
                    u32 labelColor = i == natureRow(up) ? S::CursorRed : i == natureRow(down) ? S::Male : T::InkSoft;
                    text(PAGE_X + 8, y + 1, statName(i), labelColor);
                    snprintf(line, sizeof(line), "%d", pk->stat(statAt(i)));
                    text(118, y + 1, line, T::Ink, Right);
                    rect(126, y + 4, 90, 5, T::Band);
                    rect(126, y + 4, std::max(1, std::min(90, base[i] * 90 / 180)), 5, baseColor(base[i]));
                    int iv = pk->iv(statAt(i)), ev = pk->ev(statAt(i));
                    ivTotal += iv;
                    evTotal += ev;
                    snprintf(line, sizeof(line), "%d", iv);
                    text(250, y + 1, line, iv == pk->maxIV() ? S::ShinyGold : T::Value, Right);
                    snprintf(line, sizeof(line), "%d", ev);
                    text(300, y + 1, line, ev ? T::Value : T::SubInk, Right);
                    if (pk->generation() >= pksm::Generation::SEVEN && pk->hyperTrain(statAt(i)))
                    {
                        text(318, y + 1, "HT", S::ShinyGold);
                    }
                }
                snprintf(line, sizeof(line), "IV %d  EV %d", ivTotal, evTotal);
                text(PAGE_X + PAGE_W - 8, y - 14 * 6 - 15, line, T::Bright, Right);
                break;
            }
            case 2: // Moves
            {
                pageSection(y, "Moves", S::BandMoves);
                for (int i = 0; i < 4; i++, y += 14)
                {
                    int move = u16(pk->move(i));
                    rect(PAGE_X + 2, y, PAGE_W - 4, 13, i % 2 ? T::PanelFill : T::LabelColumn);
                    if (move == 0)
                    {
                        text(PAGE_X + 16, y + 1, "-", T::SubInk);
                        continue;
                    }
                    const facts::Move* fact = facts::move(move);
                    if (fact && fact->type >= 0)
                    {
                        widgets::typeChip(PAGE_X + 6, y + 1, 60, fact->type);
                    }
                    text(PAGE_X + 74, y + 1, pkdata::move(move), T::Value, Left, 1, 220);
                    snprintf(line, sizeof(line), "PP %d/%d", pk->PP(i), pk->maxPP(i));
                    text(PAGE_X + PAGE_W - 8, y + 1, line, T::SubInk, Right);
                }
                pageRow(y, "Ball", pkdata::ball(u8(pk->ball())));
                snprintf(line, sizeof(line), "%d / 255", pk->currentFriendship());
                pageRow(y, "Friendship", line);
                break;
            }
            case 3: // Origin
            {
                pageSection(y, "Origin", S::BandOrigin);
                pageRow(y, "Game", pkdata::game(u8(pk->version())));
                std::string met = pkdata::location(pk->generation(), pk->metLocation());
                snprintf(line, sizeof(line), "Lv. %d", pk->metLevel());
                pageRow(y, pk->eggLocation() ? "Hatched" : "Met", met.empty() ? "-" : met, pk->metLevel() ? line : "");
                pageRow(y, "Language", pkdata::language(pk->language()));
                snprintf(line, sizeof(line), "%s  -  Gen %d", pkdata::format(*pk).c_str(), pkdata::genNumber(pk->generation()));
                pageRow(y, "Format", line);
                char ec[32] = "";
                if (pk->generation() >= pksm::Generation::SIX)
                {
                    snprintf(ec, sizeof(ec), "EC %08lX", (unsigned long)pk->encryptionConstant());
                }
                snprintf(line, sizeof(line), "%08lX", (unsigned long)pk->PID());
                pageRow(y, "PID", line, ec);
                break;
            }
            default: // Legal
            {
                pageSection(y, "Legal", clean ? S::BandLegal : S::BandInfo);
                for (auto& [what, ok] : found)
                {
                    pageRow(y, ok ? "OK" : "Problem", what, "", ok ? S::Legal : S::Illegal);
                }
                text(PAGE_X + PAGE_W / 2, y + 2, "Full check: X > Legalize with PKHeX", T::SubInk, Center);
                break;
            }
        }
        unclip();
    }
}

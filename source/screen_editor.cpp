// The Pokémon editor: PKForge's editor rows on the touch screen, the live inspector on top.
#include "gfx.hpp"
#include "hax.hpp"
#include "inspector.hpp"
#include "profiles.hpp"
#include "pkdata.hpp"
#include "screens.hpp"
#include "widgets.hpp"

#include "enums/Ability.hpp"
#include "enums/Ball.hpp"
#include "enums/GameVersion.hpp"
#include "enums/Move.hpp"
#include "enums/Nature.hpp"
#include "enums/Ribbon.hpp"
#include "pkx/PK6.hpp"
#include "pkx/PK7.hpp"
#include "enums/Species.hpp"
#include "sav/Sav.hpp"
#include "utils/VersionTables.hpp"
#include "utils/i18n.hpp"
#include <algorithm>
#include <cstdio>

using namespace gfx;

namespace
{
    constexpr int PLATE_Y = 29, TAB_Y = 49, LIST_Y = 67, ROW = 18, VISIBLE = 8, ARROW_X = 268;
    const char* TABS[] = {"Main", "Stats", "Moves", "Trainer"};

    struct Row
    {
        std::string label, value;
        std::function<void()> act;       // A or a tap
        std::function<void(int)> adjust; // left / right
        u32 color = T::Value;
        bool header = false; // a section's title strip, as the original's single list
    };

    std::string num(int v) { return std::to_string(v); }

    class Editor : public Screen
    {
    public:
        Editor(std::unique_ptr<pksm::PKX> p, std::string where, pksm::Sav* sav,
            std::function<void(std::unique_ptr<pksm::PKX>)> done, screens::EditorStep step)
            : pk(std::move(p)), where(std::move(where)), sav(sav), done(std::move(done)), step(std::move(step))
        {
            version = sav ? sav->version() : pksm::GameVersion::newestVersion(pk->generation());
        }

        void update(Input& in) override
        {
            // The plate's arrows, as the original's: the previous or next Pokémon of the box (ZL / ZR too).
            if (step && (in.tapped(ARROW_X, PLATE_Y, 23, 18) || (in.down & KEY_ZL)))
            {
                turn(-1);
                return;
            }
            if (step && (in.tapped(ARROW_X + 25, PLATE_Y, 23, 18) || (in.down & KEY_ZR)))
            {
                turn(1);
                return;
            }
            std::vector<Row> rows = build();
            int n = int(rows.size());
            if (in.repeat & KEY_L)
            {
                switchTab((tab + 3) % 4);
                return;
            }
            if (in.repeat & KEY_R)
            {
                switchTab((tab + 1) % 4);
                return;
            }
            for (int i = 0; i < 4; i++)
            {
                if (in.tapped(4 + i * 79, TAB_Y, 76, 16))
                {
                    switchTab(i);
                    return;
                }
            }
            if (in.repeat & KEY_DUP)
            {
                focus = (focus + n - 1) % n;
                if (rows[focus].header)
                {
                    focus = (focus + n - 1) % n; // past the section's title
                }
            }
            if (in.repeat & KEY_DDOWN)
            {
                focus = (focus + 1) % n;
                if (rows[focus].header)
                {
                    focus = (focus + 1) % n;
                }
            }
            if (rows[focus].header)
            {
                focus = std::min(n - 1, focus + 1);
            }
            if (sectionOf(focus) != tab)
            {
                // The top screen and the tabs follow the section the cursor is in.
                tab = sectionOf(focus);
                static constexpr int pages[] = {0, 1, 2, 3};
                inspector::page(pages[tab]);
            }
            if (focus < first)
            {
                first = focus;
            }
            if (focus >= first + VISIBLE)
            {
                first = focus - VISIBLE + 1;
            }
            if (in.touching && in.dragged && in.inside(0, LIST_Y, BOTTOM_W, VISIBLE * ROW))
            {
                drag += in.dy;
                while (drag <= -ROW && first + VISIBLE < n)
                {
                    drag += ROW;
                    first++;
                }
                while (drag >= ROW && first > 0)
                {
                    drag -= ROW;
                    first--;
                }
                focus = std::clamp(focus, first, std::min(n - 1, first + VISIBLE - 1));
            }
            else if (!in.touching)
            {
                drag = 0;
            }
            if ((in.repeat & KEY_DLEFT) && rows[focus].adjust)
            {
                rows[focus].adjust(-1);
                changed = true;
            }
            if ((in.repeat & KEY_DRIGHT) && rows[focus].adjust)
            {
                rows[focus].adjust(1);
                changed = true;
            }
            for (int i = 0; i < std::min(VISIBLE, n - first); i++)
            {
                if (in.tapped(4, LIST_Y + i * ROW, BOTTOM_W - 14, ROW - 1) && !rows[first + i].header)
                {
                    focus = first + i;
                    if (rows[focus].act)
                    {
                        rows[focus].act();
                    }
                    return;
                }
            }
            if ((in.down & KEY_A) && rows[focus].act)
            {
                rows[focus].act();
            }
            else if (in.down & KEY_Y)
            {
                finish();
            }
            else if (in.down & KEY_B)
            {
                if (changed)
                {
                    // The menu closes first, so each answer's own pop leaves the editor.
                    app::menu("Leave the editor?", {{"Keep the changes", [this]() { finish(); }},
                                                       {"Discard them", []() { app::pop(); }},
                                                       {"Keep editing", nullptr}});
                }
                else
                {
                    app::pop();
                }
            }
        }

        void drawTop() override { inspector::draw(pk.get(), where + "  (editing)", u8(version)); }

        void drawBottom() override
        {
            housing(0, 0, BOTTOM_W, H);
            widgets::titleBar(BOTTOM_W, pkdata::game(u8(version)), changed ? "Edited" : "Editor");
            // The name plate, as the original editor's: "Venusaur  Lv.100" in the banner blue.
            int plateW = step ? ARROW_X - 7 : BOTTOM_W - 8;
            panelGrad(4, PLATE_Y, plateW, 18, T::PartySelected, T::ChipTop, T::PartyRim);
            rect(5, PLATE_Y + 1, plateW - 2, 1, alpha(T::Bright, 50));
            char plate[64];
            snprintf(plate, sizeof(plate), "%s  Lv.%d", pk->egg() ? "Egg" : pk->nickname().c_str(), pk->level());
            text(12, PLATE_Y + 3, plate, T::Bright, Left, 1, plateW - 36);
            genderMark(4 + plateW - 16, PLATE_Y + 4, pk->egg() ? 2 : u8(pk->gender()));
            if (step)
            {
                for (int i = 0; i < 2; i++)
                {
                    panel(ARROW_X + i * 25, PLATE_Y, 23, 18, rgb(0x0C, 0x0E, 0x18), T::Ink);
                    chevron(ARROW_X + i * 25 + 9, PLATE_Y + 5, T::Bright, i == 0);
                }
            }
            for (int i = 0; i < 4; i++)
            {
                int x = 4 + i * 79;
                bool on = i == tab;
                panelGrad(x, TAB_Y, 76, 16, on ? mix(S::BandInfo, T::Bright, 0.2f) : T::TabTop, on ? S::BandInfo : T::TabBottom,
                    on ? mix(S::BandInfo, T::Bright, 0.45f) : T::FrameEdge);
                text(x + 38, TAB_Y + 2, TABS[i], on ? T::Bright : T::InkSoft, Center);
            }
            std::vector<Row> rows = build();
            int n = int(rows.size());
            for (int i = 0; i < std::min(VISIBLE, n - first); i++)
            {
                const Row& r = rows[first + i];
                if (r.header)
                {
                    static const u32 bands[] = {S::BandInfo, S::BandStats, S::BandMoves, S::BandOrigin};
                    widgets::section(4, LIST_Y + i * ROW + 3, BOTTOM_W - 14, r.label, bands[sectionOf(first + i)]);
                    continue;
                }
                widgets::row(4, LIST_Y + i * ROW, BOTTOM_W - 14, ROW - 1, r.label, r.value, first + i == focus,
                    bool(r.act), r.color);
            }
            widgets::scrollbar(BOTTOM_W - 8, LIST_Y, VISIBLE * ROW, first, VISIBLE, n);
            widgets::footer(BOTTOM_W, {{"A", "Edit"}, {"LR", "Section"}, {"Y", "Done"}, {"B", "Back"}});
        }

    private:
        std::unique_ptr<pksm::PKX> pk;
        std::string where;
        pksm::Sav* sav;
        std::function<void(std::unique_ptr<pksm::PKX>)> done;
        screens::EditorStep step;
        pksm::GameVersion version;
        int tab = 0, focus = 1, first = 0, drag = 0;
        int starts[4] = {0, 0, 0, 0};
        bool changed = false;

        int gen() const { return pkdata::genNumber(pk->generation()); }

        void switchTab(int t)
        {
            tab   = t;
            build(); // where the sections start
            first = starts[t];
            focus = starts[t] + 1;
            // The top screen follows: Main > Info, Stats, Moves, Trainer > Origin.
            static constexpr int pages[] = {0, 1, 2, 3};
            inspector::page(pages[t]);
        }

        // To the box's previous or next Pokémon; changes made here are kept first, as the original does.
        void turn(int delta)
        {
            std::unique_ptr<pksm::PKX> next;
            std::string nextWhere;
            std::function<void(std::unique_ptr<pksm::PKX>)> nextDone;
            if (changed)
            {
                pk->refreshChecksum();
                if (pk->isParty())
                {
                    pk->updatePartyData();
                }
                if (done)
                {
                    done(pk->clone());
                }
                app::toast("Changes kept");
            }
            if (!step(delta, next, nextWhere, nextDone) || !next)
            {
                app::toast("No other Pokémon here");
                return;
            }
            pk      = std::move(next);
            where   = std::move(nextWhere);
            done    = std::move(nextDone);
            changed = false;
            switchTab(tab);
            if (!sav)
            {
                version = pksm::GameVersion::newestVersion(pk->generation());
            }
        }

        void finish()
        {
            pk->refreshChecksum();
            if (pk->isParty())
            {
                pk->updatePartyData();
            }
            auto cb = done;
            auto result = std::move(pk);
            app::pop();
            if (cb)
            {
                cb(std::move(result));
            }
        }

        // Edits go through here so the checksum and party stats follow.
        void edited()
        {
            changed = true;
            if (pk->isParty())
            {
                pk->updatePartyData();
            }
        }

        void pick(const std::string& title, std::vector<screens::PickItem> items, int current,
            std::function<void(int)> apply, screens::PickPreview preview = screens::PickPreview::None)
        {
            app::push(screens::picker(title, std::move(items), current,
                [this, apply](int id)
                {
                    apply(id);
                    edited();
                },
                preview));
        }

        void numberRow(std::vector<Row>& rows, const std::string& label, int value, int min, int max,
            std::function<void(int)> set, int step = 1)
        {
            rows.push_back({label, num(value),
                [this, label, value, min, max, set]()
                {
                    int v = value;
                    if (app::numpad(label, v, min, max))
                    {
                        set(v);
                        edited();
                    }
                },
                [value, min, max, set, step](int d) { set(std::clamp(value + d * step, min, max)); }});
        }

        int abilityIndex() const
        {
            for (int i = 0; i < 3; i++)
            {
                if (pk->abilities(i) == pk->ability())
                {
                    return i;
                }
            }
            return 0;
        }

        void setSpecies(int id)
        {
            pksm::Species s{u16(id)};
            bool renamed = pk->nicknamed();
            int ability  = abilityIndex();
            pk->species(s);
            pk->alternativeForm(0);
            if (!renamed)
            {
                pk->nickname(i18n::species(pk->language(), s));
            }
            if (gen() >= 3)
            {
                pk->setAbility(u8(ability));
            }
            u8 ratio = pk->genderType();
            if (ratio == 255)
            {
                pk->gender(pksm::Gender::Genderless);
            }
            else if (ratio == 254)
            {
                pk->gender(pksm::Gender::Female);
            }
            else if (ratio == 0 || pk->gender() == pksm::Gender::Genderless)
            {
                pk->gender(pksm::Gender::Male);
            }
        }

        // One list, as the original's editor: every section under its title strip; the tabs jump between them.
        std::vector<Row> build()
        {
            std::vector<Row> rows;
            static const char* titles[] = {"Pokémon", "Stats", "Moves", "Trainer and origin"};
            for (int t = 0; t < 4; t++)
            {
                starts[t] = int(rows.size());
                Row h;
                h.label  = titles[t];
                h.header = true;
                rows.push_back(h);
                switch (t)
                {
                    case 0: mainRows(rows); break;
                    case 1: statRows(rows); break;
                    case 2: moveRows(rows); break;
                    default: trainerRows(rows); break;
                }
            }
            focus = std::clamp(focus, 0, std::max(0, int(rows.size()) - 1));
            return rows;
        }

        int sectionOf(int row) const
        {
            int t = 0;
            for (int i = 0; i < 4; i++)
            {
                if (row >= starts[i])
                {
                    t = i;
                }
            }
            return t;
        }

        void mainRows(std::vector<Row>& rows)
        {
            int species = u16(pk->species());
            rows.push_back({"Species", pkdata::species(species), [this, species]()
                {
                    std::vector<screens::PickItem> items;
                    for (auto& s : hax::species(version))
                    {
                        items.push_back({int(u16(s)), pkdata::species(u16(s)), ""});
                    }
                    pick("Species", std::move(items), species, [this](int id) { setSpecies(id); },
                        screens::PickPreview::Species);
                }});
            int forms = pksm::VersionTables::formCount(version, pk->species());
            if (forms > 1)
            {
                rows.push_back({"Form", pkdata::form(u8(version), species, pk->alternativeForm()), [this, forms, species]()
                    {
                        std::vector<screens::PickItem> items;
                        for (int f = 0; f < forms; f++)
                        {
                            items.push_back({f, pkdata::form(u8(version), species, f), ""});
                        }
                        pick("Form", std::move(items), pk->alternativeForm(),
                            [this](int f) { pk->alternativeForm(u16(f)); });
                    }});
            }
            rows.push_back({"Nickname", pk->nickname(), [this]()
                {
                    std::string name = pk->nickname();
                    int max          = gen() <= 2 ? 10 : gen() <= 5 ? 10 : 12;
                    if (app::keyboard("Nickname", name, max))
                    {
                        pk->nickname(name);
                        pk->nicknamed(name != pkdata::species(u16(pk->species())));
                        edited();
                    }
                }});
            numberRow(rows, "Level", pk->level(), 1, 100, [this](int v) { pk->level(u8(v)); });
            int up, down;
            pkdata::natureEffect(u8(pk->nature()), up, down);
            rows.push_back({"Nature", pkdata::nature(u8(pk->nature())), [this]()
                {
                    std::vector<screens::PickItem> items;
                    for (int i = 0; i < 25; i++)
                    {
                        items.push_back({i, pkdata::nature(i), ""});
                    }
                    pick("Nature", std::move(items), u8(pk->nature()), [this](int id) { pk->nature(pksm::Nature{u8(id)}); });
                }});
            if (gen() >= 3)
            {
                rows.push_back({"Ability", pkdata::ability(u16(pk->ability())), [this]()
                    {
                        std::vector<screens::PickItem> items;
                        for (int i = 0; i < 3; i++)
                        {
                            int a = u16(pk->abilities(i));
                            bool dup = false;
                            for (auto& it : items)
                            {
                                dup |= it.name == pkdata::ability(a);
                            }
                            if (a && !dup)
                            {
                                items.push_back({i, pkdata::ability(a) + (i == 2 ? "  (Hidden)" : ""), ""});
                            }
                        }
                        pick("Ability", std::move(items), abilityIndex(), [this](int i) { pk->setAbility(u8(i)); });
                    }});
            }
            if (gen() >= 2)
            {
                int item = pk->heldItem();
                rows.push_back({"Held item", item ? pkdata::item(item) : "(None)", [this, item]()
                    {
                        std::vector<screens::PickItem> items{{0, "(None)", ""}};
                        for (int id : hax::items(version))
                        {
                            if (id && !pkdata::item(id).empty())
                            {
                                items.push_back({id, pkdata::item(id), ""});
                            }
                        }
                        pick("Held item", std::move(items), item, [this](int id) { pk->heldItem(u16(id)); },
                            screens::PickPreview::Item);
                    }});
            }
            if (gen() >= 2 && pk->genderType() != 255 && pk->genderType() != 254 && pk->genderType() != 0)
            {
                bool female = pk->gender() == pksm::Gender::Female;
                rows.push_back({"Gender", female ? "Female" : "Male", [this, female]()
                    {
                        pk->gender(female ? pksm::Gender::Male : pksm::Gender::Female);
                        edited();
                    },
                    nullptr, female ? S::Female : S::Male});
            }
            rows.push_back({"Shiny", pk->shiny() ? "Yes" : "No", [this]()
                {
                    pk->shiny(!pk->shiny());
                    edited();
                },
                nullptr, pk->shiny() ? S::ShinyGold : T::Value});
            if (gen() >= 3)
            {
                rows.push_back({"Ball", pkdata::ball(u8(pk->ball())), [this]()
                    {
                        std::vector<screens::PickItem> items;
                        for (auto& b : hax::balls(version))
                        {
                            items.push_back({int(u8(b)), pkdata::ball(u8(b)), ""});
                        }
                        pick("Ball", std::move(items), u8(pk->ball()), [this](int id) { pk->ball(pksm::Ball{u8(id)}); });
                    }});
            }
            if (gen() >= 2)
            {
                numberRow(rows, "Friendship", pk->currentFriendship(), 0, 255,
                    [this](int v) { pk->currentFriendship(u8(v)); });
            }
            if (gen() >= 3)
            {
                rows.push_back({"Language", pkdata::language(pk->language()), [this]()
                    {
                        std::vector<screens::PickItem> items;
                        for (int l : {1, 2, 3, 4, 5, 7, 8, 9, 10})
                        {
                            items.push_back({l, pkdata::language(pksm::Language(l)), ""});
                        }
                        pick("Language", std::move(items), int(pk->language()),
                            [this](int l) { pk->language(pksm::Language(l)); });
                    }});
            }
            if (gen() >= 2)
            {
                rows.push_back({"Egg", pk->egg() ? "Yes" : "No", [this]()
                    {
                        pk->egg(!pk->egg());
                        edited();
                    }});
                std::string pkrs = !pk->pkrs() ? "None" : pk->pkrsDays() ? "Infected" : "Cured";
                rows.push_back({"Pokérus", pkrs, [this]()
                    {
                        if (!pk->pkrs())
                        {
                            pk->pkrsStrain(1);
                            pk->pkrsDays(3);
                        }
                        else if (pk->pkrsDays())
                        {
                            pk->pkrsDays(0);
                        }
                        else
                        {
                            pk->pkrs(0);
                        }
                        edited();
                    }});
            }
            awardRows(rows);
        }

        // The original's Awards and Cosmetics editors: ribbons, box markings, contest stats, friendship,
        // affection and trainer memories, each only where the Pokemon's format keeps it.
        void awardRows(std::vector<Row>& rows)
        {
            if (gen() >= 3)
            {
                int count = 0;
                for (u32 i = 0; i < 160; i++)
                {
                    pksm::Ribbon r{i};
                    count += pk->hasRibbon(r) && pk->ribbon(r);
                }
                rows.push_back({"Ribbons", count ? num(count) + " ribbons" : "None", [this]() { ribbonMenu(0); }});
                rows.push_back({"Markings", markingText(), [this]() { markingMenu(0); }});
                static const char* contest[] = {"Cool", "Beauty", "Cute", "Smart", "Tough", "Sheen"};
                for (u8 i = 0; i < 6; i++)
                {
                    numberRow(rows, contest[i], pk->contest(i), 0, 255,
                        [this, i](int v) { pk->contest(i, u8(v)); }, 10);
                }
            }
            if (gen() >= 6)
            {
                numberRow(rows, "Friendship (handler)", pk->htFriendship(), 0, 255,
                    [this](int v) { pk->htFriendship(u8(v)); }, 10);
            }
            if (gen() == 6)
            {
                auto* p = static_cast<pksm::PK6*>(pk.get());
                numberRow(rows, "Affection (OT)", p->otAffection(), 0, 255, [p](int v) { p->otAffection(u8(v)); }, 10);
                memoryRows(rows, p);
            }
            else if (gen() == 7)
            {
                memoryRows(rows, static_cast<pksm::PK7*>(pk.get()));
            }
        }

        template <typename P>
        void memoryRows(std::vector<Row>& rows, P* p)
        {
            numberRow(rows, "OT memory", p->otMemory(), 0, 255, [p](int v) { p->otMemory(u8(v)); });
            numberRow(rows, "OT memory feeling", p->otFeeling(), 0, 24, [p](int v) { p->otFeeling(u8(v)); });
            numberRow(rows, "OT memory intensity", p->otIntensity(), 0, 7, [p](int v) { p->otIntensity(u8(v)); });
            numberRow(rows, "OT memory detail", p->otTextVar(), 0, 65535, [p](int v) { p->otTextVar(u16(v)); });
            numberRow(rows, "Handler memory", p->htMemory(), 0, 255, [p](int v) { p->htMemory(u8(v)); });
            numberRow(rows, "Handler memory feeling", p->htFeeling(), 0, 24, [p](int v) { p->htFeeling(u8(v)); });
            numberRow(rows, "Handler memory intensity", p->htIntensity(), 0, 7, [p](int v) { p->htIntensity(u8(v)); });
            numberRow(rows, "Handler memory detail", p->htTextVar(), 0, 65535, [p](int v) { p->htTextVar(u16(v)); });
        }

        // Every ribbon this format keeps, On or Off; a pick toggles it and the list comes back at that row.
        void ribbonMenu(int at)
        {
            std::vector<screens::PickItem> items{{-2, "Give every ribbon", ""}, {-1, "Clear every ribbon", ""}};
            for (u32 i = 0; i < 160; i++)
            {
                pksm::Ribbon r{i};
                if (pk->hasRibbon(r))
                {
                    std::string name = i18n::ribbon(pkdata::lang, r);
                    items.push_back({int(i), (pk->ribbon(r) ? "[On]  " : "[Off]  ") + (name.empty() ? "Ribbon " + num(int(i)) : name), ""});
                }
            }
            app::push(screens::picker("Ribbons", std::move(items), at,
                [this](int id)
                {
                    for (u32 i = 0; i < 160; i++)
                    {
                        pksm::Ribbon r{i};
                        if (!pk->hasRibbon(r))
                        {
                            continue;
                        }
                        if (id < 0)
                        {
                            pk->ribbon(r, id == -2);
                        }
                        else if (int(i) == id)
                        {
                            pk->ribbon(r, !pk->ribbon(r));
                        }
                    }
                    edited();
                    ribbonMenu(id);
                }));
        }

        // Box markings: Gen 3 has 4 shapes, Gen 4-6 six, Gen 7 six in blue or pink (2 bits each).
        int markCount() const { return gen() == 3 ? 4 : 6; }
        int mark(int i) const
        {
            u16 v = pk->markValue();
            return gen() >= 7 ? (v >> (2 * i)) & 3 : (v >> i) & 1;
        }
        void mark(int i, int value)
        {
            u16 v = pk->markValue();
            if (gen() >= 7)
            {
                v = u16((v & ~(3 << (2 * i))) | (value << (2 * i)));
            }
            else
            {
                v = u16((v & ~(1 << i)) | ((value & 1) << i));
            }
            pk->markValue(v);
        }
        std::string markingText() const
        {
            static const char* shapes[] = {"Circle", "Triangle", "Square", "Heart", "Star", "Diamond"};
            std::string out;
            for (int i = 0; i < markCount(); i++)
            {
                if (mark(i))
                {
                    out += out.empty() ? shapes[i] : std::string(", ") + shapes[i];
                }
            }
            return out.empty() ? "None" : out;
        }
        void markingMenu(int at)
        {
            static const char* shapes[]  = {"Circle", "Triangle", "Square", "Heart", "Star", "Diamond"};
            static const char* colours[] = {"Off", "Blue", "Pink"};
            std::vector<screens::PickItem> items;
            for (int i = 0; i < markCount(); i++)
            {
                items.push_back({i, std::string(shapes[i]) + ": " + (gen() >= 7 ? colours[std::min(mark(i), 2)] : (mark(i) ? "On" : "Off")), ""});
            }
            app::push(screens::picker("Markings", std::move(items), at,
                [this](int id)
                {
                    mark(id, (mark(id) + 1) % (gen() >= 7 ? 3 : 2));
                    edited();
                    markingMenu(id);
                }));
        }

        // A date as three numbers; false when cancelled.
        bool askDate(const std::string& title, ::Date& d)
        {
            int day = d.day() ? d.day() : 1, month = d.month() ? d.month() : 1;
            int year = d.year() >= 2000 ? int(d.year()) : 2000;
            if (!app::numpad(title + ": year", year, 2000, 2099) || !app::numpad(title + ": month", month, 1, 12) ||
                !app::numpad(title + ": day", day, 1, 31))
            {
                return false;
            }
            d = ::Date{u8(day), u8(month), u32(year)};
            return true;
        }
        static std::string dateText(const ::Date& d)
        {
            if (!d.month())
            {
                return "None";
            }
            char b[16];
            snprintf(b, sizeof(b), "%04u-%02u-%02u", unsigned(d.year()), unsigned(d.month()), unsigned(d.day()));
            return b;
        }

        void statRows(std::vector<Row>& rows)
        {
            int maxIv = pk->maxIV();
            for (int i = 0; i < 6; i++)
            {
                pksm::Stat st = inspector::statAt(i);
                numberRow(rows, std::string(inspector::statName(i)) + " IV", pk->iv(st), 0, maxIv,
                    [this, st](int v) { pk->iv(st, u8(v)); });
            }
            for (int i = 0; i < 6; i++)
            {
                pksm::Stat st = inspector::statAt(i);
                int total = 0;
                for (int k = 0; k < 6; k++)
                {
                    total += pk->ev(inspector::statAt(k));
                }
                int current = pk->ev(st);
                // HaX lifts the total; each EV still fits its field.
                int room = hax::on() ? int(pk->maxEV())
                                     : std::min<int>(pk->maxEV(), int(pk->maxEVTotal()) - (total - current));
                numberRow(rows, std::string(inspector::statName(i)) + " EV", current, 0, std::max(current, room),
                    [this, st](int v) { pk->ev(st, u16(v)); }, gen() <= 2 ? 256 : 4);
            }
            rows.push_back({"Max every IV", "", [this]()
                {
                    for (int i = 0; i < 6; i++)
                    {
                        pk->iv(inspector::statAt(i), pk->maxIV());
                    }
                    edited();
                }});
            rows.push_back({"Clear every EV", "", [this]()
                {
                    for (int i = 0; i < 6; i++)
                    {
                        pk->ev(inspector::statAt(i), 0);
                    }
                    edited();
                }});
            if (gen() >= 7)
            {
                bool all = true;
                for (int i = 0; i < 6; i++)
                {
                    all &= pk->hyperTrain(inspector::statAt(i));
                }
                rows.push_back({"Hyper Training", all ? "All stats" : "Off", [this, all]()
                    {
                        for (int i = 0; i < 6; i++)
                        {
                            pk->hyperTrain(inspector::statAt(i), !all);
                        }
                        edited();
                    }});
            }
        }

        void moveRows(std::vector<Row>& rows)
        {
            for (int i = 0; i < 4; i++)
            {
                int m = u16(pk->move(i));
                char label[16];
                snprintf(label, sizeof(label), "Move %d", i + 1);
                rows.push_back({label, m ? pkdata::move(m) : "-", [this, i, m]()
                    {
                        std::vector<screens::PickItem> items{{0, "(None)", ""}};
                        for (auto& mv : hax::moves(version))
                        {
                            int id = u16(mv);
                            if (id)
                            {
                                items.push_back({id, pkdata::move(id), ""});
                            }
                        }
                        pick("Move", std::move(items), m, [this, i](int id)
                            {
                                pk->move(u8(i), pksm::Move{u16(id)});
                                pk->healPP(u8(i));
                            },
                            screens::PickPreview::Move);
                    }});
            }
            for (int i = 0; i < 4; i++)
            {
                if (!u16(pk->move(i)))
                {
                    continue;
                }
                char label[16];
                snprintf(label, sizeof(label), "PP Ups %d", i + 1);
                numberRow(rows, label, pk->PPUp(i), 0, 3, [this, i](int v)
                    {
                        pk->PPUp(u8(i), u8(v));
                        pk->healPP(u8(i));
                    });
            }
            if (gen() >= 6)
            {
                for (int i = 0; i < 4; i++)
                {
                    int m = u16(pk->relearnMove(i));
                    char label[16];
                    snprintf(label, sizeof(label), "Relearn %d", i + 1);
                    rows.push_back({label, m ? pkdata::move(m) : "-", [this, i, m]()
                        {
                            std::vector<screens::PickItem> items{{0, "(None)", ""}};
                            for (auto& mv : hax::moves(version))
                            {
                                int id = u16(mv);
                                if (id)
                                {
                                    items.push_back({id, pkdata::move(id), ""});
                                }
                            }
                            pick("Relearn move", std::move(items), m,
                                [this, i](int id) { pk->relearnMove(u8(i), pksm::Move{u16(id)}); },
                                screens::PickPreview::Move);
                        }});
                }
            }
            rows.push_back({"Restore PP", "", [this]()
                {
                    pk->healPP();
                    edited();
                }});
        }

        void trainerRows(std::vector<Row>& rows)
        {
            rows.push_back({"Trainer profile", "Use a kept one", [this]()
                {
                    profiles::pick("Original trainer", [this](const profiles::Profile& p)
                        {
                            pk->otName(p.name);
                            pk->TID(p.tid);
                            if (gen() >= 3)
                            {
                                pk->SID(p.sid);
                            }
                            if (gen() >= 2)
                            {
                                pk->otGender(p.female ? pksm::Gender::Female : pksm::Gender::Male);
                            }
                            edited();
                        });
                }});
            rows.push_back({"OT name", pk->otName(), [this]()
                {
                    std::string name = pk->otName();
                    if (app::keyboard("Original trainer", name, gen() <= 5 ? 7 : 12))
                    {
                        pk->otName(name);
                        edited();
                    }
                }});
            if (gen() >= 2)
            {
                bool female = pk->otGender() == pksm::Gender::Female;
                rows.push_back({"OT gender", female ? "Female" : "Male", [this, female]()
                    {
                        pk->otGender(female ? pksm::Gender::Male : pksm::Gender::Female);
                        edited();
                    },
                    nullptr, female ? S::Female : S::Male});
            }
            numberRow(rows, "ID No.", pk->TID(), 0, 65535, [this](int v) { pk->TID(u16(v)); });
            if (gen() >= 3)
            {
                numberRow(rows, "Secret ID", pk->SID(), 0, 65535, [this](int v) { pk->SID(u16(v)); });
                rows.push_back({"Origin game", pkdata::game(u8(pk->version())), [this]()
                    {
                        std::vector<screens::PickItem> items;
                        for (int v = 1; v <= 52; v++)
                        {
                            std::string name = i18n::game(pkdata::lang, pksm::GameVersion{u8(v)});
                            if (!name.empty() && name != "-")
                            {
                                items.push_back({v, name, ""});
                            }
                        }
                        pick("Origin game", std::move(items), u8(pk->version()),
                            [this](int v) { pk->version(pksm::GameVersion{u8(v)}); });
                    }});
                numberRow(rows, "Met level", pk->metLevel(), 0, 100, [this](int v) { pk->metLevel(u8(v)); });
                std::string place = pkdata::location(pk->generation(), pk->metLocation());
                rows.push_back({"Met location", place.empty() ? num(pk->metLocation()) : place, [this]()
                    {
                        std::vector<screens::PickItem> items;
                        for (auto& [id, name] : i18n::rawLocations(pkdata::lang, pk->generation()))
                        {
                            if (!name.empty())
                            {
                                items.push_back({id, name, ""});
                            }
                        }
                        pick("Met location", std::move(items), pk->metLocation(),
                            [this](int id) { pk->metLocation(u16(id)); });
                    }});
                rows.push_back({"Fateful", pk->fatefulEncounter() ? "Yes" : "No", [this]()
                    {
                        pk->fatefulEncounter(!pk->fatefulEncounter());
                        edited();
                    }});
            }
            if (gen() >= 4)
            {
                // The original's Met / origin editor: met date, and where and when the egg was received.
                rows.push_back({"Met date", dateText(pk->metDate()), [this]()
                    {
                        ::Date d = pk->metDate();
                        if (askDate("Met date", d))
                        {
                            pk->metDate(d);
                            edited();
                        }
                    }});
                std::string eggPlace = pk->eggLocation() ? pkdata::location(pk->generation(), pk->eggLocation()) : "None";
                rows.push_back({"Egg location", eggPlace.empty() ? num(pk->eggLocation()) : eggPlace, [this]()
                    {
                        std::vector<screens::PickItem> items{{0, "None (not from an egg)", ""}};
                        for (auto& [id, name] : i18n::rawLocations(pkdata::lang, pk->generation()))
                        {
                            if (!name.empty() && id)
                            {
                                items.push_back({id, name, ""});
                            }
                        }
                        pick("Egg location", std::move(items), pk->eggLocation(), [this](int id)
                            {
                                pk->eggLocation(u16(id));
                                if (!id)
                                {
                                    pk->eggDate(::Date{0, 0, 2000});
                                }
                            });
                    }});
                if (pk->eggLocation())
                {
                    rows.push_back({"Egg date", dateText(pk->eggDate()), [this]()
                        {
                            ::Date d = pk->eggDate();
                            if (askDate("Egg date", d))
                            {
                                pk->eggDate(d);
                                edited();
                            }
                        }});
                }
            }
        }
    };
}

namespace screens
{
    std::unique_ptr<Screen> editor(std::unique_ptr<pksm::PKX> pk, std::string where, pksm::Sav* sav,
        std::function<void(std::unique_ptr<pksm::PKX>)> done, EditorStep step)
    {
        return std::make_unique<Editor>(std::move(pk), std::move(where), sav, std::move(done), std::move(step));
    }
}

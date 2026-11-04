#include "world.hpp"
#include "app.hpp"
#include "pkdata.hpp"

#include "sav/Item.hpp"
#include "sav/Sav4.hpp"
#include "utils/ValueConverter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace world
{
    namespace
    {
        enum class Game
        {
            None,
            RS,
            E,
            FRLG,
            DP,
            Pt,
            HGSS,
            RB,  // Red, Green, Blue (Virtual Console)
            Y,   // Yellow
            C,   // Crystal
            BW,  // Black, White
            ORAS,
            SM,
            USUM
        };

        Game gameOf(const pksm::Sav& sav)
        {
            switch (u8(sav.version()))
            {
                case 1: case 2: return Game::RS;
                case 3: return Game::E;
                case 4: case 5: return Game::FRLG;
                case 10: case 11: return Game::DP;
                case 12: return Game::Pt;
                case 7: case 8: return Game::HGSS;
                case 35: case 36: case 37: return Game::RB;
                case 38: return Game::Y;
                case 41: return Game::C;
                case 20: case 21: return Game::BW;
                case 26: case 27: return Game::ORAS;
                case 30: case 31: return Game::SM;
                case 32: case 33: return Game::USUM;
                default: return Game::None;
            }
        }

        u8* raw(pksm::Sav& sav) { return sav.rawData().get(); }

        u32 rd32(const u8* p) { return u32(p[0]) | u32(p[1]) << 8 | u32(p[2]) << 16 | u32(p[3]) << 24; }
        u16 rd16(const u8* p) { return u16(p[0] | p[1] << 8); }
        void wr32(u8* p, u32 v)
        {
            for (int i = 0; i < 4; i++)
            {
                p[i] = u8(v >> (8 * i));
            }
        }
        void wr16(u8* p, u16 v)
        {
            p[0] = u8(v);
            p[1] = u8(v >> 8);
        }

        // Gen 3: the sector holding a block id in the newest of the two save slots; the save's own
        // checksums are redone by PKSM-Core when it is written.
        u8* gen3Sector(pksm::Sav& sav, int id)
        {
            u8* d   = raw(sav);
            int base = rd32(d + 0xFFC) >= rd32(d + 0xE000 + 0xFFC) ? 0 : 0xE000;
            for (int i = 0; i < 14; i++)
            {
                u8* sector = d + base + i * 0x1000;
                if (rd16(sector + 0xFF4) == id)
                {
                    return sector;
                }
            }
            return nullptr;
        }

        // An offset in the large block (sectors 1-4, 0xF80 used bytes each).
        u8* gen3Large(pksm::Sav& sav, int offset)
        {
            u8* sector = gen3Sector(sav, 1 + offset / 0xF80);
            return sector ? sector + offset % 0xF80 : nullptr;
        }

        // Only called for Diamond to SoulSilver, whose saves are always Sav4 (built without RTTI).
        u8* gen4General(pksm::Sav& sav)
        {
            auto* s4 = static_cast<pksm::Sav4*>(&sav);
            return s4->getGBO() >= 0 ? raw(sav) + s4->getGBO() : nullptr;
        }

        // ---------------------------------------------------------------- Ruby/Sapphire/Emerald clock

        struct Clock
        {
            u16 day;
            u8 hour, minute, second;
        };

        Clock readClock(const u8* p) { return {rd16(p), p[2], p[3], p[4]}; }
        void writeClock(u8* p, const Clock& c)
        {
            wr16(p, c.day);
            p[2] = c.hour;
            p[3] = c.minute;
            p[4] = c.second;
        }
        std::string clockText(const Clock& c)
        {
            char t[48];
            snprintf(t, sizeof(t), "day %u, %02u:%02u:%02u", c.day, c.hour, c.minute, c.second);
            return t;
        }

        void clockMenu(std::shared_ptr<OpenSave> save)
        {
            u8* small = gen3Sector(*save->sav, 0);
            if (!small)
            {
                app::message("Clock repair", "The save's trainer block was not found.");
                return;
            }
            Clock initial = readClock(small + 0x98), elapsed = readClock(small + 0xA0);
            std::vector<app::Option> o;
            o.push_back({"Reset the clock", [save, small]()
                {
                    writeClock(small + 0x98, {});
                    writeClock(small + 0xA0, {});
                    save->dirty = true;
                    app::message("Clock repair", "Both clock values are zero: the game counts time again from the "
                                                 "cartridge's clock. Write the save to keep it.");
                },
                "Initial " + clockText(initial) + ", elapsed " + clockText(elapsed)});
            o.push_back({"Berry fix", [save, small, elapsed]()
                {
                    Clock e = elapsed;
                    e.day   = u16(std::max(2 * 366 + 2, int(e.day))); // PKHeX SAV_RTC3: past the berry-fix date
                    writeClock(small + 0xA0, e);
                    save->dirty = true;
                    app::message("Clock repair", "Elapsed time set past the 2002 date bug: berries grow again. Write "
                                                 "the save to keep it.");
                },
                "For berries that stopped growing (the dry-battery bug)."});
            o.push_back({"Close", []() {}});
            app::menu("Clock repair", std::move(o));
        }

        // ---------------------------------------------------------------- Gen 3 roamer

        int roamerOffset(Game g) { return g == Game::RS ? 0x3144 : g == Game::E ? 0x31DC : 0x30D0; }

        u16 nationalFromGen3(u16 internal)
        {
            // Only the roamers matter here: Raikou, Entei, Suicune keep their numbers; Latias and Latios do not.
            return internal == 407 ? 380 : internal == 408 ? 381 : internal;
        }

        void roamerMenu(std::shared_ptr<OpenSave> save, Game g)
        {
            u8* r = gen3Large(*save->sav, roamerOffset(g));
            if (!r)
            {
                app::message("Roaming legendary", "The save's world block was not found.");
                return;
            }
            u16 species = nationalFromGen3(rd16(r + 8));
            // The game picks the species early but only sets it free (level, IVs) after the Elite Four.
            if (species == 0 || r[0xC] == 0)
            {
                app::message("Roaming legendary", species ? pkdata::species(species) + " is not roaming yet: the game "
                                                                                    "lets it go after the Elite Four."
                                                          : "No legendary is roaming in this save yet.");
                return;
            }
            u32 iv = rd32(r), pid = rd32(r + 4);
            u16 tid = save->sav->TID(), sid = save->sav->SID();
            bool shiny = ((tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) & 0xFFFF) < 8;
            char detail[120];
            snprintf(detail, sizeof(detail), "%s, level %u, %s, IVs %u/%u/%u/%u/%u/%u%s",
                pkdata::species(species).c_str(), r[0xC], r[0x13] == 1 ? "roaming" : "gone",
                unsigned(iv & 31), unsigned(iv >> 5 & 31), unsigned(iv >> 10 & 31), unsigned(iv >> 20 & 31),
                unsigned(iv >> 25 & 31), unsigned(iv >> 15 & 31), shiny ? ", shiny" : "");
            std::vector<app::Option> o;
            o.push_back({"Set it roaming again", [save, r]()
                {
                    r[0x13] = 1;
                    r[0x0D] = 0; // no status
                    save->dirty = true;
                    app::message("Roaming legendary", "It roams again (if you knocked it out before catching it). "
                                                      "Write the save to keep it.");
                },
                detail});
            o.push_back({"Perfect IVs", [save, r, g]()
                {
                    wr32(r, (rd32(r) & 0xC0000000) | 0x3FFFFFFF);
                    save->dirty = true;
                    app::message("Roaming legendary", g == Game::E ? "All IVs 31. Write the save to keep it."
                                                                   : "All IVs 31. Ruby, Sapphire, FireRed and LeafGreen "
                                                                     "keep only part of a roamer's IVs when you meet it "
                                                                     "(a known game bug). Write the save to keep it.");
                }});
            o.push_back({"Make it shiny", [save, r, tid, sid]()
                {
                    u16 high = u16(rand());
                    u16 low  = u16(high ^ tid ^ sid);
                    wr32(r + 4, u32(high) << 16 | low);
                    save->dirty = true;
                    app::message("Roaming legendary", "Its PID is now shiny for this trainer (its nature changes with "
                                                      "it). Write the save to keep it.");
                }});
            o.push_back({"Close", []() {}});
            app::menu("Roaming legendary", std::move(o));
        }

        // ---------------------------------------------------------------- HeartGold/SoulSilver

        void apricornMenu(std::shared_ptr<OpenSave> save)
        {
            u8* general = gen4General(*save->sav);
            if (!general)
            {
                return;
            }
            u8* box = general + 0xE558;
            // Storage order and the ball Kurt makes from each (PKHeX SAV_Apricorn).
            static const char* names[] = {"Red (Level Ball)", "Yellow (Moon Ball)", "Blue (Lure Ball)",
                "Green (Friend Ball)", "Pink (Love Ball)", "White (Fast Ball)", "Black (Heavy Ball)"};
            std::vector<app::Option> o;
            for (int i = 0; i < 7; i++)
            {
                o.push_back({names[i], [save, box, i]()
                    {
                        int v = box[i];
                        if (app::numpad(names[i], v, 0, 99))
                        {
                            box[i]      = u8(v);
                            save->dirty = true;
                        }
                    },
                    std::to_string(box[i]) + " in the box"});
            }
            o.push_back({"99 of each", [save, box]()
                {
                    memset(box, 99, 7);
                    save->dirty = true;
                    app::toast("99 of each Apricorn. Write the save to keep them.");
                }});
            o.push_back({"Close", []() {}});
            app::menu("Apricorn Box", std::move(o));
        }

        void walkerMenu(std::shared_ptr<OpenSave> save)
        {
            u8* general = gen4General(*save->sav);
            if (!general)
            {
                return;
            }
            u8* w = general + 0xE704;
            std::vector<app::Option> o;
            for (int field = 0; field < 2; field++)
            {
                const char* label = field ? "Watts" : "Steps";
                o.push_back({label, [save, w, field, label]()
                    {
                        int v = int(rd32(w + field * 4));
                        if (app::numpad(label, v, 0, 9999999))
                        {
                            wr32(w + field * 4, u32(v));
                            save->dirty = true;
                        }
                    },
                    std::to_string(rd32(w + field * 4))});
            }
            u32 courses = rd32(w + 8);
            int open    = 0;
            for (int i = 0; i < 27; i++)
            {
                open += (courses >> i) & 1;
            }
            o.push_back({"Unlock the watt courses", [save, w]()
                {
                    // Courses 0-19 open with watts; Beyond the Sea (20) on came from events (PKHeX PokewalkerCourse4).
                    wr32(w + 8, rd32(w + 8) | 0xFFFFF);
                    save->dirty = true;
                    app::toast("Courses unlocked. Write the save to keep them.");
                },
                std::to_string(open) + " of 27 courses unlocked"});
            o.push_back({"Close", []() {}});
            app::menu("Pokéwalker", std::move(o));
        }

        // ---------------------------------------------------------------- Diamond/Pearl/Platinum

        void honeyMenu(std::shared_ptr<OpenSave> save, Game g)
        {
            u8* general = gen4General(*save->sav);
            if (!general)
            {
                return;
            }
            u8* trees = general + (g == Game::Pt ? 0x7F38 : 0x72E4);
            int ready = 0;
            for (int i = 0; i < 21; i++)
            {
                u8* t  = trees + i * 8;
                u32 time = rd32(t);
                ready += t[6] != 0 && time > 0 && time <= 1080;
            }
            std::vector<app::Option> o;
            o.push_back({"Slather every tree", [save, trees]()
                {
                    for (int i = 0; i < 21; i++)
                    {
                        u8* t = trees + i * 8;
                        wr32(t, 1080); // catchable from 1080 minutes left down (PKHeX SAV_HoneyTree)
                        if (t[6] == 0)
                        {
                            t[4] = u8(rand() % 6); // slot
                            t[6] = 1;              // the common group
                            t[5] = 0;              // its sub-table, Group - 1
                            t[7] = 1;              // shakes
                        }
                    }
                    save->dirty = true;
                    app::message("Honey trees", "All 21 trees have a Pokémon waiting. Write the save to keep it.");
                },
                std::to_string(ready) + " of 21 trees have a Pokémon waiting now."});
            o.push_back({"Close", []() {}});
            app::menu("Honey trees", std::move(o));
        }

        // ---------------------------------------------------------------- key item events

        // The item plus the flags and vars the official distribution writes; numbers from the Android
        // PKForge's KeyItemEventService (PKHeX event labels, pokeruby/pokeemerald/pokefirered flags.h,
        // pokeplatinum scrcmd_mystery_gift.c and system_vars.c).
        struct Var
        {
            int index;
            u16 value;
            bool onlyIfZero;
        };
        struct KeyEvent
        {
            const char* title;
            const char* destination;
            u16 item; // Gen 3: the game's own item number; Gen 4: the national one
            std::vector<int> enable;
            std::vector<Var> vars;
            std::vector<int> completion;
            int special = 0; // 1: Crystal GS Ball byte, 2: Black/White Liberty Pass check value
        };

        std::vector<KeyEvent> keyEvents(Game g)
        {
            switch (g)
            {
                case Game::RS: return {{"Eon Ticket", "Southern Island", 275, {0x853}, {}, {0xCE}}};
                case Game::E:
                    return {{"Eon Ticket", "Southern Island", 275, {0x8B3}, {}, {0x1C8, 0x1C9}},
                        {"Aurora Ticket", "Birth Island (Deoxys)", 371, {0x8D5, 0x13A}, {}, {0x1AC, 0x1AD}},
                        {"Old Sea Map", "Faraway Island (Mew)", 376, {0x8D6, 0x13C}, {}, {0x1C7, 0x1CA}},
                        {"Mystic Ticket", "Navel Rock (Ho-Oh, Lugia)", 370, {0x8E0, 0x13B}, {}, {0x91, 0x92, 0x1DC, 0x1DD}}};
                case Game::FRLG:
                    return {{"Aurora Ticket", "Birth Island (Deoxys)", 371, {0x84B, 0x2A7}, {}, {0x2E4, 759}},
                        {"Mystic Ticket", "Navel Rock (Ho-Oh, Lugia)", 370, {0x84A, 0x2A8}, {}, {754, 755, 757, 758}}};
                case Game::DP:
                    return {{"Member Card", "Newmoon Island (Darkrai)", 454, {}, {{67, 0x1209, false}}, {344}},
                        {"Oak's Letter", "Flower Paradise (Shaymin)", 452, {}, {{68, 0x1112, false}}, {291}},
                        {"Azure Flute", "Hall of Origin (Arceus)", 455, {}, {{69, 0x1123, false}}, {286}}};
                case Game::Pt:
                    return {{"Member Card", "Newmoon Island (Darkrai)", 454, {}, {{67, 0x1209, false}}, {344}},
                        {"Oak's Letter", "Flower Paradise (Shaymin)", 452, {}, {{68, 0x1112, false}, {87, 1, true}}, {291}},
                        {"Azure Flute", "Hall of Origin (Arceus)", 455, {}, {{69, 0x1123, false}}, {286}},
                        {"Secret Key", "Rotom's Room", 467, {}, {{70, 0x1103, false}}, {710}}};
                case Game::HGSS: return {{"Enigma Stone", "Pewter City (Latias, Latios)", 536, {}, {{67, 1778, false}}, {781}}};
                // The Virtual Console patch's byte: the Goldenrod Pokémon Center then hands the GS Ball over.
                case Game::C: return {{"GS Ball", "Goldenrod Pokémon Center, then Ilex Forest (Celebi)", 0, {}, {}, {}, 1}};
                case Game::BW: return {{"Liberty Pass", "Castelia City harbor, Liberty Garden (Victini)", 574, {}, {}, {}, 2}};
                case Game::ORAS: return {{"Eon Ticket", "Southern Island", 726, {3010}, {}, {3011}}};
                default: return {};
            }
        }

        // Event flags and vars: Gen 3 in the large block, Gen 4 in the general block (PKHeX offsets).
        int flagBase(Game g)
        {
            switch (g)
            {
                case Game::RS: return 0x1220;
                case Game::E: return 0x1270;
                case Game::FRLG: return 0xEE0;
                case Game::DP: return 0xFDC;
                case Game::Pt: return 0xFEC;
                case Game::HGSS: return 0x10C4;
                case Game::ORAS: return 0x14A00 + 0x178 * 2; // EventWork block: 0x178 vars, then the flags
                default: return 0;
            }
        }
        int varBase(Game g)
        {
            switch (g)
            {
                case Game::RS: return 0x1340;
                case Game::E: return 0x139C;
                case Game::FRLG: return 0x1000;
                case Game::DP: return 0xD9C;
                case Game::Pt: return 0xDAC;
                case Game::HGSS: return 0xDE4;
                case Game::ORAS: return 0x14A00;
                default: return 0;
            }
        }
        bool gen3(Game g) { return g == Game::RS || g == Game::E || g == Game::FRLG; }

        u8* at(pksm::Sav& sav, Game g, int offset)
        {
            if (gen3(g))
            {
                return gen3Large(sav, offset);
            }
            if (g == Game::ORAS)
            {
                return raw(sav) + offset;
            }
            u8* general = gen4General(sav);
            return general ? general + offset : nullptr;
        }

        bool flag(pksm::Sav& sav, Game g, int n)
        {
            u8* b = at(sav, g, flagBase(g) + (n >> 3));
            return b && (*b >> (n & 7) & 1);
        }
        void setFlag(pksm::Sav& sav, Game g, int n)
        {
            if (u8* b = at(sav, g, flagBase(g) + (n >> 3)))
            {
                *b |= u8(1 << (n & 7));
            }
        }
        u16 var(pksm::Sav& sav, Game g, int i)
        {
            u8* p = at(sav, g, varBase(g) + i * 2);
            return p ? rd16(p) : 0;
        }
        void setVar(pksm::Sav& sav, Game g, int i, u16 v)
        {
            if (u8* p = at(sav, g, varBase(g) + i * 2))
            {
                wr16(p, v);
            }
        }

        u16 nationalItem(Game g, u16 item) { return gen3(g) ? u16(pksm::ItemConverter::g3ToNational(item)) : item; }

        int keySlots(const pksm::Sav& sav)
        {
            for (auto& [pouch, size] : sav.pouches())
            {
                if (pouch == pksm::Sav::Pouch::KeyItem)
                {
                    return size;
                }
            }
            return 0;
        }

        bool hasItem(const pksm::Sav& sav, u16 national)
        {
            for (int i = 0; i < keySlots(sav); i++)
            {
                auto it = sav.item(pksm::Sav::Pouch::KeyItem, u16(i));
                if (it && it->id() == national)
                {
                    return true;
                }
            }
            return false;
        }

        bool giveItem(pksm::Sav& sav, u16 national)
        {
            if (hasItem(sav, national))
            {
                return true;
            }
            for (int i = 0; i < keySlots(sav); i++)
            {
                auto it = sav.item(pksm::Sav::Pouch::KeyItem, u16(i));
                if (it && it->id() == 0)
                {
                    it->id(national);
                    it->count(1);
                    sav.item(*it, pksm::Sav::Pouch::KeyItem, u16(i));
                    return true;
                }
            }
            return false;
        }

        // Why an event cannot open yet, or empty.
        std::string prerequisite(pksm::Sav& sav, Game g)
        {
            if (g == Game::RS && !flag(sav, g, 0x804))
            {
                return "The ferry only sails after the Hall of Fame: beat the Elite Four first.";
            }
            if (g == Game::E && !flag(sav, g, 0x864))
            {
                return "The ferry only sails after the Hall of Fame: beat the Elite Four first.";
            }
            if (g == Game::FRLG && var(sav, g, 0x7E) < 3)
            {
                return "The Seagallop only runs after the S.S. Anne has left Vermilion.";
            }
            return "";
        }

        // Crystal: the Mobile event byte and its backup (PKHeX SAV2.EnableGSBallMobileEvent).
        std::vector<int> gsBallBytes(const pksm::Sav& sav)
        {
            return sav.language() == pksm::Language::JPN ? std::vector<int>{0xA000, 0xA083} : std::vector<int>{0x3E3C, 0x3E44};
        }
        // Black/White: Misc block 0x21200 + 0xBC must hold 2010_04_06 XOR the trainer's ID32 (PKHeX Misc5).
        u32 libertyExpected(const pksm::Sav& sav) { return 20100406u ^ (u32(sav.TID()) | u32(sav.SID()) << 16); }

        bool specialOn(pksm::Sav& sav, int special)
        {
            if (special == 1)
            {
                return raw(sav)[gsBallBytes(sav)[0]] == 0x0B;
            }
            if (special == 2)
            {
                return rd32(raw(sav) + 0x21200 + 0xBC) == libertyExpected(sav);
            }
            return true;
        }

        void specialSet(pksm::Sav& sav, int special)
        {
            if (special == 1)
            {
                for (int offset : gsBallBytes(sav))
                {
                    raw(sav)[offset] = 0x0B;
                }
            }
            else if (special == 2)
            {
                wr32(raw(sav) + 0x21200 + 0xBC, libertyExpected(sav));
            }
        }

        void keyEventsMenu(std::shared_ptr<OpenSave> save, Game g)
        {
            pksm::Sav& sav = *save->sav;
            std::vector<app::Option> o;
            for (const KeyEvent& ev : keyEvents(g))
            {
                bool done = false;
                for (int f : ev.completion)
                {
                    done |= flag(sav, g, f);
                }
                bool armed = (ev.item == 0 || hasItem(sav, nationalItem(g, ev.item))) && specialOn(sav, ev.special);
                for (int f : ev.enable)
                {
                    armed &= flag(sav, g, f);
                }
                for (const Var& v : ev.vars)
                {
                    armed &= v.onlyIfZero || var(sav, g, v.index) == v.value;
                }
                std::string state = done ? "done (already caught or met)" : armed ? "ready: go to " : "not active";
                if (armed && !done)
                {
                    state += ev.destination;
                }
                o.push_back({ev.title, [save, g, ev]()
                    {
                        pksm::Sav& s = *save->sav;
                        std::string why = prerequisite(s, g);
                        if (!why.empty())
                        {
                            app::message(ev.title, why);
                            return;
                        }
                        if (ev.item && !giveItem(s, nationalItem(g, ev.item)))
                        {
                            app::message(ev.title, "The Key Items pocket is full.");
                            return;
                        }
                        specialSet(s, ev.special);
                        for (int f : ev.enable)
                        {
                            setFlag(s, g, f);
                        }
                        for (const Var& v : ev.vars)
                        {
                            if (!v.onlyIfZero || var(s, g, v.index) == 0)
                            {
                                setVar(s, g, v.index, v.value);
                            }
                        }
                        save->dirty = true;
                        app::message(ev.title, std::string("Activated as the real distribution does") +
                                                   (ev.item ? ": the item is in the Key Items pocket" : "") +
                                                   ". Write the save, then head for " + ev.destination + ".");
                    },
                    state});
            }
            o.push_back({"Close", []() {}});
            app::menu("Key item events", std::move(o));
        }

        // ---------------------------------------------------------------- Sun/Moon Poké Beans

        void beansMenu(std::shared_ptr<OpenSave> save, Game g)
        {
            // ResortSave7 (SM block at 0x3B400, USUM at 0x3B800): 15 counts at +0x564C.
            u8* beans = raw(*save->sav) + (g == Game::SM ? 0x3B400 : 0x3B800) + 0x564C;
            static const char* colors[] = {"Red", "Blue", "Light Blue", "Green", "Yellow", "Purple", "Orange"};
            std::vector<app::Option> o;
            int total = 0;
            for (int i = 0; i < 15; i++)
            {
                total += beans[i];
            }
            o.push_back({"255 of every bean", [save, beans]()
                {
                    memset(beans, 255, 15);
                    save->dirty = true;
                    app::toast("Beans filled. Write the save to keep them.");
                },
                std::to_string(total) + " beans in Poké Pelago now."});
            for (int i = 0; i < 15; i++)
            {
                std::string name = i < 7 ? std::string(colors[i]) + " Bean"
                                 : i < 14 ? std::string(colors[i - 7]) + " Patterned Bean" : "Rainbow Bean";
                o.push_back({name, [save, beans, i, name]()
                    {
                        int v = beans[i];
                        if (app::numpad(name, v, 0, 255))
                        {
                            beans[i]    = u8(v);
                            save->dirty = true;
                        }
                    },
                    std::to_string(beans[i])});
            }
            o.push_back({"Close", []() {}});
            app::menu("Poké Beans", std::move(o));
        }

        // ---------------------------------------------------------------- Gen 1 one-time encounters

        // PKHeX G1OverworldSpawner: each is an event flag and a "hide object" flag; clearing both puts the
        // Pokémon back on the map. Yellow moves a few and adds its starter gifts.
        struct Spawn
        {
            const char* name;
            int event, hide;
        };

        std::vector<Spawn> spawns(Game g)
        {
            bool y = g == Game::Y;
            std::vector<Spawn> list = {{"Mewtwo", 0x8C1, y ? 0xD7 : 0xD1}, {"Articuno", 0x9DA, y ? 0xEB : 0xE3},
                {"Zapdos", 0x469, 0x55}, {"Moltres", 0x53E, 0x5B}, {"Electrode 1 (Power Plant)", 0x464, 0x50},
                {"Electrode 2 (Power Plant)", 0x467, 0x53}, {"Voltorb 1 (Power Plant)", 0x461, 0x4D},
                {"Voltorb 2", 0x462, 0x4E}, {"Voltorb 3", 0x463, 0x4F}, {"Voltorb 4", 0x465, 0x51},
                {"Voltorb 5", 0x466, 0x52}, {"Voltorb 6", 0x468, 0x54}, {"Hitmonlee (Fighting Dojo)", 0x356, 0x4A},
                {"Hitmonchan (Fighting Dojo)", 0x357, 0x4B}, {"Eevee (Celadon Mansion)", 0, 0x45},
                {"Kabuto fossil", 0x57E, y ? 0x6F : 0x6D}, {"Omanyte fossil", 0x57F, y ? 0x70 : 0x6E},
                {"Aerodactyl (Old Amber)", 0x069, y ? 0x33 : 0x34}};
            if (y)
            {
                list.push_back({"Bulbasaur (Cerulean gift)", 0x0A8, 0x34});
            }
            return list;
        }

        int gen1EventFlags(const pksm::Sav& sav) { return sav.language() == pksm::Language::JPN ? 0x29E9 : 0x29F3; }
        int gen1HideFlags(const pksm::Sav& sav) { return sav.language() == pksm::Language::JPN ? 0x2848 : 0x2852; }

        bool bit(const u8* base, int n) { return base[n >> 3] >> (n & 7) & 1; }
        void clearBit(u8* base, int n) { base[n >> 3] &= u8(~(1 << (n & 7))); }

        void resetsMenu(std::shared_ptr<OpenSave> save, Game g)
        {
            pksm::Sav& sav = *save->sav;
            u8* events     = raw(sav) + gen1EventFlags(sav);
            u8* hidden     = raw(sav) + gen1HideFlags(sav);
            std::vector<app::Option> o;
            auto rearm = [events, hidden](const Spawn& sp)
            {
                if (sp.event)
                {
                    clearBit(events, sp.event);
                }
                clearBit(hidden, sp.hide);
            };
            o.push_back({"Bring them all back", [save, g, rearm]()
                {
                    for (const Spawn& sp : spawns(g))
                    {
                        rearm(sp);
                    }
                    save->dirty = true;
                    app::message("One-time encounters", "Every one is back where the game first put it. Write the "
                                                        "save to keep it.");
                }});
            for (const Spawn& sp : spawns(g))
            {
                bool gone = bit(hidden, sp.hide) || (sp.event && bit(events, sp.event));
                o.push_back({sp.name, [save, sp, rearm]()
                    {
                        rearm(sp);
                        save->dirty = true;
                        app::toast(std::string(sp.name) + " is back. Write the save to keep it.");
                    },
                    gone ? "gone: A brings it back" : "still waiting in the game"});
            }
            o.push_back({"Close", []() {}});
            app::menu("One-time encounters", std::move(o));
        }
    }

    bool hasAny(const pksm::Sav& sav) { return gameOf(sav) != Game::None; }

    void open(std::shared_ptr<OpenSave> save)
    {
        Game g = gameOf(*save->sav);
        std::vector<app::Option> o;
        if (g == Game::RS || g == Game::E)
        {
            o.push_back({"Clock repair", [save]() { clockMenu(save); }, "Berries and daily events frozen? Fix the clock."});
        }
        if (g == Game::RS || g == Game::E || g == Game::FRLG)
        {
            o.push_back({"Roaming legendary", [save, g]() { roamerMenu(save, g); },
                "Set it roaming again, perfect IVs, shiny."});
        }
        if (!keyEvents(g).empty())
        {
            o.push_back({"Key item events", [save, g]() { keyEventsMenu(save, g); },
                "Tickets and event items, with the flags the real distributions set."});
        }
        if (g == Game::HGSS)
        {
            o.push_back({"Apricorn Box", [save]() { apricornMenu(save); }, "Apricorns for Kurt's balls."});
            o.push_back({"Pokéwalker", [save]() { walkerMenu(save); }, "Steps, watts and courses."});
        }
        if (g == Game::RB || g == Game::Y)
        {
            o.push_back({"Re-arm one-time encounters", [save, g]() { resetsMenu(save, g); },
                "Mewtwo, the birds, Power Plant, fossils and gifts back on the map."});
        }
        if (g == Game::SM || g == Game::USUM)
        {
            o.push_back({"Poké Beans", [save, g]() { beansMenu(save, g); }, "Poké Pelago's beans."});
        }
        if (g == Game::DP || g == Game::Pt)
        {
            o.push_back({"Honey trees", [save, g]() { honeyMenu(save, g); }, "A Pokémon waiting on every tree."});
        }
        if (o.empty())
        {
            app::message("World & events", "PKForge has no world editors for this game yet.");
            return;
        }
        o.push_back({"Close", []() {}});
        app::menu("World & events", std::move(o));
    }
}

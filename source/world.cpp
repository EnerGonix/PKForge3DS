#include "world.hpp"
#include "app.hpp"
#include "locks.hpp"
#include "pkdata.hpp"

#include "pkx/PK2.hpp"
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

        // ---------------------------------------------------------------- Day Care / Nursery
        // As the Android PKForge's Day Care view: what is deposited (level, EXP gained) and whether an egg is
        // ready, and a withdraw into the first free box slot, the gained EXP added as the game does (moves
        // stay as they are). Eggs and the egg RNG are left to the game. Offsets from PKHeX (SAV3*, SAV4,
        // Daycare5, Daycare6XY/AO, Daycare7).

        struct DaySlot
        {
            u8* pk             = nullptr; // the stored (encrypted) Pokémon
            int size           = 0;
            const char* format = "";
            u8* exp            = nullptr; // EXP gained, u32 (none in Generation 7)
            u8* occupied       = nullptr; // set when occupied (Generations 5-7; before: species != 0)
            int occupiedSize   = 0;       // 1, 4, or -1 for Gen 2's flag bit (bit 0 of a byte shared with others)
            u8* nick           = nullptr; // Gen 2: the nickname and trainer name beside the 32-byte Pokémon
            u8* ot             = nullptr;
            int nameLength     = 0;
            std::vector<std::pair<u8*, int>> clear; // bytes zeroed on withdraw
        };

        struct Facility
        {
            std::string name;
            std::vector<DaySlot> slots;
            bool egg = false;
        };

        std::vector<Facility> facilities(pksm::Sav& sav)
        {
            std::vector<Facility> out;
            u8 v = u8(sav.version());
            if (v >= 39 && v <= 41) // Gold, Silver, Crystal: after the Pokédex (PKHeX SAV2Offsets.Daycare)
            {
                bool jp  = sav.language() == pksm::Language::JPN;
                bool kor = sav.language() == pksm::Language::KOR;
                int seen = kor ? 0x2AAE : v == 41 ? (jp ? 0x29CA : 0x2A47) : (jp ? 0x29EE : 0x2A6C);
                int name = jp ? 6 : 11;
                u8* at   = raw(sav) + seen + 0x1F + 28 + 1;
                Facility f{"Route 34 Day Care", {}, false};
                f.egg = (at[0] & 0x40) != 0;
                u8* flags = at;
                u8* body  = at + 1;
                for (int i = 0; i < 2; i++)
                {
                    DaySlot s;
                    s.occupied     = flags;
                    s.occupiedSize = -1;
                    s.nick         = body;
                    s.ot           = body + name;
                    s.pk           = body + 2 * name;
                    s.size         = 32;
                    s.format       = "PK2";
                    s.nameLength   = name;
                    s.clear        = {{s.pk, 32}};
                    f.slots.push_back(s);
                    // the second slot comes after the Lady's flags, the steps and the Ditto state
                    flags = body + 2 * name + 32;
                    body  = flags + 3;
                }
                out.push_back(f);
            }
            else if (v >= 1 && v <= 5) // Ruby/Sapphire, Emerald, FireRed/LeafGreen: the large block
            {
                Game g      = gameOf(sav);
                bool rs     = g == Game::RS;
                int base    = g == Game::E ? 0x3030 : g == Game::FRLG ? 0x2F80 : 0x2F9C;
                int slotLen = rs ? 80 : 80 + 0x38 + 4;
                Facility f{g == Game::FRLG ? "Four Island Day Care" : "Route 117 Day Care", {}, false};
                for (int i = 0; i < 2; i++)
                {
                    DaySlot s;
                    s.pk     = gen3Large(sav, base + i * slotLen);
                    s.size   = 80;
                    s.format = "PK3";
                    s.exp    = gen3Large(sav, rs ? base + 160 + 2 * 0x38 + 4 * i : base + (i + 1) * slotLen - 4);
                    if (!s.pk || !s.exp)
                    {
                        return {};
                    }
                    s.clear = {{s.pk, rs ? 80 : slotLen}, {s.exp, 4}};
                    if (rs)
                    {
                        if (u8* mail = gen3Large(sav, base + 160 + i * 0x38))
                        {
                            s.clear.push_back({mail, 0x38});
                        }
                    }
                    f.slots.push_back(s);
                }
                f.egg = flag(sav, g, g == Game::FRLG ? 0x266 : 0x86);
                out.push_back(f);
            }
            else if (v == 7 || v == 8 || v == 10 || v == 11 || v == 12) // HGSS, Diamond/Pearl, Platinum
            {
                u8* general = gen4General(sav);
                if (!general)
                {
                    return {};
                }
                int base = v == 12 ? 0x1654 : v <= 8 ? 0x15FC : 0x141C;
                Facility f{v <= 8 ? "Route 34 Day Care" : "Solaceon Town Day Care", {}, false};
                for (int i = 0; i < 2; i++)
                {
                    DaySlot s;
                    s.pk     = general + base + i * 236;
                    s.size   = 136;
                    s.format = "PK4";
                    s.exp    = general + base + i * 236 + 236 - 4;
                    s.clear  = {{s.pk, 236}};
                    f.slots.push_back(s);
                }
                f.egg = rd32(general + base + 2 * 236) != 0; // the egg's PID, set while one is waiting
                out.push_back(f);
            }
            else if (v >= 20 && v <= 23) // Black/White, Black 2/White 2: block 50
            {
                u8* block = raw(sav) + (v <= 21 ? 0x20E00 : 0x20D00);
                Facility f{"Route 3 Day Care", {}, false};
                for (int i = 0; i < 2; i++)
                {
                    u8* at = block + i * 228;
                    DaySlot s;
                    s.occupied     = at;
                    s.occupiedSize = 4;
                    s.pk           = at + 4;
                    s.size         = 136;
                    s.format       = "PK5";
                    s.exp          = at + 4 + 220;
                    s.clear        = {{at, 228}};
                    f.slots.push_back(s);
                }
                f.egg = block[0x1C8] & 1;
                out.push_back(f);
            }
            else if (v >= 24 && v <= 27) // X/Y, Omega Ruby/Alpha Sapphire: block 38 (ORAS has two)
            {
                bool oras = v >= 26;
                u8* block = raw(sav) + (oras ? 0x1BC00 : 0x1B200);
                for (int d = 0; d < (oras ? 2 : 1); d++)
                {
                    u8* couple = block + d * 0x1F0;
                    Facility f{oras ? (d == 0 ? "Route 117 Day Care" : "Battle Resort Day Care") : "Route 7 Day Care", {}, false};
                    for (int i = 0; i < 2; i++)
                    {
                        u8* at = couple + i * 240;
                        DaySlot s;
                        s.occupied     = at;
                        s.occupiedSize = 1;
                        s.exp          = at + 4;
                        s.pk           = at + 8;
                        s.size         = 232;
                        s.format       = "PK6";
                        s.clear        = {{at, 240}};
                        f.slots.push_back(s);
                    }
                    f.egg = couple[0x1E0] == 1;
                    out.push_back(f);
                }
            }
            else if (v >= 30 && v <= 33) // Sun/Moon, Ultra Sun/Ultra Moon: block 33
            {
                u8* block = raw(sav) + (v <= 31 ? 0x6B400 : 0x6BA00);
                Facility f{"Paniola Ranch Nursery", {}, false};
                for (int i = 0; i < 2; i++)
                {
                    u8* at = block + i * 233;
                    DaySlot s;
                    s.occupied     = at;
                    s.occupiedSize = 1;
                    s.pk           = at + 1;
                    s.size         = 232;
                    s.format       = "PK7";
                    s.clear        = {{at, 233}};
                    f.slots.push_back(s);
                }
                f.egg = block[0x1D8] == 1;
                out.push_back(f);
            }
            return out;
        }

        // The Pokémon in a slot (nullptr when empty), with the EXP it gained added.
        std::unique_ptr<pksm::PKX> deposited(const DaySlot& s)
        {
            if (s.occupied && (s.occupiedSize == 4 ? rd32(s.occupied) : s.occupiedSize < 0 ? s.occupied[0] & 1 : s.occupied[0]) == 0)
            {
                return nullptr;
            }
            std::unique_ptr<pksm::PKX> pk;
            if (s.nick)
            {
                // Gen 2: PKSM-Core's layout, as Sav2 builds a box Pokémon (list marker, species, end; the
                // 48-byte party struct holding the 32 stored bytes; trainer name; nickname).
                u8 buffer[pksm::PK2::INT_LENGTH_WITH_NAMES] = {0x01, s.pk[0], 0xFF};
                std::copy(s.pk, s.pk + 32, buffer + 3);
                std::copy(s.ot, s.ot + s.nameLength, buffer + 3 + pksm::PK2::PARTY_LENGTH);
                std::copy(s.nick, s.nick + s.nameLength, buffer + 3 + pksm::PK2::PARTY_LENGTH + s.nameLength);
                pk = pkdata::fromBytes("PK2", buffer, 3 + pksm::PK2::PARTY_LENGTH + 2 * s.nameLength);
                if (pk)
                {
                    static_cast<pksm::PK2&>(*pk).updatePartyData();
                }
            }
            else
            {
                pk = pkdata::fromBytes(s.format, s.pk, s.size);
            }
            if (!pk || pk->species() == pksm::Species::None)
            {
                return nullptr;
            }
            if (s.exp)
            {
                u32 gained = rd32(s.exp);
                if (gained)
                {
                    pk->experience(pk->experience() + gained);
                    if (pk->level() >= 100)
                    {
                        pk->level(100);
                    }
                    pk->refreshChecksum(); // changed data: the game rejects a stale checksum as a Bad Egg
                }
            }
            return pk;
        }

        void withdraw(std::shared_ptr<OpenSave> save, int facility, int slot, const std::function<void()>& changed)
        {
            auto all = facilities(*save->sav);
            if (facility >= int(all.size()) || slot >= int(all[facility].slots.size()))
            {
                return;
            }
            const DaySlot& s = all[facility].slots[slot];
            auto pk          = deposited(s);
            if (!pk)
            {
                return;
            }
            pksm::Sav& sav = *save->sav;
            int perBox     = sav.maxSlot() / sav.maxBoxes();
            for (int b = 0; b < sav.maxBoxes(); b++)
            {
                if (locks::locked(save->ref.path, b))
                {
                    continue;
                }
                for (int i = 0; i < perBox; i++)
                {
                    auto there = sav.pkm(u8(b), u8(i));
                    if (there && there->species() != pksm::Species::None)
                    {
                        continue;
                    }
                    sav.pkm(*pk, u8(b), u8(i), false);
                    for (auto& [at, len] : s.clear)
                    {
                        memset(at, 0, len);
                    }
                    if (s.occupiedSize < 0)
                    {
                        s.occupied[0] &= 0xFE; // Gen 2: only the slot's bit, the rest of the byte stays
                    }
                    save->dirty = true;
                    if (changed)
                    {
                        changed();
                    }
                    char where[48];
                    snprintf(where, sizeof(where), " is in Box %d, slot %d.", b + 1, i + 1);
                    app::message("Day Care", pkdata::species(u16(pk->species())) + where + " Write the save to keep it.");
                    return;
                }
            }
            app::message("Day Care", "Every box is full (or locked): free a slot first.");
        }

        void dayCareSlotMenu(std::shared_ptr<OpenSave> save, int facility, int slot, std::function<void()> changed)
        {
            auto all = facilities(*save->sav);
            if (facility >= int(all.size()) || slot >= int(all[facility].slots.size()))
            {
                return;
            }
            auto pk = deposited(all[facility].slots[slot]);
            if (!pk)
            {
                return;
            }
            app::confirm(pkdata::species(u16(pk->species())) + " leaves the " + all[facility].name +
                             " for the first free box slot, with the EXP it gained. Write the save to keep it (a "
                             "restore point is kept first).",
                [save, facility, slot, changed]() { withdraw(save, facility, slot, changed); }, "Withdraw");
        }
    }

    bool hasAny(const pksm::Sav& sav) { return gameOf(sav) != Game::None; }

    bool hasDayCare(const pksm::Sav& sav)
    {
        u8 v = u8(sav.version());
        return (v >= 39 && v <= 41) || (v >= 1 && v <= 5) || v == 7 || v == 8 || (v >= 10 && v <= 12) ||
               (v >= 20 && v <= 27) || (v >= 30 && v <= 33);
    }

    void dayCare(std::shared_ptr<OpenSave> save, std::function<void()> changed)
    {
        auto all = facilities(*save->sav);
        if (all.empty())
        {
            app::message("Day Care", "The Day Care was not found in this save.");
            return;
        }
        std::vector<app::Option> o;
        for (int f = 0; f < int(all.size()); f++)
        {
            for (int i = 0; i < int(all[f].slots.size()); i++)
            {
                auto pk           = deposited(all[f].slots[i]);
                std::string label = (all.size() > 1 ? all[f].name + " " : std::string("Slot ")) + std::to_string(i + 1) + ": ";
                if (!pk)
                {
                    o.push_back({label + "empty", []() {}, all[f].name, false});
                    continue;
                }
                std::string detail = pkdata::species(u16(pk->species())) + ", Lv. " + std::to_string(pk->level());
                if (all[f].slots[i].exp)
                {
                    detail += ", +" + std::to_string(rd32(all[f].slots[i].exp)) + " EXP";
                }
                o.push_back({label + pk->nickname() + " Lv. " + std::to_string(pk->level()),
                    [save, f, i, changed]() { dayCareSlotMenu(save, f, i, changed); }, detail + ". A withdraws it into a free box slot."});
            }
            o.push_back({all[f].egg ? "An egg is waiting" : "No egg waiting", []() {},
                all[f].egg ? "Talk to the Day Care man in the game to take it." : "", false});
        }
        o.push_back({"Close", []() {}});
        app::menu(all.size() > 1 ? "Day Cares" : all[0].name, std::move(o));
    }

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

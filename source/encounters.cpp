#include "encounters.hpp"
#include "files.hpp"
#include "learnsets.hpp"
#include "pkdata.hpp"

#include "pkx/PK2.hpp"
#include "pkx/PK4.hpp"
#include "pkx/PK5.hpp"
#include "pkx/PK6.hpp"
#include "pkx/PK7.hpp"
#include "utils/i18n.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>

namespace encounters
{
    namespace
    {
        u16 rd16(const u8* p) { return u16(p[0] | p[1] << 8); }

#include "vivillon.inc"

        // The Vivillon pattern (also Scatterbug's and Spewpa's) of a save's country and region
        // (PKHeX Vivillon3DS.GetPattern).
        u8 vivillonPattern(u8 country, u8 region)
        {
            if (country >= sizeof(vivillonSameCountry))
            {
                return 0;
            }
            if (vivillonSameCountry[country])
            {
                return vivillonSameCountry[country];
            }
            u16 key = u16(country << 8 | region);
            size_t n = sizeof(vivillonDiffRegion) / sizeof(vivillonDiffRegion[0]);
            size_t lo = 0, hi = n;
            while (lo < hi)
            {
                size_t mid = (lo + hi) / 2;
                if (vivillonDiffRegion[mid] < key)
                {
                    lo = mid + 1;
                }
                else
                {
                    hi = mid;
                }
            }
            return lo < n && vivillonDiffRegion[lo] == key ? vivillonDiffForm[lo] : 0;
        }

        // Generation 3 Unown letter from its PID (PKHeX EntityPID.GetUnownForm3).
        u8 unownForm3(u32 pid)
        {
            u32 v = ((pid >> 24) & 3) << 6 | ((pid >> 16) & 3) << 4 | ((pid >> 8) & 3) << 2 | (pid & 3);
            return u8(v % 28);
        }

        int genOf(const pksm::Sav& sav) { return pkdata::genNumber(sav.generation()); }

        // ---------------------------------------------------------------- the RNG (PKHeX LCRNG)

        struct Lcrng
        {
            u32 seed;
            u32 next16()
            {
                seed = seed * 0x41C64E6Du + 0x6073u;
                return seed >> 16;
            }
            u32 next15() { return next16() & 0x7FFF; }
            // The IVs as the games read them: HP/Atk/Def from the first call, Spe/SpA/SpD from the second.
            u32 ivs()
            {
                u32 a = next15();
                u32 b = next15();
                return b << 15 | a;
            }
        };

        u32 random32() { return u32(rand()) ^ (u32(rand()) << 15) ^ (u32(rand()) << 30); }

        bool shiny3(u16 tid, u16 sid, u32 pid) { return (tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) < 8; }

        // Slot ranges of a 0-99 roll (PKHeX SlotMethodH / SlotMethodJ / SlotMethodK).
        bool range(int gen, bool hgss, u8 type, u8 slot, int& lo, int& hi)
        {
            static const u8 grass[] = {0, 20, 40, 50, 60, 70, 80, 85, 90, 94, 98, 99, 100};
            static const u8 surf[]  = {0, 60, 90, 95, 99, 100};
            static const u8 old3[]  = {0, 70, 100};
            static const u8 good3[] = {0, 60, 80, 100};
            static const u8 super3[] = {0, 40, 70, 95, 99, 100}; // Gen 3 Super Rod
            static const u8 superJ[] = {0, 40, 80, 95, 99, 100}; // D/P/Pt Good and Super Rod
            static const u8 superK[] = {0, 40, 70, 85, 95, 100}; // HG/SS Old, Good and Super Rod
            static const u8 smash4[] = {0, 80, 100};             // HGSS Rock Smash
            static const u8 headbutt[] = {0, 50, 65, 80, 90, 95, 100}; // HGSS Headbutt
            static const u8 contest[] = {80, 60, 50, 40, 30, 20, 15, 10, 5, 0, 100}; // HGSS Bug Contest, slot 0 = 80-99
            const u8* t = nullptr;
            int n       = 0;
            if (gen == 3)
            {
                switch (type)
                {
                    case 0: t = grass, n = 12; break;
                    case 1: t = surf, n = 5; break;
                    case 2: t = old3, n = 2; break;
                    case 3: t = good3, n = 3; break;
                    case 4: t = super3, n = 5; break;
                    case 5: t = surf, n = 5; break; // Rock Smash uses the Surf table
                    case 6:
                    case 7: lo = 0, hi = 49; return slot == 0;
                    default: return false;
                }
            }
            else if (gen == 4 && hgss)
            {
                switch (type)
                {
                    case 0: t = grass, n = 12; break;
                    case 1: t = surf, n = 5; break;
                    case 2:
                    case 3:
                    case 4: t = superK, n = 5; break;
                    case 5: t = smash4, n = 2; break;
                    case 6:
                    case 7: t = headbutt, n = 6; break;
                    case 8:
                        if (slot > 9)
                        {
                            return false;
                        }
                        lo = contest[slot];
                        hi = (slot == 0 ? 100 : contest[slot - 1]) - 1;
                        return true;
                    case 10: case 11: case 12: case 13: case 14: // Safari Zone: a 0-9 roll, one per slot
                        lo = hi = slot;
                        return slot < 10;
                    default: return false;
                }
            }
            else if (gen == 4)
            {
                switch (type)
                {
                    case 0: t = grass, n = 12; break;
                    case 1:
                    case 2: t = surf, n = 5; break;
                    case 3:
                    case 4: t = superJ, n = 5; break;
                    case 9: lo = 0, hi = 99; return true; // honey trees: the roll is made but not used
                    default: return false;
                }
            }
            if (!t || slot >= n)
            {
                return false;
            }
            lo = t[slot];
            hi = t[slot + 1] - 1;
            return true;
        }

        void setIVs(pksm::PKX& pk, u32 iv32)
        {
            static const pksm::Stat order[] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD,
                pksm::Stat::SPATK, pksm::Stat::SPDEF};
            for (int i = 0; i < 6; i++)
            {
                pk.iv(order[i], u8((iv32 >> (5 * i)) & 31));
            }
        }

        void randomIVs(pksm::PKX& pk)
        {
            for (pksm::Stat st : {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD, pksm::Stat::SPATK,
                     pksm::Stat::SPDEF})
            {
                pk.iv(st, u8(rand() % (pk.maxIV() + 1)));
            }
        }

        // Generation 3 Method H and Generation 4 Methods J/K, no lead (PKHeX GenerateMethodH/J/K.SetRandom).
        void classic(pksm::PKX& pk, const Slot& s, int gen, bool hgss)
        {
            int lo = 0, hi = 99;
            range(gen, hgss, s.type, s.number, lo, hi);
            bool safariK = hgss && s.type >= 10;       // HG/SS Safari Zone
            // Gen 4: grass slots have a fixed level, and so has the HG/SS Safari Zone.
            bool levelRoll = gen == 3 || (s.type != 0 && !safariK);
            bool honey     = gen == 4 && !hgss && s.type == 9;
            // R/S/E Safari Zone: one more call between level and nature (PKHeX MethodH, no Pokéblock placed).
            bool safariRSE = gen == 3 && s.location == 57;
            // HG/SS Bug Contest and Safari Zone re-roll the IVs until one of them is 31.
            bool want31 = hgss && (s.type == 8 || s.location == 202);
            u16 tid = pk.TID(), sid = pk.SID();
            Lcrng r{random32()};
            if (gen == 3 && s.species == 201)
            {
                // Unown (PKHeX GenerateMethodH.SetRandomUnown): no nature roll; the PID halves come high first and
                // are rolled until the letter is the chamber's.
                for (;;)
                {
                    u32 esv = r.next16() % 100;
                    if (int(esv) < lo || int(esv) > hi)
                    {
                        continue;
                    }
                    r.next16(); // the level roll (a fixed level)
                    for (;;)
                    {
                        u32 a   = r.next16();
                        u32 b   = r.next16();
                        u32 pid = a << 16 | b;
                        if (unownForm3(pid) != s.form)
                        {
                            continue;
                        }
                        u32 iv32 = r.ivs();
                        pk.PID(pid);
                        pk.setAbility(u8(pid & 1));
                        setIVs(pk, iv32);
                        return;
                    }
                }
            }
            // D/P/Pt Mt. Coronet B1F: every fishing roll there checks the Feebas tiles first (one more call), and
            // Feebas needs that call's top bit (PKHeX MethodJ.IsValidCoronetB1F).
            bool coronet = gen == 4 && !hgss && s.type >= 2 && s.type <= 4 && s.rate == 255;
            // Some kinds have to trigger first, judged from the current state (PKHeX CheckEncounterActivation,
            // no lead): Gen 3 Rock Smash, Gen 4 fishing bites, HGSS Rock Smash.
            auto triggers = [&](u32 seed)
            {
                u32 u16v = seed >> 16;
                if (gen == 3)
                {
                    if (s.type != 5)
                    {
                        return true;
                    }
                    u32 rate = s.rate * 16u;
                    rate += rate >> 1; // the White Flute, as PKHeX assumes
                    return u16v % 2880 < rate;
                }
                if ((s.type >= 2 && s.type <= 4) || (hgss && s.type >= 12 && s.type <= 14))
                {
                    u8 t    = s.type >= 12 ? u8(s.type - 10) : s.type;
                    u32 rod = t == 2 ? 25 : t == 3 ? 50 : 75;
                    return hgss ? u16v % 100 < rod + 50 : u16v / 656 < rod;
                }
                if (s.type == 5)
                {
                    return u16v % 100 < s.rate;
                }
                return true;
            };
            for (;;)
            {
                if (!triggers(r.seed))
                {
                    r.next16();
                    continue;
                }
                if (coronet && (r.next16() >> 15) != 1 && s.species == 349)
                {
                    continue;
                }
                u32 esv = gen == 4 && !hgss ? r.next16() / 656u : r.next16() % (safariK ? 10 : 100);
                if (int(esv) < lo || int(esv) > hi)
                {
                    continue;
                }
                u32 lv     = levelRoll ? r.next16() : 0;
                if (safariRSE)
                {
                    r.next16();
                }
                u32 nature = gen == 4 && !hgss ? r.next16() / 0xA3Eu : r.next16() % 25;
                for (;;)
                {
                    u32 a   = r.next16();
                    u32 b   = r.next16();
                    u32 pid = b << 16 | a;
                    if (pid % 25 != nature)
                    {
                        continue;
                    }
                    if (shiny3(tid, sid, pid))
                    {
                        break; // try again from a new slot roll, as PKHeX does
                    }
                    u32 iv32 = r.ivs();
                    if (gen == 4 && s.species == 201)
                    {
                        if (hgss)
                        {
                            // Ruins of Alph entrance: ! or ? from the second call after the IVs
                            // (PKHeX RuinsOfAlph4.GetEntranceForm on Method 1 seed + 6).
                            Lcrng f = r;
                            f.next16();
                            pk.alternativeForm(u8(26 + (f.next16() & 1)));
                        }
                        else
                        {
                            // Solaceon Ruins: a room with a single letter, F, R, I, E, N or D.
                            static const u8 rooms[] = {5, 17, 8, 4, 13, 3};
                            pk.alternativeForm(rooms[rand() % 6]);
                        }
                    }
                    if (want31)
                    {
                        bool any = false;
                        for (int i = 0; i < 6; i++)
                        {
                            any |= ((iv32 >> (5 * i)) & 31) == 31;
                        }
                        if (!any)
                        {
                            break;
                        }
                    }
                    u8 level = s.min;
                    if (honey)
                    {
                        level = u8(5 + lv / 0x1745u); // PKHeX MethodJ.GetHoneyTreeLevel
                        if (level < s.min || level > s.max)
                        {
                            break;
                        }
                    }
                    else if (levelRoll && s.max > s.min)
                    {
                        level = u8(s.min + lv % (1u + s.max - s.min));
                    }
                    pk.level(level);
                    pk.metLevel(level);
                    // PKSM-Core's gender, nature and ability-number setters pick a new PID: in Gen 4 the gender
                    // byte and the ability go first (written directly) and the game's PID last. Gen 3 reads
                    // gender and nature from the PID and keeps the ability as a bit.
                    if (gen == 4)
                    {
                        pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
                        pk.ability(pk.abilities(u8(pid & 1)));
                        pk.PID(pid);
                    }
                    else
                    {
                        pk.PID(pid);
                        pk.setAbility(u8(pid & 1));
                    }
                    setIVs(pk, iv32);
                    return;
                }
            }
        }

        // Generation 5 (PKHeX MonochromeRNG.Generate, wild): nature apart, ability in bit 16, the wild xor.
        void monochrome(pksm::PKX& pk, bool grotto)
        {
            u16 tid = pk.TID(), sid = pk.SID();
            u32 pid = random32();
            if (shiny3(tid, sid, pid))
            {
                pid ^= 0x10000000;
            }
            if (grotto)
            {
                pid &= ~0x10000u; // a single ability (the hidden one) forces bit 16 to 0
            }
            if (((pid >> 31) ^ (pid & 1) ^ ((tid ^ sid) & 1)) != 0)
            {
                pid ^= 0x80000000;
            }
            // Gender and ability-number setters pick a new PID: everything first, the PID last.
            pk.nature(pksm::Nature{u8(rand() % 25)});
            pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
            u8 index = grotto ? 2 : u8((pid >> 16) & 1);
            pk.ability(pk.abilities(index));
            static_cast<pksm::PK5&>(pk).hiddenAbility(grotto);
            pk.PID(pid);
            randomIVs(pk);
        }

        // Generations 6 and 7: a random non-shiny PID, encryption constant, nature, gender and one of the two
        // regular abilities.
        void modern(pksm::PKX& pk)
        {
            u16 tid = pk.TID(), sid = pk.SID();
            u32 pid;
            do
            {
                pid = random32();
            } while (((tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) >> 4) == 0);
            pk.PID(pid);
            pk.encryptionConstant(random32());
            pk.nature(pksm::Nature{u8(rand() % 25)});
            pk.gender(pksm::PKX::genderFromRatio(random32(), pk.genderType()));
            pk.setAbility(u8(rand() % 2)); // PKSM-Core: 0 first, 1 second, 2 hidden
            randomIVs(pk);
        }
    }

    namespace
    {
        const std::vector<Slot>& ofVersion(int version);
    }

    const std::vector<Slot>& of(const pksm::Sav& sav) { return ofVersion(int(u8(sav.version()))); }

    std::vector<std::string> elsewhere(const pksm::Sav& sav, u16 species)
    {
        static const std::pair<int, const char*> games[] = {{35, "Red"}, {36, "Blue"}, {38, "Yellow"}, {39, "Gold"},
            {40, "Silver"}, {41, "Crystal"}, {2, "Ruby"}, {1, "Sapphire"}, {3, "Emerald"}, {4, "FireRed"},
            {5, "LeafGreen"}, {10, "Diamond"}, {11, "Pearl"}, {12, "Platinum"}, {7, "HeartGold"}, {8, "SoulSilver"},
            {21, "Black"}, {20, "White"}, {23, "Black 2"}, {22, "White 2"}, {24, "X"}, {25, "Y"}, {27, "Omega Ruby"},
            {26, "Alpha Sapphire"}, {30, "Sun"}, {31, "Moon"}, {32, "Ultra Sun"}, {33, "Ultra Moon"}};
        std::vector<std::string> out;
        int self = int(u8(sav.version()));
        for (auto& [version, name] : games)
        {
            if (version == self)
            {
                continue;
            }
            for (const Slot& s : ofVersion(version))
            {
                if (s.species == species)
                {
                    out.push_back(name);
                    break;
                }
            }
        }
        return out;
    }

    namespace
    {
    const std::vector<Slot>& ofVersion(int version)
    {
        static std::map<int, std::vector<Slot>> cache;
        auto found  = cache.find(version);
        if (found != cache.end())
        {
            return found->second;
        }
        std::vector<Slot>& out = cache[version];
        std::vector<u8> data;
        if (files::read("romfs:/enc/" + std::to_string(version) + ".bin", data) && data.size() >= 2)
        {
            u16 n = rd16(data.data());
            for (u16 i = 0; i < n && 2 + (i + 1) * 12 <= int(data.size()); i++)
            {
                const u8* p = data.data() + 2 + i * 12;
                out.push_back({rd16(p), p[2], p[3], p[4], p[5], rd16(p + 6), p[8], p[9], p[10], p[11]});
            }
        }
        return out;
    }
    }

    std::string place(const pksm::Sav& sav, const Slot& s)
    {
        static std::map<int, std::vector<std::string>> names;
        int gen = genOf(sav);
        auto it = names.find(gen);
        if (it == names.end())
        {
            std::vector<std::string> lines;
            std::string text;
            if (files::readText("romfs:/enc/loc" + std::to_string(gen) + ".txt", text))
            {
                size_t a = 0;
                while (a <= text.size())
                {
                    size_t b = text.find('\n', a);
                    std::string line = text.substr(a, b == std::string::npos ? std::string::npos : b - a);
                    if (!line.empty() && line.back() == '\r')
                    {
                        line.pop_back();
                    }
                    lines.push_back(line);
                    if (b == std::string::npos)
                    {
                        break;
                    }
                    a = b + 1;
                }
            }
            it = names.emplace(gen, std::move(lines)).first;
        }
        if (s.location < it->second.size() && !it->second[s.location].empty())
        {
            return it->second[s.location];
        }
        return "Place " + std::to_string(s.location);
    }

    std::string kind(const pksm::Sav& sav, const Slot& s)
    {
        int gen = genOf(sav);
        static const char* g12[] = {"Grass", "Surf", "Old Rod", "Good Rod", "Super Rod", "Rock Smash", "Headbutt",
            "Headbutt (rare)", "Bug Contest"};
        static const char* g3[]  = {"Grass", "Surf", "Old Rod", "Good Rod", "Super Rod", "Rock Smash", "Swarm",
            "Swarm (fishing)"};
        static const char* g4[]  = {"Grass", "Surf", "Old Rod", "Good Rod", "Super Rod", "Rock Smash", "Headbutt",
            "Headbutt (rare)", "Bug Contest", "Honey tree", "Safari grass", "Safari surf", "Safari Old Rod",
            "Safari Good Rod", "Safari Super Rod"};
        static const char* g5[]  = {"Grass / cave", "Dark grass", "Surf", "Fishing", "Swarm", "Hidden Grotto"};
        static const char* g6[]  = {"Grass / cave", "Flowers / tall grass", "Surf", "Old Rod", "Good Rod",
            "Super Rod", "Rock Smash", "Horde", "Friend Safari"};
        static const char* g7[]  = {"Wild", "SOS call"};
        auto pick = [&](const char* const* t, size_t n) { return s.type < n ? std::string(t[s.type]) : "Other"; };
        std::string k;
        switch (gen)
        {
            case 1:
            case 2: k = pick(g12, 9); break;
            case 3: k = pick(g3, 8); break;
            case 4: k = pick(g4, 15); break;
            case 5: k = pick(g5, 6); break;
            case 6: k = pick(g6, 9); break;
            default: k = pick(g7, 2); break;
        }
        if (s.time)
        {
            std::string when;
            if (s.time & 2)
            {
                when += "morning";
            }
            if (s.time & 4)
            {
                when += when.empty() ? "day" : "/day";
            }
            if (s.time & 8)
            {
                when += when.empty() ? "night" : "/night";
            }
            k += " (" + when + ")";
        }
        return k;
    }

    namespace
    {
        // Gold/Silver/Crystal headbutt trees: which ones shake for a trainer ID (PKHeX EncounterSlot2.IsTreeAvailable).
        bool treeAvailable2(const Slot& s, u16 tid)
        {
            static const u8 places[] = {2, 4, 5, 8, 11, 12, 14, 15, 18, 20, 21, 25, 26, 34, 37, 38, 39, 91, 92};
            static const u32 trees[] = {0x3FF3FF, 0x0FF3FF, 0x3FE3FF, 0x3EE3FF, 0x2403FF, 0x37F3FF, 0x3FF3FF,
                0x0013FE, 0x2613FF, 0x3FF3FF, 0x2B93FF, 0x3FF3FF, 0x1843FF, 0x3FF3FF, 0x3FF3FF, 0x3FF3FF, 0x2FF3FF,
                0x2001FF, 0x2BB3FF};
            for (size_t i = 0; i < sizeof(places); i++)
            {
                if (places[i] == u8(s.location))
                {
                    int pivot = tid % 10;
                    return trees[i] >> (s.type == 6 ? pivot : pivot + 12) & 1;
                }
            }
            return false;
        }

        // D/P/Pt: Munchlax only comes to the 3-4 honey trees picked by the trainer's ID (PKHeX HoneyTreeUtil).
        bool munchlaxTree(const Slot& s, u16 tid, u16 sid)
        {
            static const u8 treeLocation[] = {20, 20, 21, 22, 23, 24, 25, 25, 26, 27, 27, 28, 29, 30, 33, 36, 37, 47,
                48, 49, 58};
            u32 id = u32(sid) << 16 | tid;
            u8 t[4] = {u8((id >> 24) % 21), u8((id >> 16 & 0xFF) % 21), u8((id >> 8 & 0xFF) % 21), u8((id & 0xFF) % 21)};
            for (int i = 1; i < 4; i++)
            {
                for (int j = 0; j < i; j++)
                {
                    if (t[i] == t[j] && ++t[i] >= 21)
                    {
                        t[i] = 0;
                    }
                }
            }
            for (u8 i : t)
            {
                if (treeLocation[i] == s.location)
                {
                    return true;
                }
            }
            return false;
        }
    }

    std::string notCatchable(const pksm::Sav& sav, const Slot& s)
    {
        int gen   = genOf(sav);
        u8 v      = u8(sav.version());
        bool hgss = v == 7 || v == 8;
        if (gen == 2 && (s.type == 6 || s.type == 7) && !treeAvailable2(s, sav.TID()))
        {
            return "This tree does not shake for your trainer ID (the game picks the trees from it).";
        }
        if (gen == 4 && !hgss && s.type == 9 && s.species == 446 && !munchlaxTree(s, sav.TID(), sav.SID()))
        {
            return "Munchlax only comes to 3 or 4 honey trees, picked by your trainer ID, and none is here.";
        }
        if (gen == 4 && (hgss ? s.type > 14 : (s.type > 4 && s.type != 9)))
        {
            return "This kind of encounter is not modelled yet.";
        }
        if (gen == 3 && s.type > 7)
        {
            return "This kind of encounter is not modelled yet.";
        }
        return "";
    }

    bool catchable(const pksm::Sav& sav, const Slot& s)
    {
        switch (genOf(sav))
        {
            case 1: return true;
            case 2:
            case 3:
            case 4: return notCatchable(sav, s).empty();
            case 5: return true;
            case 6: return notCatchable(sav, s).empty();
            default: return true;
        }
    }

    namespace
    {
        // Common to wild and static Pokémon: species, form, level, met data, nickname, EVs.
        void base(pksm::PKX& pk, int gen, u16 species, u8 form, u8 level, u16 location)
        {
            pk.species(pksm::Species{species});
            pk.alternativeForm(form >= 30 ? 0 : form); // 30+ marks "any form" in PKHeX's tables
            pk.level(level);
            pk.metLevel(level);
            pk.egg(false);
            pk.heldItem(u16(0));
            pk.nickname(i18n::species(pk.language(), pk.species()));
            pk.nicknamed(false);
            for (pksm::Stat st : {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD,
                     pksm::Stat::SPATK, pksm::Stat::SPDEF})
            {
                pk.ev(st, 0);
            }
            if (gen >= 3)
            {
                pk.metLocation(location);
                pk.eggLocation(0);
            }
        }

        void finish(pksm::PKX& pk, const pksm::Sav& sav, const u16* fixedMoves)
        {
            u8 version = u8(sav.version());
            if (fixedMoves && fixedMoves[0])
            {
                for (u8 i = 0; i < 4; i++)
                {
                    pk.move(i, pksm::Move{fixedMoves[i]});
                    pk.PPUp(i, 0);
                }
            }
            else
            {
                std::vector<u16> moves = learnsets::levelUpMoves(u16(pk.species()), version, pk.level());
                for (u8 i = 0; i < 4; i++)
                {
                    u16 m = moves.size() > i ? moves[moves.size() - 1 - i] : 0;
                    pk.move(i, pksm::Move{m});
                    pk.PPUp(i, 0);
                }
            }
            pk.fixMoves();
            pk.healPP();
            pk.otFriendship(pk.baseFriendship());
            pk.refreshChecksum();
        }

        void geo(pksm::PKX& pk, const pksm::Sav& sav, int gen)
        {
            if (gen == 6)
            {
                auto& pk6 = static_cast<pksm::PK6&>(pk);
                pk6.country(sav.country());
                pk6.region(sav.subRegion());
                pk6.consoleRegion(sav.consoleRegion());
                // PKHeX MemoryApplicator.SetRandomMemory6: memory 63, a feeling it allows, full intensity.
                pk6.otMemory(63);
                pk6.otFeeling(1);
                pk6.otIntensity(7);
            }
            else if (gen == 7)
            {
                auto& pk7 = static_cast<pksm::PK7&>(pk);
                pk7.country(sav.country());
                pk7.region(sav.subRegion());
                pk7.consoleRegion(sav.consoleRegion());
            }
        }
    }

    void make(pksm::PKX& pk, const pksm::Sav& sav, const Slot& s)
    {
        int gen     = genOf(sav);
        u8 version  = u8(sav.version());
        bool hgss   = version == 7 || version == 8;
        pk.species(pksm::Species{s.species});
        pk.alternativeForm(s.form >= 30 ? 0 : s.form); // 30+ marks "any form" in PKHeX's tables
        pk.level(s.min);
        pk.metLevel(s.min);
        pk.egg(false);
        pk.heldItem(u16(0));
        pk.nickname(i18n::species(pk.language(), pk.species()));
        pk.nicknamed(false);
        for (pksm::Stat st : {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD, pksm::Stat::SPATK,
                 pksm::Stat::SPDEF})
        {
            pk.ev(st, 0);
        }
        if (gen >= 3)
        {
            pk.metLocation(s.location);
            pk.eggLocation(0);
        }
        switch (gen)
        {
            case 1: randomIVs(pk); break;
            case 2:
                randomIVs(pk);
                if (version == 41) // Crystal keeps where, when and at what level
                {
                    auto& pk2 = static_cast<pksm::PK2&>(pk);
                    pk2.metLocation(s.location);
                    pk2.metLevel(s.min);
                    u8 times[3], n = 0;
                    for (u8 t = 1; t <= 3; t++)
                    {
                        if (!s.time || (s.time & (1 << t)))
                        {
                            times[n++] = t;
                        }
                    }
                    pk2.metTime(n ? times[rand() % n] : 1);
                }
                break;
            case 3:
            case 4:
            {
                classic(pk, s, gen, hgss);
                if (gen == 4)
                {
                    static_cast<pksm::PK4&>(pk).encounterType(s.tile);
                }
                // The Safari Zone and the Great Marsh need the Safari Ball, the Bug Contest the Sport Ball.
                bool safari = gen == 3 ? (s.location == 57 || s.location == 136) : (s.location == 52 || s.location == 202);
                pk.ball(pksm::Ball{u8(safari ? 5 : gen == 4 && hgss && s.type == 8 ? 24 : 4)});
                break;
            }
            case 5: monochrome(pk, s.type == 5); break;
            default:
                modern(pk);
                if (gen == 6 && s.type == 8 && (version == 24 || version == 25))
                {
                    // Friend Safari (PKHeX EncounterSlot6XY): two perfect IVs, and the hidden ability can come too.
                    static const pksm::Stat stats[] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF,
                        pksm::Stat::SPD, pksm::Stat::SPATK, pksm::Stat::SPDEF};
                    int first = rand() % 6, second = (first + 1 + rand() % 5) % 6;
                    pk.iv(stats[first], 31);
                    pk.iv(stats[second], 31);
                    pk.setAbility(u8(rand() % 3));
                }
                if (s.form == 30) // Vivillon's line: the pattern of the save's country and region
                {
                    pk.alternativeForm(vivillonPattern(sav.country(), sav.subRegion()));
                }
                if (gen == 6)
                {
                    auto& pk6 = static_cast<pksm::PK6&>(pk);
                    pk6.country(sav.country());
                    pk6.region(sav.subRegion());
                    pk6.consoleRegion(sav.consoleRegion());
                    // PKHeX MemoryApplicator.SetRandomMemory6: memory 63, a feeling it allows, full intensity.
                    pk6.otMemory(63);
                    pk6.otFeeling(1);
                    pk6.otIntensity(7);
                }
                else
                {
                    auto& pk7 = static_cast<pksm::PK7&>(pk);
                    pk7.country(sav.country());
                    pk7.region(sav.subRegion());
                    pk7.consoleRegion(sav.consoleRegion());
                }
                break;
        }
        std::vector<u16> moves = learnsets::levelUpMoves(s.species, version, pk.level());
        for (u8 i = 0; i < 4; i++)
        {
            u16 m = moves.size() > i ? moves[moves.size() - 1 - i] : 0;
            pk.move(i, pksm::Move{m});
            pk.PPUp(i, 0);
        }
        pk.fixMoves();
        pk.healPP();
        pk.otFriendship(pk.baseFriendship());
        pk.refreshChecksum();
    }

    const std::vector<Static>& statics(const pksm::Sav& sav)
    {
        static std::map<int, std::vector<Static>> cache;
        int version = int(u8(sav.version()));
        auto found  = cache.find(version);
        if (found != cache.end())
        {
            return found->second;
        }
        std::vector<Static>& out = cache[version];
        std::vector<u8> data;
        if (files::read("romfs:/enc/s" + std::to_string(version) + ".bin", data) && data.size() >= 2)
        {
            u16 n = rd16(data.data());
            for (u16 i = 0; i < n && 2 + (i + 1) * 32 <= int(data.size()); i++)
            {
                const u8* p = data.data() + 2 + i * 32;
                Static st;
                st.species = rd16(p);
                st.form = p[2], st.level = p[3];
                st.location = rd16(p + 4);
                st.ball = p[6], st.ability = p[7], st.neverShiny = p[8], st.flawless = p[9];
                st.gender = p[10], st.nature = p[11], st.tile = p[12], st.gift = p[13];
                for (int m = 0; m < 4; m++)
                {
                    st.moves[m]   = rd16(p + 14 + 2 * m);
                    st.relearn[m] = rd16(p + 22 + 2 * m);
                }
                out.push_back(st);
            }
        }
        return out;
    }

    std::string place(const pksm::Sav& sav, const Static& s)
    {
        Slot as;
        as.location = s.location;
        return place(sav, as);
    }

    void make(pksm::PKX& pk, const pksm::Sav& sav, const Static& s)
    {
        int gen = genOf(sav);
        base(pk, gen, s.species, s.form, s.level, s.location);
        pk.ball(pksm::Ball{s.ball});
        u16 tid = pk.TID(), sid = pk.SID();
        if (gen == 3 || gen == 4)
        {
            // Method 1: the PID (low half first) and the IVs from four calls in a row, not shiny.
            Lcrng r{random32()};
            u32 pid;
            for (;;)
            {
                u32 a = r.next16();
                u32 b = r.next16();
                pid   = b << 16 | a;
                if (!shiny3(tid, sid, pid))
                {
                    break;
                }
            }
            u32 iv32 = r.ivs();
            if (gen == 4)
            {
                pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
                pk.ability(pk.abilities(u8(pid & 1)));
                pk.PID(pid);
                static_cast<pksm::PK4&>(pk).encounterType(s.tile);
            }
            else
            {
                pk.PID(pid);
                pk.setAbility(u8(pid & 1));
            }
            setIVs(pk, iv32);
        }
        else if (gen == 5)
        {
            // PKHeX EncounterStatic5 + MonochromeRNG.Generate: a single-ability encounter forces bit 16; the
            // wild xor applies to non-gift, non-hidden-ability, unlocked ones.
            bool wildXor = s.neverShiny == 0 && !s.gift && s.ability != 3;
            u32 pid      = random32();
            if (s.gender != 255)
            {
                u8 gr = pk.genderType();
                if (gr != 0 && gr != 254 && gr != 255)
                {
                    pid = (pid & 0xFFFFFF00) | (s.gender == 0 ? u32(gr + rand() % (254 - gr)) : u32(1 + rand() % (gr - 1)));
                }
            }
            if (shiny3(tid, sid, pid))
            {
                pid ^= 0x10000000;
            }
            if (s.ability != 0)
            {
                pid = (pid & ~0x10000u) | (s.ability == 2 ? 0x10000u : 0u);
            }
            if (wildXor && (((pid >> 31) ^ (pid & 1) ^ ((tid ^ sid) & 1)) != 0))
            {
                pid ^= 0x80000000;
            }
            pk.nature(pksm::Nature{s.nature != 255 ? s.nature : u8(rand() % 25)});
            pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
            u8 index = s.ability == 3 ? 2 : u8((pid >> 16) & 1);
            pk.ability(pk.abilities(index));
            static_cast<pksm::PK5&>(pk).hiddenAbility(index == 2);
            pk.PID(pid);
            randomIVs(pk);
        }
        else
        {
            // Gen 6/7 (PKHeX EncounterStatic6/7.SetPINGA): random PID and EC, a shiny lock, perfect IVs.
            u32 pid;
            do
            {
                pid = random32();
            } while (((tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) >> 4) == 0);
            pk.PID(pid);
            pk.encryptionConstant(random32());
            randomIVs(pk);
            static const pksm::Stat stats[] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD,
                pksm::Stat::SPATK, pksm::Stat::SPDEF};
            int perfect[6] = {0, 1, 2, 3, 4, 5};
            for (int i = 5; i > 0; i--)
            {
                std::swap(perfect[i], perfect[rand() % (i + 1)]);
            }
            for (int i = 0; i < s.flawless && i < 6; i++)
            {
                pk.iv(stats[perfect[i]], 31);
            }
            pk.nature(pksm::Nature{s.nature != 255 ? s.nature : u8(rand() % 25)});
            pk.gender(s.gender != 255 ? pksm::Gender{s.gender} : pksm::PKX::genderFromRatio(random32(), pk.genderType()));
            u8 index = s.ability == 0 ? u8(rand() % 2) : u8(s.ability - 1);
            pk.setAbility(index);
            geo(pk, sav, gen);
            if (gen == 7 && s.relearn[0])
            {
                for (u8 i = 0; i < 4; i++)
                {
                    pk.relearnMove(i, pksm::Move{s.relearn[i]});
                }
                finish(pk, sav, s.relearn);
                return;
            }
        }
        finish(pk, sav, s.moves);
    }

    namespace
    {
        std::string langCode(pksm::Language l)
        {
            switch (l)
            {
                case pksm::Language::JPN: return "ja";
                case pksm::Language::FRE: return "fr";
                case pksm::Language::ITA: return "it";
                case pksm::Language::GER: return "de";
                case pksm::Language::SPA: return "es";
                case pksm::Language::KOR: return "ko";
                case pksm::Language::CHS: return "zh-Hans";
                case pksm::Language::CHT: return "zh-Hant";
                default: return "en";
            }
        }

        std::string pickName(const std::string& all, const std::string& lang)
        {
            std::string fallback;
            size_t a = 0;
            while (a < all.size())
            {
                size_t b = all.find('|', a);
                std::string item = all.substr(a, b == std::string::npos ? std::string::npos : b - a);
                size_t eq = item.find('=');
                if (eq != std::string::npos)
                {
                    if (item.substr(0, eq) == lang)
                    {
                        return item.substr(eq + 1);
                    }
                    if (item.substr(0, eq) == "en")
                    {
                        fallback = item.substr(eq + 1);
                    }
                }
                if (b == std::string::npos)
                {
                    break;
                }
                a = b + 1;
            }
            return fallback;
        }

        std::vector<std::string> fields(const std::string& line, char sep)
        {
            std::vector<std::string> out;
            size_t a = 0;
            for (;;)
            {
                size_t b = line.find(sep, a);
                out.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
                if (b == std::string::npos)
                {
                    return out;
                }
                a = b + 1;
            }
        }
    }

    const std::vector<Trade>& trades(const pksm::Sav& sav)
    {
        static std::map<int, std::vector<Trade>> cache;
        int version = int(u8(sav.version()));
        auto found  = cache.find(version);
        if (found != cache.end())
        {
            return found->second;
        }
        std::vector<Trade>& out = cache[version];
        std::string text;
        if (!files::readText("romfs:/enc/t" + std::to_string(version) + ".txt", text))
        {
            return out;
        }
        for (const std::string& line : fields(text, '\n'))
        {
            auto f = fields(line, '\t');
            if (f.size() < 20)
            {
                continue;
            }
            Trade t;
            t.gen      = atoi(f[0].c_str());
            t.species  = u16(atoi(f[1].c_str()));
            t.form     = u8(atoi(f[2].c_str()));
            t.level    = u8(atoi(f[3].c_str()));
            t.location = u16(atoi(f[4].c_str()));
            t.fixedPid = f[5] != "R";
            t.pid      = t.fixedPid ? u32(strtoul(f[5].c_str(), nullptr, 16)) : 0;
            t.tid      = u16(atoi(f[6].c_str()));
            t.sid      = u16(atoi(f[7].c_str()));
            t.otGender = u8(atoi(f[8].c_str()));
            t.gender   = f[9] == "R" ? -1 : atoi(f[9].c_str());
            t.nature   = f[10] == "R" ? -1 : atoi(f[10].c_str());
            t.ability  = u8(atoi(f[11].c_str()));
            auto ivs = fields(f[12], ','), contest = fields(f[13], ','), moves = fields(f[14], ','), mem = fields(f[15], ',');
            for (int i = 0; i < 6; i++)
            {
                t.ivs[i]     = i < int(ivs.size()) ? atoi(ivs[i].c_str()) : -1;
                t.contest[i] = u8(i < int(contest.size()) ? atoi(contest[i].c_str()) : 0);
            }
            for (int i = 0; i < 4; i++)
            {
                t.moves[i]  = u16(i < int(moves.size()) ? atoi(moves[i].c_str()) : 0);
                t.memory[i] = u16(i < int(mem.size()) ? atoi(mem[i].c_str()) : 0);
            }
            t.fixedNickname = f[16] == "1";
            t.evolve        = f[17] == "1";
            t.nicknames     = f[18];
            t.trainers      = f[19];
            out.push_back(t);
        }
        return out;
    }

    std::string tradeTrainer(const pksm::Sav& sav, const Trade& t) { return pickName(t.trainers, langCode(sav.language())); }

    void make(pksm::PKX& pk, const pksm::Sav& sav, const Trade& t)
    {
        int gen = genOf(sav);
        base(pk, gen, t.species, t.form, t.level, t.location);
        std::string lang = langCode(sav.language());
        pk.TID(t.tid);
        pk.SID(t.sid);
        pk.otName(pickName(t.trainers, lang));
        pk.otGender(pksm::Gender{t.otGender});
        pk.ball(pksm::Ball{4});
        u8 abilityIndex;
        u32 pid;
        if (t.fixedPid)
        {
            pid          = t.pid;
            abilityIndex = t.ability == 0 ? u8(gen <= 4 ? (pid & 1) : (pid >> 16) & 1) : u8(t.ability - 1);
        }
        else if (gen == 5)
        {
            // B2W2 (PKHeX EncounterTrade5B2W2): the Monochrome rules without the wild xor, ability bit forced.
            pid = random32();
            if (t.gender >= 0 && t.gender < 2)
            {
                u8 gr = pk.genderType();
                if (gr != 0 && gr != 254 && gr != 255)
                {
                    pid = (pid & 0xFFFFFF00) | (t.gender == 0 ? u32(gr + rand() % (254 - gr)) : u32(1 + rand() % (gr - 1)));
                }
            }
            if (shiny3(pk.TID(), pk.SID(), pid))
            {
                pid ^= 0x10000000;
            }
            if (t.ability == 1 || t.ability == 2)
            {
                pid = (pid & ~0x10000u) | (t.ability == 2 ? 0x10000u : 0u);
            }
            abilityIndex = t.ability == 3 ? 2 : u8((pid >> 16) & 1);
        }
        else
        {
            // Gen 6/7: random, not shiny.
            pid = random32();
            if (((pk.TID() ^ pk.SID() ^ (pid >> 16) ^ (pid & 0xFFFF)) >> 4) == 0)
            {
                pid ^= 0x10000000;
            }
            abilityIndex = t.ability == 0 ? u8(rand() % 2) : u8(t.ability - 1);
        }
        // Everything that re-rolls the PID in PKSM-Core first (gender, nature, ability number), the PID last.
        if (gen >= 5 || gen == 4)
        {
            if (t.gender == 2)
            {
                pk.gender(pksm::Gender::Genderless);
            }
            else if (t.gender >= 0)
            {
                pk.gender(pksm::Gender{u8(t.gender)});
            }
            else
            {
                pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
            }
        }
        if (gen >= 5)
        {
            pk.nature(pksm::Nature{u8(t.nature >= 0 ? t.nature : rand() % 25)});
        }
        if (gen >= 6)
        {
            pk.setAbility(u8(abilityIndex));
            pk.PID(pid);
            pk.encryptionConstant(random32());
        }
        else
        {
            pk.ability(pk.abilities(abilityIndex));
            if (gen == 5)
            {
                static_cast<pksm::PK5&>(pk).hiddenAbility(abilityIndex == 2);
            }
            pk.PID(pid);
            if (gen == 3)
            {
                pk.setAbility(abilityIndex); // Gen 3 keeps it as a bit, beside the PID
            }
        }
        static const pksm::Stat stats[] = {pksm::Stat::HP, pksm::Stat::ATK, pksm::Stat::DEF, pksm::Stat::SPD,
            pksm::Stat::SPATK, pksm::Stat::SPDEF};
        for (int i = 0; i < 6; i++)
        {
            pk.iv(stats[i], t.ivs[i] >= 0 ? u8(t.ivs[i]) : u8(rand() % (pk.maxIV() + 1)));
        }
        if (gen <= 4)
        {
            for (u8 i = 0; i < 6; i++)
            {
                pk.contest(i, t.contest[i]);
            }
        }
        if (t.fixedNickname)
        {
            pk.nickname(pickName(t.nicknames, lang));
            pk.nicknamed(true);
        }
        if (gen >= 6)
        {
            geo(pk, sav, gen);
            if (gen == 6)
            {
                auto& pk6 = static_cast<pksm::PK6&>(pk);
                pk6.otMemory(u8(t.memory[0]));
                pk6.otIntensity(u8(t.memory[1]));
                pk6.otFeeling(u8(t.memory[2]));
                pk6.otTextVar(t.memory[3]);
                pk6.htName(sav.otName());
                pk6.htGender(sav.gender());
                pk6.currentHandler(pksm::PKXHandler::NonOT);
                pk6.htFriendship(pk.baseFriendship());
            }
            else
            {
                auto& pk7 = static_cast<pksm::PK7&>(pk);
                pk7.otMemory(u8(t.memory[0]));
                pk7.otIntensity(u8(t.memory[1]));
                pk7.otFeeling(u8(t.memory[2]));
                pk7.otTextVar(t.memory[3]);
                pk7.htName(sav.otName());
                pk7.htGender(sav.gender());
                pk7.currentHandler(pksm::PKXHandler::NonOT);
                pk7.htFriendship(pk.baseFriendship());
            }
        }
        finish(pk, sav, t.moves);
        if (t.evolve)
        {
            pk.species(pksm::Species{u16(t.species + 1)});
            if (!t.fixedNickname)
            {
                pk.nickname(i18n::species(pk.language(), pk.species()));
            }
            pk.refreshChecksum();
        }
    }
}

#include "encounters.hpp"
#include "files.hpp"
#include "learnsets.hpp"
#include "pkdata.hpp"

#include "pkx/PK2.hpp"
#include "pkx/PK4.hpp"
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
                    case 6:
                    case 7: lo = 0, hi = 49; return slot == 0;
                    default: return false;
                }
            }
            else if (gen == 4)
            {
                (void)hgss;
                switch (type)
                {
                    case 0: t = grass, n = 12; break;
                    case 1: t = surf, n = 5; break;
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
            bool levelRoll = gen == 3 || s.type != 0; // Gen 4: grass slots have a fixed level
            u16 tid = pk.TID(), sid = pk.SID();
            Lcrng r{random32()};
            for (;;)
            {
                u32 esv = gen == 4 && !hgss ? r.next16() / 656u : r.next16() % 100;
                if (int(esv) < lo || int(esv) > hi)
                {
                    continue;
                }
                u32 lv     = levelRoll ? r.next16() : 0;
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
                    u8 level = s.min;
                    if (levelRoll && s.max > s.min)
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
        void monochrome(pksm::PKX& pk)
        {
            u16 tid = pk.TID(), sid = pk.SID();
            u32 pid = random32();
            if (shiny3(tid, sid, pid))
            {
                pid ^= 0x10000000;
            }
            if (((pid >> 31) ^ (pid & 1) ^ ((tid ^ sid) & 1)) != 0)
            {
                pid ^= 0x80000000;
            }
            // Gender and ability-number setters pick a new PID: everything first, the PID last.
            pk.nature(pksm::Nature{u8(rand() % 25)});
            pk.gender(pksm::PKX::genderFromRatio(pid, pk.genderType()));
            pk.ability(pk.abilities(u8((pid >> 16) & 1)));
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
            pk.setAbility(u8(1 << (rand() % 2)));
            randomIVs(pk);
        }
    }

    const std::vector<Slot>& of(const pksm::Sav& sav)
    {
        static std::map<int, std::vector<Slot>> cache;
        int version = int(u8(sav.version()));
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
            for (u16 i = 0; i < n && 2 + (i + 1) * 11 <= int(data.size()); i++)
            {
                const u8* p = data.data() + 2 + i * 11;
                out.push_back({rd16(p), p[2], p[3], p[4], p[5], rd16(p + 6), p[8], p[9], p[10]});
            }
        }
        return out;
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

    bool catchable(const pksm::Sav& sav, const Slot& s)
    {
        switch (genOf(sav))
        {
            case 1: return true;
            case 2: return s.type <= 5; // headbutt trees depend on the trainer ID, the Bug Contest on its ball
            case 3: return s.species != 201 && (s.type <= 4 || s.type == 6 || s.type == 7);
            case 4: return s.species != 201 && s.type <= 1;
            case 5: return s.type != 5;
            case 6: return s.type != 8 && s.species != 666; // Vivillon's pattern follows the console's region
            default: return true;
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
                classic(pk, s, gen, hgss);
                if (gen == 4)
                {
                    static_cast<pksm::PK4&>(pk).encounterType(s.tile);
                }
                break;
            case 5: monochrome(pk); break;
            default:
                modern(pk);
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
}

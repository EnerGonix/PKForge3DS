#include "learnsets.hpp"
#include "files.hpp"

#include <algorithm>
#include <cstring>

namespace learnsets
{
    namespace
    {
        std::vector<u8> data;
        bool tried = false;
        u16 groupCount = 0, speciesCount = 0;

        template <typename T>
        T at(u32 offset)
        {
            T v{};
            if (offset + sizeof(T) <= data.size())
            {
                memcpy(&v, data.data() + offset, sizeof(T));
            }
            return v;
        }

        bool ready()
        {
            if (!tried)
            {
                tried = true;
                if (files::read("romfs:/learnsets.bin", data) && data.size() > 8 && memcmp(data.data(), "LRN1", 4) == 0)
                {
                    groupCount   = at<u16>(4);
                    speciesCount = at<u16>(6);
                }
                else
                {
                    data.clear();
                }
            }
            return !data.empty();
        }

        u32 dirAt() { return 8 + speciesCount * 2; }

        // PokeAPI version groups by PKSM game version (pksm::GameVersion numbers).
        std::vector<u8> ownGroups(u8 version)
        {
            switch (version)
            {
                case 1: case 2: return {5};            // Sapphire, Ruby
                case 3: return {6};                    // Emerald
                case 4: case 5: return {7};            // FireRed, LeafGreen
                case 15: return {12, 13};              // Colosseum, XD
                case 10: case 11: return {8};          // Diamond, Pearl
                case 12: return {9};                   // Platinum
                case 7: case 8: return {10};           // HeartGold, SoulSilver
                case 20: case 21: return {11};         // White, Black
                case 22: case 23: return {14};         // White 2, Black 2
                case 24: case 25: return {15};         // X, Y
                case 26: case 27: return {16};         // Alpha Sapphire, Omega Ruby
                case 30: case 31: return {17};         // Sun, Moon
                case 32: case 33: return {18};         // Ultra Sun, Ultra Moon
                case 35: case 36: case 37: return {1}; // Red, Green, Blue (VC)
                case 38: return {2};                   // Yellow
                case 39: case 40: return {3};          // Gold, Silver
                case 41: return {4};                   // Crystal
                case 42: case 43: return {19};         // Let's Go
                case 44: case 45: return {20, 21, 22}; // Sword, Shield and their DLC
                case 47: return {24};                  // Legends: Arceus
                case 50: case 51: return {25, 26, 27}; // Scarlet, Violet and their DLC
                case 52: return {30};                  // Legends: Z-A
                default: return {};
            }
        }

        int generationOf(u8 group)
        {
            for (u16 i = 0; i < groupCount; i++)
            {
                if (at<u8>(dirAt() + i * 6) == group)
                {
                    return at<u8>(dirAt() + i * 6 + 1);
                }
            }
            return 0;
        }

        u32 record(u8 group, u16 species)
        {
            if (species == 0 || species >= speciesCount)
            {
                return 0;
            }
            for (u16 i = 0; i < groupCount; i++)
            {
                u32 entry = dirAt() + i * 6;
                if (at<u8>(entry) == group)
                {
                    return at<u32>(at<u32>(entry + 2) + species * 4);
                }
            }
            return 0;
        }

        bool inRecord(u32 rec, u16 move)
        {
            u16 n = at<u16>(rec);
            for (u16 i = 0; i < n; i++)
            {
                if (at<u16>(rec + 2 + i * 2) == move)
                {
                    return true;
                }
            }
            return false;
        }

        // The groups a Pokémon in this game may have learnt its moves in: its own, and every earlier
        // generation's (transfers keep moves). Let's Go only takes what was learnt there.
        std::vector<u8> sourceGroups(u8 version)
        {
            std::vector<u8> own = ownGroups(version);
            if (own.empty())
            {
                return own;
            }
            if (own[0] == 19)
            {
                return own;
            }
            int gen = generationOf(own[0]);
            std::vector<u8> out;
            for (u16 i = 0; i < groupCount; i++)
            {
                u8 g = at<u8>(dirAt() + i * 6);
                int gg = at<u8>(dirAt() + i * 6 + 1);
                bool letsGo = g == 19 && gen < 8; // Let's Go Pokémon only leave through HOME, to Gen 8 on
                if (gg <= gen && !letsGo)
                {
                    out.push_back(g);
                }
            }
            return out;
        }
    }

    bool covers(u8 version) { return ready() && !ownGroups(version).empty() && record(ownGroups(version)[0], 1) != 0; }

    bool canKnow(u16 species, u16 move, u8 version)
    {
        if (!ready())
        {
            return true;
        }
        std::vector<u8> groups = sourceGroups(version);
        // The species and everything it evolved from.
        for (u16 s = species; s != 0 && s < speciesCount; s = at<u16>(8 + s * 2))
        {
            for (u8 g : groups)
            {
                u32 rec = record(g, s);
                if (rec && inRecord(rec, move))
                {
                    return true;
                }
            }
        }
        return false;
    }

    u8 hatchCycles(u16 species)
    {
        static std::vector<u8> table;
        if (table.empty())
        {
            files::read("romfs:/hatch.bin", table);
        }
        return species < table.size() ? table[species] : 0;
    }

    std::vector<u16> levelUpMoves(u16 species, u8 version, u8 level)
    {
        std::vector<std::pair<u8, u16>> found;
        if (ready())
        {
            for (u8 g : ownGroups(version))
            {
                u32 rec = record(g, species);
                if (!rec)
                {
                    continue;
                }
                u32 ups = rec + 2 + at<u16>(rec) * 2;
                u16 n   = at<u16>(ups);
                for (u16 i = 0; i < n; i++)
                {
                    u16 move = at<u16>(ups + 2 + i * 3);
                    u8 lvl   = at<u8>(ups + 4 + i * 3);
                    if (lvl <= level)
                    {
                        found.push_back({lvl, move});
                    }
                }
            }
        }
        std::stable_sort(found.begin(), found.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<u16> out;
        for (auto& [lvl, move] : found)
        {
            out.erase(std::remove(out.begin(), out.end(), move), out.end());
            out.push_back(move);
        }
        return out;
    }
}

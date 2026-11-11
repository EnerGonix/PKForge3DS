// Wild encounters, as the Android PKForge's encounter gallery (PKHeX's EncounterMovesetGenerator): where a
// species can be caught in the open save's game, and a Pokémon made from one of those encounters the way the
// game makes it, so PKHeX's legality check accepts it. Tables from PKHeX (tools/encounters.py ->
// romfs/enc/<version>.bin), the RNG methods ported from PKHeX's GenerateMethodH/J/K and MonochromeRNG:
//  - Generation 3: Method H (slot, level, nature, then a PID with that nature, then the IVs)
//  - Generation 4: Method J (Diamond, Pearl, Platinum) and Method K (HeartGold, SoulSilver), with the ground tile
//  - Generation 5: the Monochrome PID rules (ability bit 16, the wild xor), nature apart
//  - Generations 1, 2, 6, 7: random DVs/PID, as PKHeX does
// Not made (listed only): Gen 4 honey trees, Safari and Bug Contest, headbutt trees (they depend on the
// trainer ID), Coronet's Feebas tiles, Gen 3/4 Unown, X/Y's Vivillon (its pattern follows the region).
#pragma once

#include "pkx/PKX.hpp"
#include "sav/Sav.hpp"
#include <string>
#include <vector>

namespace encounters
{
    struct Slot
    {
        u16 species = 0;
        u8 form = 0, min = 0, max = 0, type = 0;
        u16 location = 0;
        u8 time = 0, number = 0, tile = 0, rate = 0;
    };

    // Every wild encounter of the save's game (empty for games without a table).
    const std::vector<Slot>& of(const pksm::Sav& sav);
    // The other games (up to Ultra Sun and Ultra Moon) where a species is found in the wild, by name.
    std::vector<std::string> elsewhere(const pksm::Sav& sav, u16 species);
    // "Route 102", "Grass", "Lv. 4" or "Lv. 3-5", "morning/day" (Crystal), as shown.
    std::string place(const pksm::Sav& sav, const Slot& s);
    std::string kind(const pksm::Sav& sav, const Slot& s);
    // Whether PKForge can make a legal Pokémon from it (see above).
    bool catchable(const pksm::Sav& sav, const Slot& s);
    // Why not, shown to the user ("" when it is catchable).
    std::string notCatchable(const pksm::Sav& sav, const Slot& s);
    // Fills a fresh Pokémon of the save (trainer, language, version set) as caught there: species, form,
    // level, met data, PID/IVs/nature/gender/ability by the game's method, moves, friendship.
    void make(pksm::PKX& pk, const pksm::Sav& sav, const Slot& s);

    // Static and gift encounters (Generations 3-7, tools/statics.py -> romfs/enc/s<version>.bin): the
    // legendaries and Pokémon standing on the map, starters, fossils and gifts PKForge can make exactly as
    // PKHeX does (Method 1 in Gen 3 and 4, the Monochrome rules in Gen 5, perfect IVs, locks and relearn
    // moves in Gen 6 and 7).
    struct Static
    {
        u16 species = 0;
        u8 form = 0, level = 0;
        u16 location = 0;
        u8 ball = 4, ability = 0, neverShiny = 0, flawless = 0, gender = 255, nature = 255, tile = 0, gift = 0;
        u16 moves[4] = {}, relearn[4] = {};
    };
    const std::vector<Static>& statics(const pksm::Sav& sav);
    std::string place(const pksm::Sav& sav, const Static& s);
    void make(pksm::PKX& pk, const pksm::Sav& sav, const Static& s);

    // In-game trades (Generations 3-7, tools/trades.py -> romfs/enc/t<version>.txt): the NPC's Pokémon with
    // its fixed PID (Gen 3-4, Black/White), fixed IVs, contest stats, trainer and nickname in the save's
    // language, and the trade memories of Generations 6 and 7.
    struct Trade
    {
        int gen = 0;
        u16 species = 0;
        u8 form = 0, level = 0;
        u16 location = 0;
        bool fixedPid = false;
        u32 pid = 0;
        u16 tid = 0, sid = 0;
        u8 otGender = 0;
        int gender = -1, nature = -1; // -1: random
        u8 ability = 0;
        int ivs[6] = {-1, -1, -1, -1, -1, -1};
        u8 contest[6] = {};
        u16 moves[4] = {};
        u16 memory[4] = {};
        bool fixedNickname = true, evolve = false;
        std::string nicknames, trainers; // "lang=name|lang=name"
    };
    const std::vector<Trade>& trades(const pksm::Sav& sav);
    // The trainer and nickname in the save's language.
    std::string tradeTrainer(const pksm::Sav& sav, const Trade& t);
    void make(pksm::PKX& pk, const pksm::Sav& sav, const Trade& t);
}

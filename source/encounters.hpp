// Wild encounters, as the Android PKForge's encounter gallery (PKHeX's EncounterMovesetGenerator): where a
// species can be caught in the open save's game, and a Pokémon made from one of those encounters the way the
// game makes it, so PKHeX's legality check accepts it. Tables from PKHeX (tools/encounters.py ->
// romfs/enc/<version>.bin), the RNG methods ported from PKHeX's GenerateMethodH/J/K and MonochromeRNG:
//  - Generation 3: Method H (slot, level, nature, then a PID with that nature, then the IVs)
//  - Generation 4: Method J (Diamond, Pearl, Platinum) and Method K (HeartGold, SoulSilver), with the ground tile
//  - Generation 5: the Monochrome PID rules (ability bit 16, the wild xor), nature apart
//  - Generations 1, 2, 6, 7: random DVs/PID, as PKHeX does
// Encounter types that need an RNG proc PKForge does not model (Rock Smash, fishing in Gen 4, honey trees,
// Safari, Bug Contest, headbutt, hidden grottos, Friend Safari) are listed but not caught.
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
        u8 time = 0, number = 0, tile = 0;
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
    // Fills a fresh Pokémon of the save (trainer, language, version set) as caught there: species, form,
    // level, met data, PID/IVs/nature/gender/ability by the game's method, moves, friendship.
    void make(pksm::PKX& pk, const pksm::Sav& sav, const Slot& s);
}

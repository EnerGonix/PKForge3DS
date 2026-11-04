// Which moves a species can know in a game: romfs:/learnsets.bin, packed from PokeAPI by
// tools/learnsets.py. Every way of learning counts (level, TM, egg, tutor), in that game, in the games
// a Pokémon can come from, and as any of its pre-evolutions.
#pragma once

#include <3ds/types.h>
#include <vector>

namespace learnsets
{
    // False when the game has no learnset data (then nothing should be judged by it).
    bool covers(u8 version);
    bool canKnow(u16 species, u16 move, u8 version);
    // Level-up moves of the species in that game up to the level, the latest last.
    std::vector<u16> levelUpMoves(u16 species, u8 version, u8 level);
    // Egg cycles of the species (romfs:/hatch.bin from PokeAPI), 0 when unknown.
    u8 hatchCycles(u16 species);
}

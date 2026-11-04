// HaX mode, as in PKHeX and the Android PKForge: the editor offers every species, move, item and ball
// the generation can store instead of only what the game has, EVs ignore the 510 total, and Pokémon
// move into games that lack their species or moves. Off by default; kept in sdmc:/3ds/PKForge/hax.txt.
#pragma once

#include "enums/GameVersion.hpp"
#include <vector>

namespace hax
{
    bool on();
    void set(bool enabled);

    // Hardcore mode, as the original's: data editing is off (editor, HaX, new Pokémon and eggs, cloning, gifts,
    // legalizing, bag and trainer, world editors) for challenge runs and RetroAchievements players; moving
    // Pokémon between games and the Bank stays possible. Kept in sdmc:/3ds/PKForge/hardcore.txt.
    bool hardcore();
    void hardcore(bool enabled);

    // The choices the editor offers: the game's own lists, or with HaX every id up to its maximum.
    std::vector<int> species(pksm::GameVersion version);
    std::vector<int> moves(pksm::GameVersion version);
    std::vector<int> items(pksm::GameVersion version);
    std::vector<int> balls(pksm::GameVersion version);
}

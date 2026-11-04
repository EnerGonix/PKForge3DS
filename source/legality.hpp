// Legalize: puts right what PKSM-Core knows for a game (its moves, abilities, items and balls, the
// IV/EV/PP limits, levels). PKSM-Core has no learnsets or encounter tables, so this cannot judge
// whether a species learns a move or was caught where it says, as PKHeX's checker does.
#pragma once

#include "sav/Sav.hpp"
#include <string>
#include <vector>

namespace legality
{
    // Fixes pk in place for sav's game; returns what changed, one short line each.
    std::vector<std::string> fix(pksm::PKX& pk, const pksm::Sav& sav);
}

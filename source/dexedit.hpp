#pragma once

#include "sav/Sav.hpp"

// Per-species Pokédex editing (unseen / seen / caught), Generations 1 to 7.
namespace dexedit
{
    enum class State
    {
        Unseen,
        Seen,
        Caught
    };

    bool supported(const pksm::Sav& sav);
    State get(const pksm::Sav& sav, u16 species);
    void set(pksm::Sav& sav, u16 species, State state);
}

#pragma once

#include "sav/Sav.hpp"
#include <memory>

// Pokémon Colosseum and XD: Gale of Darkness saves (Dolphin's .gci, or the bare save), as the Android PKForge
// opens them through PKHeX (SAV3Colosseum, SAV3XD). To the rest of PKForge they are Generation 3 saves: their
// CK3/XK3 Pokémon read and write as PK3 (PKHeX's ConvertToPK3 / ConvertToCK3 / ConvertToXK3), Shadow data
// kept for a Pokémon that stays itself. The tables (species, items, moves) are Emerald's.
namespace gamecube
{
    // A Colosseum or XD save, or nullptr.
    std::unique_ptr<pksm::Sav> load(const std::shared_ptr<u8[]>& data, u32 length);
    // Whether an open save is one of them, and which.
    bool is(const pksm::Sav& sav);
    bool isXD(const pksm::Sav& sav);
    const char* name(const pksm::Sav& sav);
    // Colosseum's Strategy Memo as a Pokédex: 0 unseen, 1 seen, 2 owned.
    int dexState(const pksm::Sav& sav, u16 species);
    void dexState(pksm::Sav& sav, u16 species, int state);
}

#pragma once

#include "sav/Sav.hpp"
#include <memory>
#include <string>
#include <vector>

// ROM hacks the Android PKForge opens: Pokémon Radical Red and GS Chronicles (CFRU FireRed hacks) and Pokémon
// Unbound. Their Pokémon keep the Generation 3 layout with the hack's own species, move, item and ball numbers;
// PKForge shows and edits them with national numbers through the hack's tables (romfs/hacks, made by
// tools/romhacks.py from the original's data) and writes the hack's numbers back. As in the original, legality
// is not judged: these species and encounters do not exist in PKHeX.
namespace romhack
{
    enum class Hack
    {
        None,
        RadicalRed,
        Unbound,
        GSChronicles
    };

    // A hack save, or nullptr.
    std::unique_ptr<pksm::Sav> load(const std::shared_ptr<u8[]>& data, u32 length);
    bool is(const pksm::Sav& sav);
    bool is(const pksm::PKX& pk);
    Hack hackOf(const pksm::Sav& sav);
    const char* name(Hack hack);
    // The Bank format the Android PKForge gives a hack Pokémon ("PK3RR", "PK3UB", "PK3GSC") and its 58-byte record.
    std::string bankFormat(const pksm::PKX& pk);
    std::vector<u8> bankBytes(const pksm::PKX& pk);
    std::unique_ptr<pksm::PKX> fromBank(const std::string& format, const std::vector<u8>& bytes);
    // What the editor offers in a hack (national ids).
    std::vector<int> species(Hack hack);
    // Whether a hack save can hold this Pokémon (its species is in the hack's table).
    bool canHold(const pksm::Sav& sav, const pksm::PKX& pk);
    // The CFRU hacks' Pokédex (not Unbound's): 0 unseen, 1 seen, 2 caught.
    bool hasDex(const pksm::Sav& sav);
    int dexState(const pksm::Sav& sav, u16 species);
    void dexState(pksm::Sav& sav, u16 species, int state);
    std::vector<int> moves(Hack hack);
    std::vector<int> items(Hack hack);
}

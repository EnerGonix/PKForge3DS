// Pokémon Showdown sets, the text PKHeX, PKSM and Showdown all read: English names, one set per
// paragraph ("Nickname (Species) (F) @ Item", Ability, Level, Shiny, EVs, "<Nature> Nature", IVs, "- Move").
#pragma once

#include "sav/Sav.hpp"
#include <string>
#include <vector>

namespace showdown
{
    std::string write(const pksm::PKX& pk);
    // Splits a text into its sets (blank lines between them).
    std::vector<std::string> split(const std::string& text);
    // Fills pk (already a valid Pokémon of the save's format, its trainer set) from one set. Unknown
    // names and things the game lacks are skipped and listed in notes; false when the species is unknown.
    bool read(const std::string& set, pksm::PKX& pk, const pksm::Sav& sav, std::vector<std::string>& notes);
}

// The top screen's inspector: PKForge's second-screen summary of one Pokémon.
#pragma once

#include "pkx/PKX.hpp"
#include <string>

namespace inspector
{
    // where: "BOX 02 · SLOT 03", "PARTY 1", "BANK 01 · SLOT 05". version names forms.
    void draw(const pksm::PKX* pk, const std::string& where, u8 version);
    // The Bank page's card, as the original's: the gold name plate, the sprite, No. and genus, types, nature,
    // ability and the Pokédex entry.
    void drawCard(const pksm::PKX* pk, const std::string& where, u8 version);
    // A Bank slot this port cannot read (BDSP, Colosseum...): what the index knows.
    void drawStub(int species, int form, bool shiny, const std::string& nickname, int level,
        const std::string& format, const std::string& where);
    // The 0-5 stat order PKForge shows (HP Atk Def SpA SpD Spe) as PKSM-Core stats.
    // The summary page shown (Info, Stats, Moves, Origin, Legal), kept between Pokémon and screens.
    constexpr int PAGES = 5;
    void page(int p);
    int page();
    pksm::Stat statAt(int i);
    const char* statName(int i);
}

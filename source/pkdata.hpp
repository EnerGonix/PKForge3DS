// Names and small facts over PKSM-Core: species, moves, items, games, formats.
#pragma once

#include "enums/Generation.hpp"
#include "enums/Language.hpp"
#include "pkx/PKX.hpp"
#include <string>

namespace pkdata
{
    // The language Pokémon data is named in (species, moves, items...).
    extern pksm::Language lang;

    const std::string& species(int id);
    const std::string& move(int id);
    const std::string& item(int id);
    const std::string& ability(int id);
    const std::string& nature(int id);
    const std::string& ball(int id);
    const std::string& type(int id);
    std::string form(u8 version, int species, int form);
    std::string game(u8 version);
    std::string location(pksm::Generation gen, int id);
    std::string language(pksm::Language l);

    int genNumber(pksm::Generation g); // 1-9, Let's Go counts as 7
    // PKForge 3DS covers Generations 1 to 7, up to Ultra Sun and Ultra Moon: no Let's Go (versions 42/43)
    // and nothing from the Switch (Generation 8 and later).
    bool inScope(int gen, int version);
    constexpr int LAST_SPECIES = 807; // Zeraora, the last species Ultra Sun and Ultra Moon have
    std::string format(const pksm::PKX& pk); // PKHeX's type name: PK3, PB7, PA8...
    // PKForge's game art file for a version ("firered"); empty when none.
    const char* gameArt(u8 version);
    // Nature stat effects: which stat (0 atk..4 spd as in Stat order) goes up and down; -1 when neutral.
    void natureEffect(int nature, int& up, int& down);
    // Builds a PKX from stored bytes and a PKHeX format name (Bank, .pkN files); null when unreadable.
    std::unique_ptr<pksm::PKX> fromBytes(const std::string& format, const u8* data, size_t size);
    // The format a file extension names (".pk7" -> "PK7").
    std::string formatFromExtension(std::string ext);
}

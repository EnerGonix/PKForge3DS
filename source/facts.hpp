// PKForge's dex facts (from PokeAPI, BSD-3): move types and texts, ability texts, genus, Pokédex entries.
#pragma once

#include <string>

namespace facts
{
    struct Move
    {
        int type = -1, category = 0, power = 0, accuracy = 0;
        std::string text;
    };
    struct Entry
    {
        std::string genus, game, text;
    };

    void load();
    const Move* move(int id);
    const Entry* species(int id);
    const std::string& ability(int id);
}

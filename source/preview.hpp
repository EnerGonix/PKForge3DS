// The transfer preview, as the original's: before a Pokémon moves into a game of another format, what the move
// changes (format, moves, ability, item, ball, nickname...) and the legality verdict there; nothing is written
// until the player confirms.
#pragma once

#include "app.hpp"
#include "sav/Sav.hpp"
#include <functional>
#include <memory>
#include <string>

namespace preview
{
    // Whether a preview is worth showing: another format, or something PKForge's rules would change there.
    bool needed(const pksm::PKX& before, const pksm::PKX& after, const pksm::Sav& sav);
    std::unique_ptr<Screen> open(const pksm::PKX& before, const pksm::PKX& after, const pksm::Sav& sav,
        const std::string& game, std::function<void()> confirm);
}

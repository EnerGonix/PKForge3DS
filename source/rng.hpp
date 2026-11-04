// RNG / IVs, as the original's: a Pokémon's PID, encryption constant, IVs, nature and shininess, and
// "Reroll nature (keeps shiny)". Generations 3 and 4 take their nature from the PID, so a new PID is found
// that keeps the shininess, gender, form and ability; Generation 5 on writes the nature itself.
#pragma once

#include "app.hpp"
#include "pkx/PKX.hpp"
#include <functional>
#include <memory>
#include <string>

namespace rng
{
    // keep receives the changed Pokémon after a reroll (the slot is written by the caller).
    std::unique_ptr<Screen> open(std::unique_ptr<pksm::PKX> pk, std::string where,
        std::function<void(std::unique_ptr<pksm::PKX>)> keep);
}

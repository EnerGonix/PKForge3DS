// Mystery Gift: PKHeX's database of every real distribution (romfs:/mg, packed by
// tools/mysterygift.py), offered by the open save's generation and sent into its gift inbox,
// where the game hands it over (as PKSM does).
#pragma once

#include "sav/Sav.hpp"
#include "wcx/WCX.hpp"
#include <memory>
#include <string>
#include <vector>

namespace mysterygift
{
    struct Gift
    {
        std::string file; // romfs:/mg/<file>.bin
        int index = 0;
        std::string title, detail;
    };

    // The cards this save can take, newest database entries last; empty when its game has none.
    std::vector<Gift> forSave(const pksm::Sav& sav);
    std::unique_ptr<pksm::WCX> load(const Gift& gift);
}

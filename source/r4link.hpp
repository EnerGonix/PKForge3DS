// Starts R4 PokéLink (the DSi-mode companion that copies saves off the R4 card) from PKForge.
// TWiLight Menu++ is told to auto-run it once, then the console reboots into TWiLight.
#pragma once

#include <string>

namespace r4link
{
    // Puts TWiLight's settings back if a launch never reached R4 PokéLink (it restores them itself).
    void restorePending();
    // Installs R4 PokéLink out of sight if needed and asks before rebooting into it; falls back to TWiLight.
    void start();
    // The TWiLight Menu++ way: puts R4PokeLink.nds on the SD and has TWiLight auto-run it once.
    void startThroughTwilight(const std::string& why);
}

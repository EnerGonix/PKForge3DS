// The Living Dex Autopilot, after PKForge's LivingDexPlanner: fills one living dex in the Bank, in Pokédex
// order, with Pokémon the player already has. Moves only, never copies; a save's last copy of a species and
// party Pokémon and eggs stay where they are; a spare Kadabra, Machoke, Graveler, Haunter, Boldore, Gurdurr,
// Phantump or Pumpkaboo is evolved by trade for its evolution's slot. Nothing is touched until the player
// confirms the plan, and every save it writes keeps a restore point first.
#pragma once

namespace autopilot
{
    void open();
}

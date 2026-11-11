// World & events, after the Android PKForge's menu of the same name: the in-game world editors the
// open game has, working on the save's bytes at PKHeX's offsets (PKSM-Core does not model them).
//  - Ruby/Sapphire/Emerald clock repair (SAV_RTC3: small block 0x98 initial, 0xA0 elapsed)
//  - Gen 3 roamer (Roamer3, large block RS 0x3144, E 0x31DC, FRLG 0x30D0)
//  - HeartGold/SoulSilver Apricorn Box (general 0xE558) and Pokéwalker (general 0xE704)
//  - Diamond/Pearl/Platinum honey trees (general DP 0x72E4, Pt 0x7F38, 21 x 8 bytes)
// Every change marks the save edited; writing it keeps a restore point first, as always.
#pragma once

#include "saves.hpp"
#include <functional>
#include <memory>

namespace world
{
    bool hasAny(const pksm::Sav& sav);
    void open(std::shared_ptr<OpenSave> save);
    // Day Care / Nursery (Generations 3-7): what is deposited, whether an egg waits, and a withdraw into a box.
    bool hasDayCare(const pksm::Sav& sav);
    // changed runs after a withdraw, for the boxes on screen to show it.
    void dayCare(std::shared_ptr<OpenSave> save, std::function<void()> changed = {});
}

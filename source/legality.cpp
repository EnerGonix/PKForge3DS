#include "legality.hpp"
#include "romhack.hpp"
#include "learnsets.hpp"

#include <algorithm>

namespace legality
{
    std::vector<std::string> fix(pksm::PKX& pk, const pksm::Sav& sav)
    {
        std::vector<std::string> changes;
        if (romhack::is(pk) || romhack::is(sav))
        {
            return changes; // hack species and moves do not exist in the games' tables
        }
        if (pk.egg())
        {
            return changes; // eggs keep what the game gave them until they hatch
        }

        // Moves the game does not have go; the rest close up.
        const auto& moves = sav.availableMoves();
        bool movesChanged = false;
        for (u8 i = 0; i < 4; i++)
        {
            pksm::Move m = pk.move(i);
            if (m != pksm::Move::None && !moves.empty() && !moves.contains(m))
            {
                pk.move(i, pksm::Move::None);
                movesChanged = true;
            }
        }
        if (movesChanged)
        {
            pk.fixMoves();
            changes.push_back("removed moves this game does not have");
        }

        // Moves its species line cannot learn in this game or the ones it can come from (Smeargle
        // sketches anything). Empty slots then take its latest level-up moves.
        const u8 version = u8(sav.version());
        const u16 species = u16(pk.species());
        if (learnsets::covers(version) && species != 235)
        {
            bool unlearnable = false;
            for (u8 i = 0; i < 4; i++)
            {
                u16 m = u16(pk.move(i));
                if (m != 0 && !learnsets::canKnow(species, m, version))
                {
                    pk.move(i, pksm::Move::None);
                    unlearnable = true;
                }
            }
            if (unlearnable)
            {
                pk.fixMoves();
                changes.push_back("removed moves it cannot learn");
            }
            bool filled = false;
            std::vector<u16> ups = learnsets::levelUpMoves(species, version, pk.level());
            for (u8 i = 0; i < 4 && !ups.empty(); i++)
            {
                if (pk.move(i) != pksm::Move::None)
                {
                    continue;
                }
                if (!unlearnable && !movesChanged && i > 0)
                {
                    break; // a short moveset it already had is legal: only refill what was taken
                }
                while (!ups.empty())
                {
                    u16 m = ups.back();
                    ups.pop_back();
                    bool known = false;
                    for (u8 j = 0; j < 4; j++)
                    {
                        known |= u16(pk.move(j)) == m;
                    }
                    if (!known && moves.contains(pksm::Move(m)))
                    {
                        pk.move(i, pksm::Move(m));
                        pk.PPUp(i, 0);
                        pk.healPP(i);
                        filled = true;
                        break;
                    }
                }
            }
            if (filled)
            {
                changes.push_back("gave it level-up moves for its level");
            }
        }

        // PP Ups at most 3, PP no more than the move allows (used PP is fine), none on empty slots.
        bool ppChanged = false;
        for (u8 i = 0; i < 4; i++)
        {
            if (pk.PPUp(i) > 3)
            {
                pk.PPUp(i, 3);
                ppChanged = true;
            }
            bool empty = pk.move(i) == pksm::Move::None;
            if ((empty && pk.PP(i) != 0) || (!empty && pk.PP(i) > pk.maxPP(i)))
            {
                pk.PP(i, empty ? 0 : pk.maxPP(i));
                ppChanged = true;
            }
        }
        if (ppChanged)
        {
            changes.push_back("PP brought within what the moves allow");
        }

        // Ability: the one its species has in the slot it uses.
        const auto& abilities = sav.availableAbilities();
        if (!abilities.empty() && !abilities.contains(pk.ability()))
        {
            pk.setAbility(pk.abilityNumber());
            changes.push_back("ability set from its species");
        }

        const auto& items = sav.availableItems();
        if (pk.heldItem() != 0 && !items.empty() && !items.contains(pk.heldItem()))
        {
            pk.heldItem(u16(0));
            changes.push_back("removed a held item this game does not have");
        }

        const auto& balls = sav.availableBalls();
        if (!balls.empty() && !balls.contains(pk.ball()))
        {
            pk.ball(pksm::Ball::Poke);
            changes.push_back("ball set to a Poke Ball");
        }

        // IVs and EVs within the generation's limits.
        using pksm::Stat;
        const Stat stats[] = {Stat::HP, Stat::ATK, Stat::DEF, Stat::SPD, Stat::SPATK, Stat::SPDEF};
        bool ivs = false, evs = false;
        u32 total = 0;
        for (Stat s : stats)
        {
            if (pk.iv(s) > pk.maxIV())
            {
                pk.iv(s, pk.maxIV());
                ivs = true;
            }
            if (pk.ev(s) > pk.maxEV())
            {
                pk.ev(s, pk.maxEV());
                evs = true;
            }
            total += pk.ev(s);
        }
        while (total > pk.maxEVTotal())
        {
            // Trim the largest EV until the total fits.
            Stat big = *std::max_element(std::begin(stats), std::end(stats),
                [&](Stat a, Stat b) { return pk.ev(a) < pk.ev(b); });
            u32 cut = std::min<u32>(pk.ev(big), total - pk.maxEVTotal());
            pk.ev(big, u16(pk.ev(big) - cut));
            total -= cut;
            evs = true;
        }
        if (ivs)
        {
            changes.push_back("IVs brought to the limit");
        }
        if (evs)
        {
            changes.push_back("EVs brought within the limits");
        }

        if (pk.level() < 1 || pk.level() > 100)
        {
            pk.level(u8(std::clamp<int>(pk.level(), 1, 100)));
            changes.push_back("level brought to 1-100");
        }
        if (pk.metLevel() > pk.level())
        {
            pk.metLevel(pk.level());
            changes.push_back("met level no higher than its level");
        }

        if (!changes.empty())
        {
            pk.refreshChecksum();
        }
        return changes;
    }
}

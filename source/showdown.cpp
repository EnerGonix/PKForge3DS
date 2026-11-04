#include "showdown.hpp"

#include "enums/Ability.hpp"
#include "enums/Language.hpp"
#include "enums/Move.hpp"
#include "enums/Nature.hpp"
#include "enums/Species.hpp"
#include "enums/Stat.hpp"
#include "utils/i18n.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace showdown
{
    namespace
    {
        constexpr pksm::Language EN = pksm::Language::ENG;
        using pksm::Stat;
        // Showdown's order and labels.
        const Stat ORDER[6]       = {Stat::HP, Stat::ATK, Stat::DEF, Stat::SPATK, Stat::SPDEF, Stat::SPD};
        const char* const LABEL[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

        void englishReady()
        {
            static bool done = false;
            if (!done)
            {
                i18n::init(EN);
                done = true;
            }
        }

        std::string trim(std::string s)
        {
            while (!s.empty() && isspace(u8(s.back())))
            {
                s.pop_back();
            }
            size_t i = 0;
            while (i < s.size() && isspace(u8(s[i])))
            {
                i++;
            }
            return s.substr(i);
        }

        // Case- and punctuation-blind, so "Mr. Mime", "mr mime" and "Farfetch’d" find their entry.
        std::string key(const std::string& s)
        {
            std::string out;
            for (unsigned char c : s)
            {
                if (isalnum(c) || c >= 0x80)
                {
                    out.push_back(char(tolower(c)));
                }
            }
            return out;
        }

        int find(const std::vector<std::string>& names, const std::string& name)
        {
            std::string k = key(name);
            if (k.empty())
            {
                return -1;
            }
            for (size_t i = 1; i < names.size(); i++)
            {
                if (key(names[i]) == k)
                {
                    return int(i);
                }
            }
            return -1;
        }

        bool startsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

        // "252 HP / 4 Atk / 252 Spe" -> values by Showdown order; untouched where absent.
        void stats(const std::string& text, int out[6])
        {
            std::stringstream parts(text);
            std::string part;
            while (std::getline(parts, part, '/'))
            {
                part = trim(part);
                size_t space = part.find(' ');
                if (space == std::string::npos)
                {
                    continue;
                }
                int value         = atoi(part.c_str());
                std::string label = trim(part.substr(space + 1));
                for (int i = 0; i < 6; i++)
                {
                    if (key(label) == key(LABEL[i]))
                    {
                        out[i] = value;
                    }
                }
            }
        }
    }

    std::string write(const pksm::PKX& pk)
    {
        englishReady();
        std::string species = i18n::species(EN, pk.species());
        std::string out     = pk.nicknamed() && pk.nickname() != species ? pk.nickname() + " (" + species + ")" : species;
        if (pk.gender() == pksm::Gender::Male)
        {
            out += " (M)";
        }
        else if (pk.gender() == pksm::Gender::Female)
        {
            out += " (F)";
        }
        if (pk.heldItem() != 0)
        {
            out += " @ " + i18n::item(EN, pk.heldItem());
        }
        out += "\n";
        if (pk.generation() >= pksm::Generation::THREE)
        {
            out += "Ability: " + i18n::ability(EN, pk.ability()) + "\n";
        }
        if (pk.level() != 100)
        {
            out += "Level: " + std::to_string(pk.level()) + "\n";
        }
        if (pk.shiny())
        {
            out += "Shiny: Yes\n";
        }
        std::string evs, ivs;
        for (int i = 0; i < 6; i++)
        {
            int ev = pk.ev(ORDER[i]), iv = pk.iv(ORDER[i]);
            if (ev)
            {
                evs += (evs.empty() ? "" : " / ") + std::to_string(ev) + " " + LABEL[i];
            }
            if (iv != pk.maxIV())
            {
                ivs += (ivs.empty() ? "" : " / ") + std::to_string(iv) + " " + LABEL[i];
            }
        }
        if (!evs.empty())
        {
            out += "EVs: " + evs + "\n";
        }
        if (pk.generation() >= pksm::Generation::THREE)
        {
            out += i18n::nature(EN, pk.nature()) + " Nature\n";
        }
        if (!ivs.empty())
        {
            out += "IVs: " + ivs + "\n";
        }
        for (u8 i = 0; i < 4; i++)
        {
            if (pk.move(i) != pksm::Move::None)
            {
                out += "- " + i18n::move(EN, pk.move(i)) + "\n";
            }
        }
        return out;
    }

    std::vector<std::string> split(const std::string& text)
    {
        std::vector<std::string> sets;
        std::stringstream in(text);
        std::string line, current;
        while (std::getline(in, line))
        {
            if (trim(line).empty())
            {
                if (!trim(current).empty())
                {
                    sets.push_back(current);
                }
                current.clear();
            }
            else
            {
                current += line + "\n";
            }
        }
        if (!trim(current).empty())
        {
            sets.push_back(current);
        }
        return sets;
    }

    bool read(const std::string& set, pksm::PKX& pk, const pksm::Sav& sav, std::vector<std::string>& notes)
    {
        englishReady();
        std::stringstream in(set);
        std::string first;
        std::getline(in, first);
        first = trim(first);

        // "Nickname (Species) (M) @ Item"
        std::string item;
        size_t at = first.find(" @ ");
        if (at != std::string::npos)
        {
            item  = trim(first.substr(at + 3));
            first = trim(first.substr(0, at));
        }
        int gender = -1;
        for (auto [tag, g] : {std::pair{"(M)", 0}, std::pair{"(F)", 1}})
        {
            size_t p = first.rfind(tag);
            if (p != std::string::npos && p + 3 == first.size())
            {
                gender = g;
                first  = trim(first.substr(0, p));
            }
        }
        std::string nickname, speciesName = first;
        size_t open = first.rfind('('), close = first.rfind(')');
        if (open != std::string::npos && close == first.size() - 1 && open > 0)
        {
            nickname    = trim(first.substr(0, open));
            speciesName = trim(first.substr(open + 1, close - open - 1));
        }
        // "Rotom-Wash": the base species; forms by name are left to the editor.
        int species = find(i18n::rawSpecies(EN), speciesName);
        if (species < 0 && speciesName.find('-') != std::string::npos)
        {
            species = find(i18n::rawSpecies(EN), speciesName.substr(0, speciesName.find('-')));
        }
        if (species < 0)
        {
            notes.push_back("unknown species \"" + speciesName + "\"");
            return false;
        }
        if (!sav.availableSpecies().contains(pksm::Species{u16(species)}))
        {
            notes.push_back(speciesName + " is not in this game");
            return false;
        }
        pk.species(pksm::Species{u16(species)});
        pk.alternativeForm(0);
        pk.nickname(nickname.empty() ? i18n::species(sav.language(), pk.species()) : nickname);
        pk.nicknamed(!nickname.empty());
        pk.setAbility(0);

        if (!item.empty())
        {
            int id = find(i18n::rawItems(EN), item);
            if (id > 0 && sav.availableItems().contains(id))
            {
                pk.heldItem(u16(id));
            }
            else
            {
                notes.push_back("item " + item + " skipped");
            }
        }

        int evs[6], ivs[6];
        for (int i = 0; i < 6; i++)
        {
            evs[i] = 0;
            ivs[i] = pk.maxIV();
        }
        bool shiny = false;
        int level  = 100;
        int moveSlot = 0;
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (startsWith(line, "Ability:"))
            {
                int id = find(i18n::rawAbilities(EN), trim(line.substr(8)));
                pksm::Ability wanted{u16(id > 0 ? id : 0)};
                bool set = false;
                for (u8 n = 0; n < 3 && id > 0; n++)
                {
                    if (pk.abilities(n) == wanted)
                    {
                        pk.setAbility(pk.generation() >= pksm::Generation::SIX ? u8(1 << n) : n);
                        set = pk.ability() == wanted;
                        break;
                    }
                }
                if (!set)
                {
                    notes.push_back("ability " + trim(line.substr(8)) + " kept as its species allows");
                }
            }
            else if (startsWith(line, "Level:"))
            {
                level = std::clamp(atoi(line.c_str() + 6), 1, 100);
            }
            else if (startsWith(line, "Shiny:"))
            {
                shiny = key(line.substr(6)) == "yes";
            }
            else if (startsWith(line, "EVs:"))
            {
                stats(line.substr(4), evs);
            }
            else if (startsWith(line, "IVs:"))
            {
                stats(line.substr(4), ivs);
            }
            else if (line.size() > 7 && line.compare(line.size() - 7, 7, " Nature") == 0)
            {
                int id = find(i18n::rawNatures(EN), line.substr(0, line.size() - 7));
                if (id >= 0)
                {
                    pk.nature(pksm::Nature{u8(id)});
                }
            }
            else if (startsWith(line, "-") && moveSlot < 4)
            {
                std::string name = trim(line.substr(1));
                // "Hidden Power [Fire]" is Hidden Power.
                size_t bracket = name.find('[');
                if (bracket != std::string::npos)
                {
                    name = trim(name.substr(0, bracket));
                }
                int id = find(i18n::rawMoves(EN), name);
                if (id > 0 && sav.availableMoves().contains(pksm::Move{u16(id)}))
                {
                    pk.move(u8(moveSlot), pksm::Move{u16(id)});
                    pk.PPUp(u8(moveSlot), 0);
                    moveSlot++;
                }
                else
                {
                    notes.push_back("move " + name + " skipped");
                }
            }
            // Tera Type, Happiness, Dynamax Level and the like are not in these games' data.
        }
        for (u8 i = u8(moveSlot); i < 4; i++)
        {
            pk.move(i, pksm::Move::None);
        }
        u32 total = 0;
        for (int i = 0; i < 6; i++)
        {
            int ev = std::clamp(evs[i], 0, int(pk.maxEV()));
            if (total + ev > pk.maxEVTotal())
            {
                ev = int(pk.maxEVTotal() - total);
            }
            total += ev;
            pk.ev(ORDER[i], u16(ev));
            pk.iv(ORDER[i], u8(std::clamp(ivs[i], 0, int(pk.maxIV()))));
        }
        pk.level(u8(level));
        pk.metLevel(u8(level));
        pk.PID(pksm::PKX::getRandomPID(pk.species(), gender == 1 ? pksm::Gender::Female : pksm::Gender::Male, pk.version(),
            pk.nature(), 0, pk.abilityNumber(), shiny, pk.TSV(), pk.PID(), pk.generation()));
        // The PID asked for that gender; the species' ratio has the last word.
        pk.gender(pksm::PKX::genderFromRatio(pk.PID(), pk.genderType()));
        if (pk.shiny() != shiny)
        {
            pk.shiny(shiny);
        }
        pk.otFriendship(pk.baseFriendship());
        pk.healPP();
        pk.refreshChecksum();
        return true;
    }
}

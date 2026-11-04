// Trainer profiles, as the original's: a trainer (name, IDs, gender) kept to put on another save or on a
// Pokémon's original-trainer fields. Kept in sdmc:/3ds/PKForge/profiles.txt, one per line.
#pragma once
#include "app.hpp"
#include <functional>
#include <string>
#include <vector>

namespace profiles
{
    struct Profile
    {
        std::string name;
        u16 tid = 0, sid = 0;
        bool female = false;
    };

    std::vector<Profile> all();
    void add(const Profile& p);
    void remove(int index);
    // A picker of the profiles; chosen gets the one picked.
    void pick(const std::string& title, std::function<void(const Profile&)> chosen);
}

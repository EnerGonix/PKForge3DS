// Locked boxes, as the original's box manager: nothing goes in or out of a locked box (grabbing, dropping,
// sorting, cloning, releasing, the Dex Autopilot) until it is unlocked. Kept in sdmc:/3ds/PKForge/locks.txt,
// one "<save path or bank>\t<box>" per line.
#pragma once
#include <string>

namespace locks
{
    constexpr const char* BANK = "bank";
    bool locked(const std::string& owner, int box);
    void set(const std::string& owner, int box, bool locked);
}

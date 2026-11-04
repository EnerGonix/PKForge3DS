#include "hax.hpp"
#include "files.hpp"

#include "utils/VersionTables.hpp"

namespace hax
{
    namespace
    {
        constexpr const char* FILE_PATH = "sdmc:/3ds/PKForge/hax.txt";
        int state = -1; // read from the SD card the first time

        std::vector<int> upTo(int max)
        {
            std::vector<int> out;
            for (int id = 1; id <= max; id++)
            {
                out.push_back(id);
            }
            return out;
        }

        template <typename Set>
        std::vector<int> ids(const Set& set)
        {
            std::vector<int> out;
            for (const auto& v : set)
            {
                out.push_back(int(v));
            }
            return out;
        }
    }

    bool on()
    {
        if (state < 0)
        {
            std::string text;
            state = files::readText(FILE_PATH, text) && !text.empty() && text[0] == '1';
        }
        return state == 1;
    }

    void set(bool enabled)
    {
        state = enabled;
        files::writeText(FILE_PATH, enabled ? "1" : "0");
    }

    namespace
    {
        int hardcoreState = -1;
    }

    bool hardcore()
    {
        if (hardcoreState < 0)
        {
            std::string text;
            hardcoreState = files::readText("sdmc:/3ds/PKForge/hardcore.txt", text) && !text.empty() && text[0] == '1';
        }
        return hardcoreState == 1;
    }

    void hardcore(bool enabled)
    {
        hardcoreState = enabled;
        files::writeText("sdmc:/3ds/PKForge/hardcore.txt", enabled ? "1" : "0");
        if (enabled)
        {
            set(false); // HaX and Hardcore exclude each other
        }
    }

    std::vector<int> species(pksm::GameVersion v)
    {
        return on() ? upTo(u16(pksm::VersionTables::maxSpecies(v))) : ids(pksm::VersionTables::availableSpecies(v));
    }

    std::vector<int> moves(pksm::GameVersion v)
    {
        return on() ? upTo(u16(pksm::VersionTables::maxMove(v))) : ids(pksm::VersionTables::availableMoves(v));
    }

    std::vector<int> items(pksm::GameVersion v)
    {
        return on() ? upTo(pksm::VersionTables::maxItem(v)) : ids(pksm::VersionTables::availableItems(v));
    }

    std::vector<int> balls(pksm::GameVersion v)
    {
        return on() ? upTo(u8(pksm::VersionTables::maxBall(v))) : ids(pksm::VersionTables::availableBalls(v));
    }
}

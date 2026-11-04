#include "locks.hpp"
#include "files.hpp"

#include <set>
#include <vector>

namespace locks
{
    namespace
    {
        constexpr const char* FILE_PATH = "sdmc:/3ds/PKForge/locks.txt";
        std::set<std::string> lines;
        bool loaded = false;

        std::string keyOf(const std::string& owner, int box) { return owner + "\t" + std::to_string(box); }

        void load()
        {
            if (loaded)
            {
                return;
            }
            loaded = true;
            std::string text;
            if (!files::readText(FILE_PATH, text))
            {
                return;
            }
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                at = end == std::string::npos ? text.size() : end + 1;
                if (line.find('\t') != std::string::npos)
                {
                    lines.insert(line);
                }
            }
        }
    }

    bool locked(const std::string& owner, int box)
    {
        load();
        return lines.count(keyOf(owner, box)) != 0;
    }

    void set(const std::string& owner, int box, bool lock)
    {
        load();
        if (lock)
        {
            lines.insert(keyOf(owner, box));
        }
        else
        {
            lines.erase(keyOf(owner, box));
        }
        std::string text;
        for (const std::string& l : lines)
        {
            text += l + "\n";
        }
        files::writeText(FILE_PATH, text);
    }
}

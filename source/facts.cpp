#include "facts.hpp"
#include "files.hpp"

#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace facts
{
    namespace
    {
        std::unordered_map<int, Move> moves;
        std::unordered_map<int, Entry> entries;
        std::unordered_map<int, std::string> abilities;
        const std::string none;

        std::vector<std::string> split(const std::string& line)
        {
            std::vector<std::string> out;
            std::string part;
            std::stringstream ss(line);
            while (std::getline(ss, part, '\t'))
            {
                out.push_back(part);
            }
            return out;
        }

        void eachLine(const char* path, void (*fn)(const std::vector<std::string>&))
        {
            std::string text;
            if (!files::readText(path, text))
            {
                return;
            }
            std::stringstream ss(text);
            std::string line;
            while (std::getline(ss, line))
            {
                if (!line.empty())
                {
                    fn(split(line));
                }
            }
        }
    }

    void load()
    {
        eachLine("romfs:/data/moves.tsv", [](const std::vector<std::string>& f)
            {
                if (f.size() >= 5)
                {
                    Move m{atoi(f[1].c_str()), atoi(f[2].c_str()), atoi(f[3].c_str()), atoi(f[4].c_str()),
                        f.size() > 5 ? f[5] : ""};
                    moves[atoi(f[0].c_str())] = m;
                }
            });
        eachLine("romfs:/data/abilities.tsv", [](const std::vector<std::string>& f)
            {
                if (f.size() >= 2)
                {
                    abilities[atoi(f[0].c_str())] = f[1];
                }
            });
        eachLine("romfs:/data/species.tsv", [](const std::vector<std::string>& f)
            {
                if (f.size() >= 2)
                {
                    entries[atoi(f[0].c_str())] = {f[1], f.size() > 2 ? f[2] : "", f.size() > 3 ? f[3] : ""};
                }
            });
    }

    const Move* move(int id)
    {
        auto found = moves.find(id);
        return found == moves.end() ? nullptr : &found->second;
    }

    const Entry* species(int id)
    {
        auto found = entries.find(id);
        return found == entries.end() ? nullptr : &found->second;
    }

    const std::string& ability(int id)
    {
        auto found = abilities.find(id);
        return found == abilities.end() ? none : found->second;
    }
}

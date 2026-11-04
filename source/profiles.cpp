#include "profiles.hpp"
#include "files.hpp"
#include "screens.hpp"

#include <cstdio>
#include <cstdlib>

namespace profiles
{
    namespace
    {
        constexpr const char* FILE_PATH = "sdmc:/3ds/PKForge/profiles.txt";

        void write(const std::vector<Profile>& list)
        {
            std::string text;
            for (const Profile& p : list)
            {
                text += p.name + "\t" + std::to_string(p.tid) + "\t" + std::to_string(p.sid) + "\t" + (p.female ? "F" : "M") + "\n";
            }
            files::writeText(FILE_PATH, text);
        }
    }

    std::vector<Profile> all()
    {
        std::vector<Profile> out;
        std::string text;
        if (!files::readText(FILE_PATH, text))
        {
            return out;
        }
        size_t at = 0;
        while (at < text.size())
        {
            size_t end = text.find('\n', at);
            std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
            at = end == std::string::npos ? text.size() : end + 1;
            size_t a = line.find('\t');
            if (a == std::string::npos)
            {
                continue;
            }
            Profile p;
            p.name = line.substr(0, a);
            const char* rest = line.c_str() + a + 1;
            char* next       = nullptr;
            p.tid  = u16(strtoul(rest, &next, 10));
            p.sid  = u16(strtoul(next && *next ? next + 1 : rest, &next, 10));
            p.female = next && *next && next[1] == 'F';
            out.push_back(p);
        }
        return out;
    }

    void add(const Profile& p)
    {
        auto list = all();
        for (const Profile& q : list)
        {
            if (q.name == p.name && q.tid == p.tid && q.sid == p.sid)
            {
                return;
            }
        }
        list.push_back(p);
        write(list);
    }

    void remove(int index)
    {
        auto list = all();
        if (index >= 0 && index < int(list.size()))
        {
            list.erase(list.begin() + index);
            write(list);
        }
    }

    void pick(const std::string& title, std::function<void(const Profile&)> chosen)
    {
        auto list = all();
        if (list.empty())
        {
            app::message(title, "No profiles yet: on a save's trainer card, START > Save as profile.");
            return;
        }
        std::vector<screens::PickItem> items;
        for (int i = 0; i < int(list.size()); i++)
        {
            char ids[32];
            snprintf(ids, sizeof(ids), "ID %05u  SID %05u  %s", list[i].tid, list[i].sid, list[i].female ? "F" : "M");
            items.push_back({i, list[i].name, ids});
        }
        app::push(screens::picker(title, std::move(items), -1, [list, chosen](int i) { chosen(list[i]); }));
    }
}

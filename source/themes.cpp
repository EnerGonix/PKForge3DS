#include "gfx.hpp"
#include "files.hpp"

#include <cstdlib>

namespace gfx
{
    namespace
    {
#include "themes.inc"
        constexpr const char* FILE_PATH = "sdmc:/3ds/PKForge/theme.txt";
        int current = 0;

        void apply(int i)
        {
            const u32* c = THEMES[i].colours;
            int k        = 0;
#define SET(name) T::name = c[k++];
            THEME_FIELDS(SET)
#undef SET
        }
    }

    int themeCount() { return int(sizeof(THEMES) / sizeof(THEMES[0])); }

    const char* themeName(int i) { return THEMES[i].name; }

    int theme() { return current; }

    void theme(int i)
    {
        if (i < 0 || i >= themeCount())
        {
            return;
        }
        current = i;
        apply(i);
        files::writeText(FILE_PATH, std::to_string(i));
    }

    void loadTheme()
    {
        std::string text;
        if (files::readText(FILE_PATH, text))
        {
            int i = atoi(text.c_str());
            if (i > 0 && i < themeCount())
            {
                current = i;
                apply(i);
            }
        }
    }
}

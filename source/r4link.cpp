// R4 PokéLink launch. PKForge carries it as a DSiWare CIA for a DSi system title, which the HOME
// menu does not show: installed quietly (again when PKForge brings a different one), then chainloaded.
// When that install fails it goes through TWiLight Menu++ instead, which runs its "last played" ROM
// at boot when AUTORUNGAME is set (title/arm9/source/main.cpp, lastRunROM): PKForge points that at
// R4PokeLink.nds, keeps the user's settings.ini aside, and R4 PokéLink copies the original back.
#include "r4link.hpp"
#include "app.hpp"
#include "files.hpp"
#include "input.hpp"

#include <3ds.h>
#include <cstdio>
#include <utility>
#include <vector>

namespace r4link
{
    namespace
    {
        constexpr const char* SETTINGS = "sdmc:/_nds/TWiLightMenu/settings.ini";
        constexpr const char* BACKUP   = "sdmc:/3ds/PKForge/twilight-settings.bak";
        constexpr const char* NDS      = "sdmc:/3ds/PKForge/R4PokeLink.nds";
        constexpr const char* BUNDLED  = "romfs:/r4/R4PokeLink.nds";
        // The same path as TWiLight sees it in DSi mode.
        constexpr const char* NDS_TWL = "sd:/3ds/PKForge/R4PokeLink.nds";
        // TWiLight's booter is the DSiWare title 00030004-53524C41 ("SRLA"); the 3DS installs it as 00048004-.
        constexpr u32 TWILIGHT_LOW = 0x53524C41;
        // R4 PokéLink's own title (DSi 00030005-5234504C "R4PL", see r4pokelink/Makefile).
        constexpr u64 R4_TITLE            = 0x000480055234504CULL;
        constexpr const char* BUNDLED_CIA = "romfs:/r4/R4PokeLink.cia";
        constexpr const char* INSTALLED   = "sdmc:/3ds/PKForge/r4pokelink.installed";
        // Tells R4 PokéLink it was started by PKForge (it then points back to PKForge when done).
        constexpr const char* FROM_PKFORGE = "sdmc:/3ds/PKForge/r4pokelink.from-pkforge";

        std::string fingerprint(const std::vector<u8>& bytes)
        {
            u64 h = 0xCBF29CE484222325ULL;
            for (u8 b : bytes)
            {
                h = (h ^ b) * 0x100000001B3ULL;
            }
            char out[40];
            snprintf(out, sizeof(out), "%016llX %zu", (unsigned long long)h, bytes.size());
            return out;
        }

        // Installs the bundled R4 PokéLink unless this very one is already there.
        bool installHidden(std::string& why)
        {
            std::vector<u8> cia;
            if (!files::read(BUNDLED_CIA, cia))
            {
                why = "R4PokeLink.cia is missing from PKForge.";
                return false;
            }
            if (R_FAILED(amInit()))
            {
                why = "The title service did not start.";
                return false;
            }
            std::string mark = fingerprint(cia), had;
            u64 id           = R4_TITLE;
            AM_TitleEntry entry;
            bool present = R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_NAND, 1, &id, &entry));
            if (present && files::readText(INSTALLED, had) && had == mark)
            {
                amExit();
                return true;
            }
            Handle cia_h = 0;
            u32 written  = 0;
            Result res   = AM_StartCiaInstall(MEDIATYPE_NAND, &cia_h);
            if (R_SUCCEEDED(res))
            {
                res = FSFILE_Write(cia_h, &written, 0, cia.data(), cia.size(), FS_WRITE_FLUSH);
                if (R_SUCCEEDED(res) && written == cia.size())
                {
                    res = AM_FinishCiaInstall(cia_h);
                }
                else
                {
                    AM_CancelCIAInstall(cia_h);
                    res = R_FAILED(res) ? res : -1;
                }
            }
            amExit();
            if (R_FAILED(res))
            {
                char text[80];
                snprintf(text, sizeof(text), "Installing R4 PokeLink failed (%08lX).", (unsigned long)res);
                why = text;
                return false;
            }
            files::writeText(INSTALLED, mark);
            return true;
        }

        void launchDirect()
        {
            files::writeText(FROM_PKFORGE, "1");
            if (autotest::active())
            {
                app::toast("R4: R4 PokeLink installed (no reboot in tests)");
                return;
            }
            aptSetChainloader(R4_TITLE, MEDIATYPE_NAND);
            app::quit();
        }

        u64 findTwilight()
        {
            if (R_FAILED(amInit()))
            {
                return 0;
            }
            u64 found = 0;
            u32 count = 0;
            if (R_SUCCEEDED(AM_GetTitleCount(MEDIATYPE_NAND, &count)) && count > 0)
            {
                std::vector<u64> ids(count);
                u32 read = 0;
                if (R_SUCCEEDED(AM_GetTitleList(&read, MEDIATYPE_NAND, count, ids.data())))
                {
                    for (u32 i = 0; i < read && !found; i++)
                    {
                        if (u32(ids[i]) == TWILIGHT_LOW && (ids[i] >> 32 & 0xFFFFFF00) == 0x00048000)
                        {
                            found = ids[i];
                        }
                    }
                }
            }
            amExit();
            return found;
        }

        // Sets keys in the [SRLOADER] section, adding the ones that are missing; other lines stay as they are.
        std::string setKeys(const std::string& ini, const std::vector<std::pair<std::string, std::string>>& keys)
        {
            std::vector<bool> done(keys.size());
            std::string out;
            bool inSection = false, sawSection = false;
            auto addMissing = [&]
            {
                for (size_t k = 0; k < keys.size(); k++)
                {
                    if (!done[k])
                    {
                        out += keys[k].first + " = " + keys[k].second + "\r\n";
                        done[k] = true;
                    }
                }
            };
            size_t pos = 0;
            while (pos < ini.size())
            {
                size_t end       = ini.find('\n', pos);
                std::string line = ini.substr(pos, end == std::string::npos ? std::string::npos : end - pos + 1);
                pos              = end == std::string::npos ? ini.size() : end + 1;
                size_t first     = line.find_first_not_of(" \t");
                if (first != std::string::npos && line[first] == '[')
                {
                    if (inSection)
                    {
                        addMissing();
                    }
                    inSection = line.compare(first, 10, "[SRLOADER]") == 0;
                    sawSection |= inSection;
                }
                else if (inSection && first != std::string::npos)
                {
                    size_t eq       = line.find('=');
                    std::string key = eq == std::string::npos ? "" : line.substr(first, eq - first);
                    key.erase(key.find_last_not_of(" \t") + 1);
                    bool replaced = false;
                    for (size_t k = 0; k < keys.size(); k++)
                    {
                        if (key == keys[k].first)
                        {
                            out += keys[k].first + " = " + keys[k].second + "\r\n";
                            done[k]  = true;
                            replaced = true;
                        }
                    }
                    if (replaced)
                    {
                        continue;
                    }
                }
                out += line;
            }
            if (!out.empty() && out.back() != '\n')
            {
                out += "\r\n";
            }
            if (!sawSection)
            {
                out += "[SRLOADER]\r\n";
            }
            addMissing();
            return out;
        }

        bool sameFile(const std::string& a, const std::string& b)
        {
            std::vector<u8> x, y;
            return files::read(a, x) && files::read(b, y) && x == y;
        }

        void launch(u64 twilight)
        {
            std::string ini;
            if (!files::readText(SETTINGS, ini))
            {
                app::message("R4", "Could not read TWiLight's settings.ini.");
                return;
            }
            // An older backup is the user's real settings: a launch that never got that far left it there.
            if (!files::exists(BACKUP) && !files::copy(SETTINGS, BACKUP))
            {
                app::message("R4", "Could not keep a copy of TWiLight's settings. Nothing was changed.");
                return;
            }
            std::string changed = setKeys(ini, {
                                                   {"ROM_PATH", NDS_TWL},
                                                   {"LAUNCH_TYPE", "2"}, // ESDFlashcardDirectLaunch
                                                   {"PREVIOUS_USED_DEVICE", "0"}, // the SD card
                                                   {"AUTORUNGAME", "1"},
                                                   // R4 PokéLink power-cycles Slot-1 and talks to the card
                                                   // itself: TWiLight must leave SCFG unlocked.
                                                   {"SLOT1_SCFG_UNLOCK", "1"},
                                                   {"SLOT1_ENABLESD", "1"},
                                                   // Straight into R4 PokéLink: the DSi splash plays before
                                                   // lastRunROM, the logos would only add time.
                                                   {"DSI_SPLASH", "0"},
                                                   {"ROCKET_ROBZ_LOGO", "0"},
                                                   {"SHOWLOGO", "0"},
                                               });
            if (!files::writeText(SETTINGS, changed))
            {
                restorePending();
                app::message("R4", "Could not write TWiLight's settings. Nothing was changed.");
                return;
            }
            if (autotest::active())
            {
                app::toast("R4: TWiLight set up (no reboot in tests)");
                return;
            }
            aptSetChainloader(twilight, MEDIATYPE_NAND);
            app::quit();
        }
    }

    void restorePending()
    {
        if (files::exists(BACKUP) && files::copy(BACKUP, SETTINGS))
        {
            remove(BACKUP);
        }
    }

    void start()
    {
        std::string why;
        if (installHidden(why))
        {
            app::confirm(
                "The 3DS restarts into R4 PokeLink (DSi mode) to copy saves between the R4 and the SD card. When you "
                "are done, press HOME and open PKForge again: the R4 saves are in the list.",
                [] { launchDirect(); }, "Restart", "Cancel");
            return;
        }
        startThroughTwilight(why);
    }

    void startThroughTwilight(const std::string& why)
    {
        u64 twilight = findTwilight();
        if (!twilight && autotest::active())
        {
            // Azahar has no TWiLight: tests carry on with its usual ID and stop before the reboot.
            twilight = 0x0004800453524C41;
        }
        if (!twilight)
        {
            app::message("R4",
                why + " The other way in is TWiLight Menu++, which is not installed: install it from "
                      "Universal-Updater, open it once, then try again.");
            return;
        }
        if (!files::exists(SETTINGS))
        {
            app::message("R4", "Open TWiLight Menu++ once so it writes its settings, then try again.");
            return;
        }
        if (!sameFile(BUNDLED, NDS) && !files::copy(BUNDLED, NDS))
        {
            app::message("R4", "Could not put R4PokeLink.nds on the SD card.");
            return;
        }
        app::confirm(
            "The 3DS restarts into R4 PokeLink (DSi mode) to copy saves between the R4 and the SD card. When you are "
            "done, press HOME and open PKForge again: the R4 saves are in the list.",
            [twilight] { launch(twilight); }, "Restart", "Cancel");
    }
}

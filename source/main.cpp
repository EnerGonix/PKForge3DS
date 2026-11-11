// PKForge for Nintendo 3DS: entry point.
#include "app.hpp"
#include "spritepack.hpp"
#include "audio.hpp"
#include "bank.hpp"
#include "facts.hpp"
#include "files.hpp"
#include "gfx.hpp"
#include "pkdata.hpp"
#include "r4link.hpp"
#include "saves.hpp"
#include "screens.hpp"
#include "guide.hpp"
#include "updater.hpp"

#include "utils/i18n.hpp"
#include <3ds.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <optional>

namespace
{
    // What start-up loads while the title screen shows: the text tables, the Pokédex facts and the Bank.
    struct Loader
    {
        Thread thread = nullptr;
        int appPriority = 0x30;
        bool bankOk   = true;
        std::string bankError;
    } loader;

    void loadWork(void*)
    {
        {
            app::Timed t("audio::init");
            audio::init(loader.appPriority);
        }
        {
            app::Timed t("sprite index");
            gfx::loadSpriteIndex();
        }
        {
            app::Timed t("i18n::init");
            i18n::init(pkdata::lang);
        }
        {
            app::Timed t("facts::load");
            facts::load();
        }
        app::Timed t("bank load");
        loader.bankOk = app::bank().load(loader.bankError);
    }

    void loadFinish()
    {
        if (loader.thread)
        {
            threadJoin(loader.thread, U64_MAX);
            threadFree(loader.thread);
            loader.thread = nullptr;
        }
        if (!loader.bankOk)
        {
            app::message("Bank", loader.bankError);
            loader.bankOk = true;
        }
    }

    // Shown with the system console when the RomFS is missing (a .3dsx launched without its data).
    void fatal(const char* what)
    {
        gfxInitDefault();
        consoleInit(GFX_TOP, nullptr);
        printf("PKForge could not start:\n\n%s\n\nPress START to leave.", what);
        while (aptMainLoop())
        {
            hidScanInput();
            if (hidKeysDown() & KEY_START)
            {
                break;
            }
            gspWaitForVBlank();
        }
        gfxExit();
    }
}

int main()
{
    if (R_FAILED(romfsInit()))
    {
        fatal("Its RomFS could not be opened.");
        return 0;
    }
    app::perfBegin();
    std::optional<app::Timed> total;
    total.emplace("start-up (to the first frame)");
    ptmuInit();
    osSetSpeedupEnable(true); // New 3DS: 804 MHz and the L2 cache (no effect on an Old 3DS)
    gfxInitDefault();
    srand(u32(time(nullptr)) ^ u32(svcGetSystemTick()));
    bool gfxOk;
    {
        app::Timed t("gfx::init");
        gfxOk = gfx::init();
    }
    if (gfxOk)
    {
        gfx::loadTheme();
    }
    if (!gfxOk)
    {
        gfxExit();
        fatal("Its font or images are missing from the RomFS.");
        romfsExit();
        return 0;
    }

    for (const char* sub : {"/saves", "/import", "/export", "/backups", "/bank"})
    {
        files::mkdirs(std::string(files::ROOT) + sub);
    }
    autotest::load();
    {
        app::Timed t("r4link::restorePending");
        r4link::restorePending();
    }
    // A lower priority than the app's: it runs while the title waits for the screen.
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    loader.appPriority = prio;
    loader.thread = threadCreate(loadWork, nullptr, 64 * 1024, prio + 1, -2, false);
    if (!loader.thread)
    {
        loadWork(nullptr);
    }
    if (autotest::active())
    {
        loadFinish();
        app::push(screens::home());
    }
    else
    {
        app::push(screens::home());
        if (!guide::seen())
        {
            app::push(guide::open()); // the first start: the quick guide comes after the title screen
        }
        app::push(screens::title([]() { loadFinish(); }));
        updater::checkInBackground(); // once a day, quietly
    }

    total.reset();
    app::run();
    saves::stopScan();

    spritepack::stop();
    audio::exit();
    gfx::exit();
    i18n::exit();
    ptmuExit();
    gfxExit();
    romfsExit();
    return 0;
}

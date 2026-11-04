/*
    R4 PokéLink: moves Pokémon saves between an R4 (Ace3DS family) flashcard in Slot-1 and
    the 3DS SD card, where PKForge for 3DS edits them. Runs in DSi mode, started from TWiLight
    Menu++ on the 3DS SD card.

    The R4 is only ever written save by save: an existing save of the same size is rewritten in
    place, after its sectors were checked against the file and a backup was kept on the 3DS SD.

    This program is free software: you can redistribute it and/or modify it under the terms
    of the GNU General Public License as published by the Free Software Foundation, either
    version 3 of the License, or (at your option) any later version.
*/

#include <nds.h>
#include <fat.h>
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ace3ds.h"
#include "ace3ds_sdinit.h"
#include "card_init.h"
#include "fat32.h"
#include "ui.h"

#define VERSION       "0.7"
#define DEST_ROOT     "sd:/3ds/PKForge/saves/R4"
#define BACKUP_ROOT   "sd:/3ds/PKForge/r4backups"
#define DUMP_ROOT     "sd:/3ds/PKForge/r4dump"
#define LOG_PATH      "sd:/3ds/PKForge/r4pokelink.log"
// PKForge keeps TWiLight's settings here while TWiLight is set to auto-run this app.
#define TWL_SETTINGS  "sd:/_nds/TWiLightMenu/settings.ini"
#define TWL_BACKUP    "sd:/3ds/PKForge/twilight-settings.bak"
// Left by PKForge when it starts this app directly (installed as a hidden DSi title).
#define FROM_PKFORGE  "sd:/3ds/PKForge/r4pokelink.from-pkforge"
#define MAX_SAVES     64
#define MAX_SAVE_SIZE (1024 * 1024)
#define MAX_SECTORS   (MAX_SAVE_SIZE / 512)

enum
{
    REL_ONLY_R4,
    REL_SAME,
    REL_DIFFERENT
};

typedef struct
{
    char name[200];   // Pokemon - White Version.sav
    char sys[8];      // nds, gba, gb, gbc
    char r4Path[260]; // r4:/roms/nds/...
    char r4Rel[240];  // roms/nds/... for the raw FAT32 lookup
    char sdPath[260]; // sd:/3ds/PKForge/saves/R4/nds/...
    u32 r4Size, sdSize;
    bool r4Blank, onSd;
    int relation;
    bool pick[2];
} Item;

static Item items[MAX_SAVES];
static int itemCount;
static FILE* logFile;
static Fat32 fat;
static bool fatOk;
static int tab, focus, firstRow;
static char status[160] = "";
static u32 sectorList[MAX_SECTORS];
static u32 rawSector[128];
static bool fromPKForge;

// ---------------------------------------------------------------- logging and small helpers

static void say(const char* format, ...)
{
    char line[256];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (logFile)
    {
        fprintf(logFile, "%s\n", line);
        fflush(logFile);
        // The FAT entry only learns the new size on a sync: a log cut off by HOME would read as empty.
        fsync(fileno(logFile));
    }
}

static void setStatus(const char* text) { snprintf(status, sizeof(status), "%s", text); }

static bool endsWith(const char* s, const char* suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    if (a < b)
    {
        return false;
    }
    for (size_t i = 0; i < b; i++)
    {
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suffix[i]))
        {
            return false;
        }
    }
    return true;
}

static bool hasPokemon(const char* name)
{
    for (const char* p = name; *p; p++)
    {
        if (tolower((unsigned char)p[0]) == 'p' && tolower((unsigned char)p[1]) == 'o' &&
            tolower((unsigned char)p[2]) == 'k')
        {
            return true;
        }
    }
    return false;
}

static u8* readWhole(const char* path, u32* size)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    u8* data = NULL;
    if (n > 0 && n <= MAX_SAVE_SIZE)
    {
        data = malloc(n);
        if (data && fread(data, 1, n, f) != (size_t)n)
        {
            free(data);
            data = NULL;
        }
    }
    fclose(f);
    *size = data ? (u32)n : 0;
    return data;
}

static bool writeWhole(const char* path, const u8* data, u32 size)
{
    FILE* f = fopen(path, "wb");
    if (!f)
    {
        return false;
    }
    bool ok = fwrite(data, 1, size, f) == size;
    ok      = (fclose(f) == 0) && ok;
    return ok;
}

// Started by PKForge: TWiLight auto-runs this app until its own settings are back.
static void restoreTwilight(void)
{
    u32 size;
    u8* data = readWhole(TWL_BACKUP, &size);
    if (!data)
    {
        return;
    }
    fromPKForge = writeWhole(TWL_SETTINGS, data, size);
    free(data);
    if (fromPKForge)
    {
        remove(TWL_BACKUP);
    }
}

static bool blank(const u8* data, u32 size)
{
    for (u32 i = 1; i < size; i++)
    {
        if (data[i] != data[0])
        {
            return false;
        }
    }
    return data[0] == 0xFF || data[0] == 0x00;
}

static void stamp(char* out, size_t size)
{
    time_t now = time(NULL);
    struct tm* t = gmtime(&now);
    strftime(out, size, "%Y%m%d-%H%M%S", t);
}

// ---------------------------------------------------------------- drawing

static const char* tabName(int t) { return t == 0 ? "R4  >  3DS" : "3DS  >  R4"; }

// Rows of the current tab: R4 > 3DS lists saves with data on the R4; 3DS > R4 lists 3DS copies
// that can go back over a save of the same size.
static bool inTab(const Item* it, int t)
{
    if (t == 0)
    {
        return !it->r4Blank;
    }
    return it->onSd && it->sdSize == it->r4Size;
}

static bool selectable(const Item* it, int t)
{
    if (t == 0)
    {
        return it->relation != REL_SAME;
    }
    return it->relation == REL_DIFFERENT && fatOk;
}

static int rowsInTab(int t, int* map)
{
    int n = 0;
    for (int i = 0; i < itemCount; i++)
    {
        if (inTab(&items[i], t))
        {
            if (map)
            {
                map[n] = i;
            }
            n++;
        }
    }
    return n;
}

static const char* relationText(const Item* it, int t)
{
    if (t == 0)
    {
        return it->relation == REL_ONLY_R4 ? "new" : it->relation == REL_SAME ? "same" : "different";
    }
    return it->relation == REL_SAME ? "same" : "edited";
}

static void drawTitle(void)
{
    uiHousing(UI_TOP);
    uiRect(UI_TOP, 0, 0, UI_W, 18, COL_VOID);
    uiRect(UI_TOP, 0, 17, UI_W, 1, COL_GRID);
    uiText(UI_TOP, 6, 2, "R4 PokéLink", COL_INK, 0);
    uiText(UI_TOP, UI_W - 6 - uiTextWidth(VERSION), 2, VERSION, COL_INKSOFT, 0);
    uiText(UI_TOP, 6 + uiTextWidth("R4 PokéLink") + 8, 2, "by En3rGonix", COL_INKSOFT, 0);
}

static void showProgress(const char* line)
{
    drawTitle();
    uiPanel(UI_TOP, 8, 60, 240, 50, COL_PANEL, COL_FRAMEEDGE);
    uiTextCentered(UI_TOP, 128, 78, line, COL_INK);
    uiHousing(UI_BOTTOM);
    uiTextCentered(UI_BOTTOM, 128, 88, "One moment...", COL_INKSOFT);
    uiPresent();
}

static void showMessage(const char* heading, const char* body)
{
    drawTitle();
    uiPanel(UI_TOP, 8, 26, 240, 156, COL_PANEL, COL_FRAMEEDGE);
    uiText(UI_TOP, 16, 32, heading, COL_GOLD, 224);
    uiWrap(UI_TOP, 16, 50, 224, body, COL_INK, 10);
    uiHousing(UI_BOTTOM);
    uiPanel(UI_BOTTOM, 2, 170, 252, 21, COL_DECK, COL_FRAMEEDGE);
    uiKeyHint(UI_BOTTOM, 90, 174, "START", "Exit");
    uiPresent();
}

static void drawBrowser(void)
{
    int map[MAX_SAVES];
    int rows = rowsInTab(tab, map);

    // Top screen: the save under the cursor.
    drawTitle();
    uiPanel(UI_TOP, 6, 24, 244, 112, COL_PANEL, COL_FRAMEEDGE);
    if (rows > 0)
    {
        const Item* it = &items[map[focus]];
        uiWrap(UI_TOP, 14, 30, 228, it->name, COL_INK, 2);
        char line[96];
        snprintf(line, sizeof(line), "%s   R4: %lu KB", it->sys, (unsigned long)(it->r4Size / 1024));
        uiText(UI_TOP, 14, 58, line, COL_INKSOFT, 0);
        if (it->onSd)
        {
            snprintf(line, sizeof(line), "3DS copy: %lu KB, %s", (unsigned long)(it->sdSize / 1024),
                it->relation == REL_SAME ? "same as the R4" : "differs from the R4");
        }
        else
        {
            snprintf(line, sizeof(line), "Not on the 3DS yet");
        }
        uiText(UI_TOP, 14, 72, line, it->relation == REL_DIFFERENT ? COL_GOLD : COL_INKSOFT, 228);
        const char* hint = tab == 0 ? "Copying to the 3DS makes it ready in PKForge (3ds/PKForge/saves/R4). "
                                      "SELECT deletes the R4's save."
                                    : "Writing to the R4 replaces the R4's save with this copy, after a backup on the SD. "
                                      "SELECT deletes the 3DS copy.";
        uiWrap(UI_TOP, 14, 92, 228, hint, COL_SUBINK, 3);
    }
    else
    {
        uiWrap(UI_TOP, 14, 34, 228,
            tab == 0 ? "No Pokemon save with data on the R4." : "No 3DS copy to send back to the R4.",
            COL_INKSOFT, 3);
    }
    uiPanel(UI_TOP, 6, 142, 244, 44, COL_DEEP, COL_FRAMEEDGE);
    uiWrap(UI_TOP, 12, 146, 232, status[0] ? status : "Pick the saves and press X.", COL_INK, 3);

    // Bottom screen: tabs, the list with checkboxes, the keys.
    uiHousing(UI_BOTTOM);
    for (int t = 0; t < 2; t++)
    {
        int x  = 4 + t * 126;
        bool on = t == tab;
        uiPanel(UI_BOTTOM, x, 3, 122, 18, on ? COL_GRID : COL_DEEP, on ? COL_CYAN : COL_FRAMEEDGE);
        uiTextCentered(UI_BOTTOM, x + 61, 5, tabName(t), on ? COL_INK : COL_INKSOFT);
    }
    const int listY = 24, rowH = 20, visible = 7;
    uiPanel(UI_BOTTOM, 2, listY - 1, 252, visible * rowH + 3, COL_BAND, COL_FRAMEEDGE);
    for (int r = 0; r < visible && firstRow + r < rows; r++)
    {
        int idx      = firstRow + r;
        const Item* it = &items[map[idx]];
        int y        = listY + 1 + r * rowH;
        bool focused = idx == focus;
        if (focused)
        {
            uiPanel(UI_BOTTOM, 4, y, 248, rowH - 1, COL_CHIPTOP, COL_CYAN);
        }
        bool can = selectable(it, tab);
        uiCheckbox(UI_BOTTOM, 8, y + 3, it->pick[tab], can);
        const char* rel = relationText(it, tab);
        int relW        = uiTextWidth(rel);
        uiText(UI_BOTTOM, 24, y + 2, it->name, can ? COL_INK : COL_SUBINK, 246 - 24 - relW - 10);
        uiText(UI_BOTTOM, 248 - relW, y + 2, rel,
            it->relation == REL_DIFFERENT ? COL_GOLD : it->relation == REL_ONLY_R4 ? COL_LEGAL : COL_SUBINK, 0);
    }
    if (rows > visible)
    {
        int knob = visible * rowH * visible / rows;
        int pos  = (visible * rowH - knob) * firstRow / (rows - visible);
        uiRect(UI_BOTTOM, 251, listY + pos, 2, knob, COL_CYAN);
    }
    uiPanel(UI_BOTTOM, 2, 168, 252, 23, COL_DECK, COL_FRAMEEDGE);
    int x = 6;
    x += uiKeyHint(UI_BOTTOM, x, 173, "A", "Mark") + 6;
    x += uiKeyHint(UI_BOTTOM, x, 173, "X", tab == 0 ? "Copy" : "Write") + 6;
    x += uiKeyHint(UI_BOTTOM, x, 173, "Y", "All") + 6;
    uiKeyHint(UI_BOTTOM, x, 173, "B", "Exit");
    uiPresent();
}

// ---------------------------------------------------------------- the saves

static void scanR4(const char* sys)
{
    char dirPath[64];
    snprintf(dirPath, sizeof(dirPath), "r4:/roms/%s", sys);
    DIR* dir = opendir(dirPath);
    if (!dir)
    {
        return;
    }
    struct dirent* e;
    while ((e = readdir(dir)) && itemCount < MAX_SAVES)
    {
        if (e->d_type == DT_DIR || !endsWith(e->d_name, ".sav") || !hasPokemon(e->d_name))
        {
            continue;
        }
        Item* it = &items[itemCount];
        memset(it, 0, sizeof(*it));
        snprintf(it->name, sizeof(it->name), "%s", e->d_name);
        snprintf(it->sys, sizeof(it->sys), "%s", sys);
        snprintf(it->r4Path, sizeof(it->r4Path), "%s/%s", dirPath, e->d_name);
        snprintf(it->r4Rel, sizeof(it->r4Rel), "roms/%s/%s", sys, e->d_name);
        snprintf(it->sdPath, sizeof(it->sdPath), DEST_ROOT "/%s/%s", sys, e->d_name);
        itemCount++;
    }
    closedir(dir);
}

// The R4's copy of a save straight from its sectors: no filesystem cache, so it is right even just
// after a write (remounting the R4 to drop the cache hung). The filesystem when FAT32 is not mapped.
static u8* readR4(const Item* it, u32* size)
{
    *size = 0;
    if (!fatOk)
    {
        return readWhole(it->r4Path, size);
    }
    u32 cluster = 0, bytes = 0;
    if (!fat32Find(&fat, it->r4Rel, &cluster, &bytes) || bytes == 0 || bytes > MAX_SAVE_SIZE)
    {
        return NULL;
    }
    int count = fat32FileSectors(&fat, cluster, bytes, sectorList, MAX_SECTORS);
    u8* data  = count > 0 ? malloc(count * 512) : NULL;
    if (!data)
    {
        return NULL;
    }
    for (int i = 0; i < count; i++)
    {
        if (!ace3dsReadSectors(sectorList[i], 1, data + i * 512))
        {
            free(data);
            return NULL;
        }
    }
    *size = bytes;
    return data;
}

// Reads both sides of one save and says how they relate.
static void compare(Item* it)
{
    u32 r4Size = 0, sdSize = 0;
    u8* r4 = readR4(it, &r4Size);
    u8* sd = readWhole(it->sdPath, &sdSize);
    it->r4Size  = r4Size;
    it->r4Blank = !r4 || blank(r4, r4Size);
    it->onSd    = sd != NULL;
    it->sdSize  = sdSize;
    if (!sd)
    {
        it->relation = REL_ONLY_R4;
    }
    else if (r4 && r4Size == sdSize && memcmp(r4, sd, r4Size) == 0)
    {
        it->relation = REL_SAME;
    }
    else
    {
        it->relation = REL_DIFFERENT;
    }
    it->pick[0] = it->pick[1] = false;
    free(r4);
    free(sd);
}

// A copy already on the 3DS SD card that differs is kept as .old1, .old2...
static bool keepOldCopy(const char* path)
{
    char old[300];
    for (int n = 1; n < 100; n++)
    {
        snprintf(old, sizeof(old), "%s.old%d", path, n);
        struct stat st;
        if (stat(old, &st) != 0)
        {
            return rename(path, old) == 0;
        }
    }
    return false;
}

static bool copyToSd(Item* it)
{
    u32 size = 0;
    u8* data = readR4(it, &size);
    if (!data)
    {
        return false;
    }
    char dir[96];
    snprintf(dir, sizeof(dir), DEST_ROOT "/%s", it->sys);
    mkdir(DEST_ROOT, 0777);
    mkdir(dir, 0777);
    bool ok = true;
    struct stat st;
    if (stat(it->sdPath, &st) == 0)
    {
        ok = keepOldCopy(it->sdPath);
    }
    ok = ok && writeWhole(it->sdPath, data, size);
    free(data);
    say("to 3DS %s: %s", it->name, ok ? "ok" : "FAILED");
    return ok;
}

// Writes the 3DS copy over the R4's save of the same size, sector by sector, touching nothing else.
// Erasing writes FF over the whole save instead: the file stays, as a fresh cartridge's would.
static const char* writeToR4(Item* it, bool erase)
{
    u32 newSize = 0, oldSize = 0;
    u8* old   = readR4(it, &oldSize);
    u8* fresh = NULL;
    if (!erase)
    {
        fresh = readWhole(it->sdPath, &newSize);
    }
    else if (old && (fresh = malloc(oldSize)))
    {
        memset(fresh, 0xFF, oldSize);
        newSize = oldSize;
    }
    const char* error = NULL;
    u32 cluster = 0, size = 0;
    int count = 0;
    if (!fresh || !old || newSize != oldSize)
    {
        error = "the sizes do not match";
    }
    else if (!fatOk || !fat32Find(&fat, it->r4Rel, &cluster, &size) || size != oldSize)
    {
        error = "the file is not in the R4's FAT";
    }
    else if ((count = fat32FileSectors(&fat, cluster, size, sectorList, MAX_SECTORS)) <= 0)
    {
        error = "the cluster chain looks wrong";
    }
    if (!error)
    {
        // The sectors must hold exactly the save the filesystem read: else the map is wrong.
        for (int i = 0; i < count && !error; i++)
        {
            u32 n = (i + 1) * 512 <= size ? 512 : size - i * 512;
            if (!ace3dsReadSectors(sectorList[i], 1, rawSector) || memcmp(rawSector, old + i * 512, n) != 0)
            {
                error = "the sectors do not match the save";
            }
        }
    }
    if (!error)
    {
        char when[32], dir[96], backup[320];
        stamp(when, sizeof(when));
        snprintf(dir, sizeof(dir), BACKUP_ROOT "/%s", it->sys);
        mkdir(BACKUP_ROOT, 0777);
        mkdir(dir, 0777);
        snprintf(backup, sizeof(backup), "%s/%s.%s.bak", dir, it->name, when);
        if (!writeWhole(backup, old, oldSize))
        {
            error = "the backup could not be saved";
        }
        else
        {
            say("backup of %s: %s", it->name, backup);
        }
    }
    int written = 0;
    if (!error)
    {
        for (int i = 0; i < count && !error; i++)
        {
            u32 n = (i + 1) * 512 <= size ? 512 : size - i * 512;
            if (memcmp(fresh + i * 512, old + i * 512, n) == 0)
            {
                continue; // unchanged sector
            }
            // Clearing rewrites the whole save, about a thousand sectors: keep the screen moving.
            if (written % 128 == 0)
            {
                say("%s %s: sector %d of %d", erase ? "clearing" : "writing", it->name, i, count);
            }
            if (written % 16 == 0)
            {
                char line[64];
                snprintf(line, sizeof(line), "%s no R4... %d%%", erase ? "A apagar" : "A escrever", i * 100 / count);
                showProgress(line);
            }
            if (!ace3dsReadSectors(sectorList[i], 1, rawSector))
            {
                error = "a read before writing failed";
                break;
            }
            memcpy(rawSector, fresh + i * 512, n);
            if (!ace3dsWriteSector(sectorList[i], rawSector))
            {
                error = "a write to the R4 failed";
                break;
            }
            written++;
            u32 check[128];
            if (!ace3dsReadSectors(sectorList[i], 1, check) || memcmp(check, rawSector, 512) != 0)
            {
                error = "the check after writing failed";
            }
        }
        if (error && written)
        {
            // Put the old save back sector by sector; the backup on the 3DS SD stays either way.
            for (int i = 0; i < count; i++)
            {
                u32 n = (i + 1) * 512 <= size ? 512 : size - i * 512;
                if (ace3dsReadSectors(sectorList[i], 1, rawSector))
                {
                    memcpy(rawSector, old + i * 512, n);
                    ace3dsWriteSector(sectorList[i], rawSector);
                }
            }
            say("restored %s after: %s", it->name, error);
        }
    }
    say("to R4 %s: %s (%d sectors written)", it->name, error ? error : "ok", written);
    free(fresh);
    free(old);
    return error;
}

// ---------------------------------------------------------------- diagnostics

static void dumpCard(const NtrHeader* header)
{
    mkdir(DUMP_ROOT, 0777);
    writeWhole(DUMP_ROOT "/header.bin", (const u8*)header, sizeof(*header));
    writeWhole(DUMP_ROOT "/secure_area_key1.bin", (const u8*)cardSecureArea(), 0x4000);
    FILE* f = fopen(DUMP_ROOT "/rom_8000.bin", "wb");
    if (f)
    {
        u32 buf[128];
        for (u32 address = 0x8000; address < 0x8000 + 0x100000; address += 0x200)
        {
            cardMainRead(header, address, buf);
            fwrite(buf, 1, 512, f);
        }
        fclose(f);
    }
    say("card dumped to " DUMP_ROOT);
}

static bool diagnose(const NtrHeader* header, u32 chipId)
{
    const u8* h = (const u8*)header;
    #define H32(o) ((u32)h[o] | ((u32)h[(o) + 1] << 8) | ((u32)h[(o) + 2] << 16) | ((u32)h[(o) + 3] << 24))
    say("ARM9 rom %08lX entry %08lX ram %08lX size %08lX", (unsigned long)H32(0x20), (unsigned long)H32(0x24),
        (unsigned long)H32(0x28), (unsigned long)H32(0x2C));
    say("ARM7 rom %08lX entry %08lX ram %08lX size %08lX", (unsigned long)H32(0x30), (unsigned long)H32(0x34),
        (unsigned long)H32(0x38), (unsigned long)H32(0x3C));
    say("ROMCTRL normal %08lX key1 %08lX, seed %02X", (unsigned long)H32(0x60), (unsigned long)H32(0x64), h[0x13]);
    #undef H32
    u32 sdStatusBefore = ace3dsCommand4(0xB0, 0);
    say("B0 before anything %08lX", (unsigned long)sdStatusBefore);
    u32 mainChipId = ace3dsCommand4(0xB8, 0);
    say("B8 chip ID %08lX (%s)", (unsigned long)mainChipId, mainChipId == chipId ? "KEY2 OK" : "KEY2 MISMATCH");
    say("B0 status %08lX", (unsigned long)ace3dsCommand4(0xB0, 0));
    for (int wait = 0; wait < 120 && (ace3dsCommand4(0xB0, 0) & 7) != 4; wait++)
    {
        swiWaitForVBlank();
    }
    say("B0 after wait %08lX", (unsigned long)ace3dsCommand4(0xB0, 0));

    bool good = false;
    for (int attempt = 0; attempt < 3; attempt++)
    {
        u32 probe[128];
        memset(probe, 0, sizeof(probe));
        bool read = ace3dsReadSectors(0, 1, probe);
        u8* p     = (u8*)probe;
        u32 nonzero = 0;
        for (int i = 0; i < 512; i++)
        {
            nonzero += p[i] != 0;
        }
        say("S0 #%d %s polls %lu last %08lX: %02X%02X%02X%02X %02X%02X%02X%02X .. %02X%02X, %lu non-zero", attempt,
            read ? "read" : "FAIL", (unsigned long)ace3dsLastPolls, (unsigned long)ace3dsLastAnswer, p[0], p[1], p[2],
            p[3], p[4], p[5], p[6], p[7], p[510], p[511], (unsigned long)nonzero);
        say("   1BE: %02X %02X%02X%02X %02X %02X%02X%02X lba %02X%02X%02X%02X", p[0x1BE], p[0x1BF], p[0x1C0],
            p[0x1C1], p[0x1C2], p[0x1C3], p[0x1C4], p[0x1C5], p[0x1C9], p[0x1C8], p[0x1C7], p[0x1C6]);
        good = good || (read && p[510] == 0x55 && p[511] == 0xAA);
    }
    const u32 starts[] = {2048, 8192, 32768};
    for (unsigned i = 0; i < sizeof(starts) / sizeof(starts[0]); i++)
    {
        u32 probe[128];
        bool read = ace3dsReadSectors(starts[i], 1, probe);
        u8* p     = (u8*)probe;
        say("S%lu %s: %02X%02X%02X %.8s .. %02X%02X", (unsigned long)starts[i], read ? "read" : "FAIL", p[0], p[1],
            p[2], (char*)p + 3, p[510], p[511]);
    }
    if (!good)
    {
        dumpCard(header);
    }
    return good;
}

// ---------------------------------------------------------------- the main loop

static void waitStart(void)
{
    while (pmMainLoop())
    {
        swiWaitForVBlank();
        scanKeys();
        if (keysDown() & (KEY_START | KEY_B))
        {
            return;
        }
    }
}

static int pickedCount(int t)
{
    int n = 0;
    for (int i = 0; i < itemCount; i++)
    {
        n += inTab(&items[i], t) && items[i].pick[t];
    }
    return n;
}

static bool confirmWrite(int n)
{
    char body[200];
    snprintf(body, sizeof(body),
        "Write %d save%s to the R4? Each R4 save is kept first in 3ds/PKForge/r4backups on the 3DS's "
        "SD card.\n\nA: yes, write     B: no",
        n, n == 1 ? "" : "s");
    showMessage("Confirm", body);
    while (pmMainLoop())
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown();
        if (down & KEY_A)
        {
            return true;
        }
        if (down & KEY_B)
        {
            return false;
        }
    }
    return false;
}

// SELECT: on the R4 tab the R4's save is cleared, on the 3DS tab the 3DS copy goes (kept as .oldN).
static void eraseFocused(Item* it)
{
    char body[360];
    if (tab == 0)
    {
        if (!fatOk)
        {
            setStatus("Deleting on the R4 needs FAT32.");
            return;
        }
        snprintf(body, sizeof(body),
            "Delete the R4's save?\n\n%s\n\nThe game starts over. A backup stays in 3ds/PKForge/r4backups on the "
            "3DS's SD card.\n\nA: yes, delete     B: no",
            it->name);
    }
    else
    {
        snprintf(body, sizeof(body),
            "Delete the 3DS copy?\n\n%s\n\nThe R4's save stays as it is. The copy is renamed .old on the SD.\n\n"
            "A: yes, delete     B: no",
            it->name);
    }
    showMessage("Delete", body);
    bool yes = false;
    while (pmMainLoop())
    {
        swiWaitForVBlank();
        scanKeys();
        u32 down = keysDown();
        if (down & (KEY_A | KEY_B))
        {
            yes = down & KEY_A;
            break;
        }
    }
    if (!yes)
    {
        setStatus("Nothing was deleted.");
        return;
    }
    const char* error = NULL;
    if (tab == 0)
    {
        showProgress("Deleting on the R4...");
        error = writeToR4(it, true);
    }
    else if (!keepOldCopy(it->sdPath))
    {
        error = "the copy could not be renamed";
    }
    say("erase %s on %s: %s", it->name, tab == 0 ? "R4" : "3DS", error ? error : "ok");
    compare(it);
    char text[160];
    snprintf(text, sizeof(text), error ? "Not deleted: %s." : "Deleted.", error);
    setStatus(text);
}

static void runCopy(void)
{
    int n = pickedCount(tab);
    if (n == 0)
    {
        setStatus("Nothing marked: mark saves with A or by touching them.");
        return;
    }
    if (tab == 1 && !confirmWrite(n))
    {
        setStatus("Nothing was written to the R4.");
        return;
    }
    int done = 0, failed = 0, index = 0;
    const char* lastError = NULL;
    for (int i = 0; i < itemCount; i++)
    {
        Item* it = &items[i];
        if (!inTab(it, tab) || !it->pick[tab])
        {
            continue;
        }
        char line[96];
        snprintf(line, sizeof(line), "%s %d de %d...", tab == 0 ? "A copiar" : "A escrever", ++index, n);
        showProgress(line);
        const char* error = tab == 0 ? (copyToSd(it) ? NULL : "the copy failed") : writeToR4(it, false);
        if (error)
        {
            failed++;
            lastError = error;
        }
        else
        {
            done++;
        }
        compare(it);
    }
    char text[160];
    if (failed)
    {
        snprintf(text, sizeof(text), "%d done%s, %d failed: %s.", done, "", failed, lastError);
    }
    else if (tab == 0)
    {
        snprintf(text, sizeof(text), "%d save%s copied to the 3DS. Open PKForge to edit.", done,
            done == 1 ? "" : "s");
    }
    else
    {
        snprintf(text, sizeof(text), "%d save%s written to the R4.", done, done == 1 ? "" : "s");
    }
    setStatus(text);
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    uiInit();
    srand(time(NULL));
    showProgress("Starting...");

    if (!isDSiMode())
    {
        showMessage("DS mode", "Open R4 PokéLink from TWiLight Menu++ on the 3DS's SD card, so it runs in DSi mode.");
        waitStart();
        return 0;
    }
    struct stat sdRoot;
    if (!fatInitDefault() || stat("sd:/", &sdRoot) != 0)
    {
        showMessage("No SD card", "The 3DS's SD card could not be read.");
        waitStart();
        return 0;
    }
    mkdir("sd:/3ds", 0777);
    mkdir("sd:/3ds/PKForge", 0777);
    mkdir("sd:/3ds/PKForge/saves", 0777);
    logFile = fopen(LOG_PATH, "w");
    say("R4 PokéLink " VERSION);
    restoreTwilight();
    struct stat mark;
    if (stat(FROM_PKFORGE, &mark) == 0)
    {
        fromPKForge = true;
        remove(FROM_PKFORGE);
    }
    say("Started by PKForge: %s", fromPKForge ? "yes" : "no");

    showProgress("Talking to the R4...");
    NtrHeader header;
    u32 chipId = 0;
    int init   = cardInitMainMode(&header, &chipId);
    if (init == CARD_INIT_NO_CARD || init == CARD_INIT_BUSY)
    {
        say(init == CARD_INIT_NO_CARD ? "No card in Slot-1." : "The card bus is held by the ARM7.");
        showMessage("No card", "Put the R4 in the game card slot and open R4 PokéLink again.");
        waitStart();
        return 0;
    }
    char code[5] = {0}, name[13] = {0};
    memcpy(code, header.gameCode, 4);
    memcpy(name, header.title, 12);
    say("Card: \"%s\" %s", name, code);
    say("Chip ID %08lX, header %s", (unsigned long)chipId, init == CARD_INIT_OK ? "OK" : "CRC BAD");

    // From a fresh boot the R4 still plays its disguise and its microSD is off: wake both up.
    showProgress("Starting the R4's microSD...");
    ace3dsLeaveGameMode(&header, say);
    bool sdStarted = ace3dsSdInit(say);
    say("microSD start-up: %s", sdStarted ? "ok" : "FAILED");

    showProgress("Reading the R4's microSD...");
    if (!diagnose(&header, chipId))
    {
        showMessage("Could not read the R4",
            "The R4 answered, but its microSD did not read well. Nothing was changed. A diagnosis is in "
            "3ds/PKForge/r4pokelink.log and 3ds/PKForge/r4dump.");
        waitStart();
        return 0;
    }
    if (!fatMountSimple("r4", &ace3dsReadOnlyInterface))
    {
        say("FAT mount failed.");
        showMessage("Could not open the R4", "The R4's microSD read, but its FAT did not mount. Nothing was changed.");
        waitStart();
        return 0;
    }
    fatOk = fat32Open(&fat);
    say("R4 mounted; raw FAT32 %s", fatOk ? "ok" : "unavailable (write-back off)");

    showProgress("Looking for saves...");
    const char* systems[] = {"nds", "gba", "gb", "gbc"};
    for (unsigned i = 0; i < sizeof(systems) / sizeof(systems[0]); i++)
    {
        scanR4(systems[i]);
    }
    for (int i = 0; i < itemCount; i++)
    {
        char line[64];
        snprintf(line, sizeof(line), "Comparing %d of %d...", i + 1, itemCount);
        showProgress(line);
        compare(&items[i]);
        say("%s/%s: R4 %lu%s, 3DS %s", items[i].sys, items[i].name, (unsigned long)items[i].r4Size,
            items[i].r4Blank ? " (empty)" : "", items[i].onSd ? relationText(&items[i], 1) : "none");
    }

    touchPosition touch;
    bool wasTouching = false, dirty = true;
    int touchStartY = 0, touchRow = -1;
    while (pmMainLoop())
    {
        int map[MAX_SAVES];
        int rows = rowsInTab(tab, map);
        if (focus >= rows)
        {
            focus = rows > 0 ? rows - 1 : 0;
        }
        if (focus < firstRow)
        {
            firstRow = focus;
        }
        if (focus >= firstRow + 7)
        {
            firstRow = focus - 6;
        }
        // Redrawn only after something happened: the screens stay still in between.
        if (dirty)
        {
            drawBrowser();
            dirty = false;
        }
        else
        {
            swiWaitForVBlank();
        }

        scanKeys();
        u32 down = keysDown(), held = keysHeld(), rep = keysDownRepeat();
        if (down || rep || (held & KEY_TOUCH) || wasTouching)
        {
            dirty = true;
        }
        if (rep & KEY_UP && focus > 0)
        {
            focus--;
        }
        if (rep & KEY_DOWN && focus + 1 < rows)
        {
            focus++;
        }
        if (down & (KEY_L | KEY_R | KEY_LEFT | KEY_RIGHT))
        {
            tab      = 1 - tab;
            focus    = firstRow = 0;
            status[0] = 0;
        }
        if ((down & KEY_A) && rows > 0)
        {
            Item* it = &items[map[focus]];
            if (selectable(it, tab))
            {
                it->pick[tab] = !it->pick[tab];
            }
            else
            {
                setStatus(tab == 1 && !fatOk ? "Writing to the R4 needs FAT32." : "This one is already the same.");
            }
        }
        if (down & KEY_Y)
        {
            bool any = pickedCount(tab) > 0;
            for (int i = 0; i < rows; i++)
            {
                Item* it = &items[map[i]];
                it->pick[tab] = !any && selectable(it, tab);
            }
        }
        if (down & KEY_X)
        {
            runCopy();
        }
        if ((down & KEY_SELECT) && rows > 0)
        {
            eraseFocused(&items[map[focus]]);
        }
        if (down & (KEY_B | KEY_START))
        {
            if (fromPKForge)
            {
                showMessage("Done", "Press HOME to go back to the 3DS and open PKForge: the copied saves "
                                      "are there.\n\nB: back to R4 PokeLink");
                waitStart();
                dirty = true;
                continue;
            }
            break;
        }

        // Touch: the tabs, then a tap on a row marks it.
        bool touching = held & KEY_TOUCH;
        if (touching)
        {
            touchRead(&touch);
        }
        if (touching && !wasTouching)
        {
            touchStartY = touch.py;
            touchRow    = -1;
            if (touch.py < 22)
            {
                int t = touch.px < 128 ? 0 : 1;
                if (t != tab)
                {
                    tab       = t;
                    focus     = firstRow = 0;
                    status[0] = 0;
                }
            }
            else if (touch.py >= 24 && touch.py < 24 + 7 * 20)
            {
                int idx = firstRow + (touch.py - 24) / 20;
                if (idx < rows)
                {
                    touchRow = idx;
                    focus    = idx;
                }
            }
            else if (touch.py >= 168)
            {
                // The footer: X's place copies, the rest of it does nothing on purpose.
                if (touch.px >= 60 && touch.px < 150)
                {
                    runCopy();
                }
            }
        }
        if (touching && touchRow >= 0)
        {
            int moved = touch.py - touchStartY;
            if (moved > 20 || moved < -20)
            {
                // A drag scrolls instead of marking.
                int step = moved / 20;
                firstRow -= step;
                if (firstRow < 0)
                {
                    firstRow = 0;
                }
                if (firstRow > rows - 7)
                {
                    firstRow = rows > 7 ? rows - 7 : 0;
                }
                focus       = firstRow;
                touchStartY = touch.py;
                touchRow    = -2;
            }
        }
        if (!touching && wasTouching && touchRow >= 0 && touchRow < rows)
        {
            Item* it = &items[map[touchRow]];
            if (selectable(it, tab))
            {
                it->pick[tab] = !it->pick[tab];
            }
        }
        wasTouching = touching;
    }
    if (logFile)
    {
        fclose(logFile);
    }
    return 0;
}

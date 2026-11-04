/*
    The Ace3DS+ start-up sequence of flashcart_core (ntrteam et al.), devices/ace3dsplus.cpp:
    passAntiAntiPiracy, cmdSd/cmdSdRaw/cmdSdRegister and cartSdInit, ported from C++/libncgc to C
    for R4 PokéLink.

    flashcart_core is free software under the GNU General Public License version 3; this file
    is distributed under the same licence (GPL-3.0-or-later). See LICENSE.
*/

#include "ace3ds_sdinit.h"
#include "ace3ds.h"

#include <nds.h>
#include <string.h>

#define B0_POLL_LIMIT 200000

static u32 versionStatus(void)
{
    static const u8 cmd[8] = {0xB0, 0, 0, 0, 0, 0, 0, 0};
    u32 word = 0xFFFFFFFF;
    ace3dsTransfer(cmd, ACE3DS_ROMCTRL_WORD, &word, 1);
    return word;
}

// Sets some SD-related register on the card: 0 at the start, then 1 (SDSC) or 3 (SDHC).
static void sdRegister(u8 param)
{
    const u8 cmd[8] = {0xC2, 0, 0, 0, param, 0, 0, 0};
    ace3dsTransfer(cmd, ACE3DS_ROMCTRL_NODATA, NULL, 0);
}

static void sdRaw(u8 x, u32 y, u8 z)
{
    const u8 cmd[8] = {0xC0, x, (u8)(y >> 24), (u8)(y >> 16), (u8)(y >> 8), (u8)y, z, 0};
    ace3dsTransfer(cmd, ACE3DS_ROMCTRL_NODATA, NULL, 0);
}

static void readBuffer(u8* resp)
{
    static const u8 cmd[8] = {0xBA, 0, 0, 0, 0, 0, 0, 0};
    u32 words[128];
    ace3dsTransfer(cmd, ACE3DS_ROMCTRL_SECTOR, words, 128);
    if (resp)
    {
        memcpy(resp, words, 0x200);
    }
}

// Waits until the status bit clears and two reads in a row agree.
static bool waitClear(u32 bit, u32* last)
{
    u32 prev = bit;
    for (u32 guard = 0; guard < B0_POLL_LIMIT; guard++)
    {
        u32 tr = versionStatus();
        if (!(tr & bit) && tr == prev)
        {
            *last = tr;
            return true;
        }
        prev = tr;
    }
    return false;
}

// Sends a raw SD command (x: index, y: argument, z: response kind), then polls for the result.
static bool sdCommand(u8 x, u32 y, u8 z, u8* resp)
{
    for (int timeout = 5; timeout > 0; timeout--)
    {
        sdRaw(x, y, z);
        u32 tr;
        if (!waitClear(4, &tr))
        {
            return false;
        }
        if (!(z & 8))
        {
            return true;
        }
        if (!waitClear(8, &tr))
        {
            return false;
        }
        if (!(tr & 0x20))
        {
            readBuffer(resp);
            return true;
        }
    }
    return false;
}

u32 ace3dsLeaveGameMode(const NtrHeader* header, Ace3dsLog log)
{
    // The Deep Labyrinth flash's trigger: two ROM reads the game never makes.
    u32 buf[128];
    cardMainRead(header, 0x10FE00, buf);
    cardMainRead(header, 0x167400, buf);
    u32 resp = 0;
    for (int i = 0; i < 0x10; i++)
    {
        resp = versionStatus();
        if (resp != 0 && resp != 0xFFFFFFFF)
        {
            break;
        }
    }
    if (resp == 0 || resp == 0xFFFFFFFF)
    {
        // flashcart_core's last resort: sweep the first 2 MiB of ROM twice.
        log("B0 %08lX after the trigger reads; sweeping the ROM", (unsigned long)resp);
        for (int pass = 0; pass < 2; pass++)
        {
            for (u32 address = 0x8000; address < 0x200000; address += 0x200)
            {
                cardMainRead(header, address, buf);
            }
        }
        resp = versionStatus();
    }
    log("B0 version/status %08lX", (unsigned long)resp);
    return resp;
}

bool ace3dsSdInit(Ace3dsLog log)
{
    u8 buf[0x200];
    sdRegister(0);
    if (!sdCommand(0, 0, 4, buf))
    {
        log("SD CMD0 failed");
        return false;
    }

    memset(buf, 0, sizeof(buf));
    bool sdhc = sdCommand(8, 0x1C8, 0x1C, buf);
    u32 ocrArg = sdhc ? 0x40300000 : 0x300000;
    log("SD CMD8 %s: %02X %02X %02X %02X %02X", sdhc ? "answered" : "silent", buf[0], buf[1], buf[2], buf[3], buf[4]);
    if (sdhc && (buf[3] != 1 || buf[4] != 0xC8))
    {
        log("SD CMD8: unexpected answer");
        return false;
    }

    for (int timeout = 0x1000; timeout > 0; timeout--)
    {
        if (!sdCommand(0x37, 0, 0x1C, buf))
        {
            log("SD CMD55 failed");
            return false;
        }
        sdCommand(0x29, ocrArg, 0x1C, buf);
        if (!(buf[1] & 0x80))
        {
            continue; // still powering up
        }

        bool highCapacity = (buf[1] & 0x40) != 0;
        log("SD ready (OCR %02X%02X%02X%02X), %s", buf[1], buf[2], buf[3], buf[4], highCapacity ? "SDHC/SDXC" : "SDSC");
        sdCommand(2, 0, 0x3C, buf);
        sdCommand(3, 0, 0x1C, buf);
        sdRegister(highCapacity ? 3 : 1);

        u32 rca = ((u32)buf[2] << 16) | ((u32)buf[1] << 24);
        log("SD RCA %08lX", (unsigned long)rca);
        sdCommand(9, rca, 0x3C, buf);
        if ((buf[6] & 0xF) <= 8)
        {
            log("SD CSD looks wrong (%02X)", buf[6]);
            return false;
        }
        sdCommand(7, rca, 0x1C, buf);
        sdCommand(16, 0x200, 0x1C, buf);

        u32 tr;
        if (!waitClear(0x40, &tr))
        {
            log("SD: busy after CMD16");
        }
        sdCommand(0x37, rca, 0x1C, buf);
        sdCommand(6, 2, 0x1C, buf);
        log("SD start-up done");
        return true;
    }
    log("SD start-up timed out");
    return false;
}

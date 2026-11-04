/*
    The Ace3DS microSD read protocol, rewritten in C from Pico Loader's
    arm9/source/patches/platform/ace3ds/Ace3DSReadSdPatchCode.s:

    Copyright (c) 2025 LNH team

    This software is provided 'as-is', without any express or implied
    warranty. In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.

    Altered for R4 PokéLink: C instead of Thumb assembly, a timeout on the
    ready poll, and a read-only disc interface around it.
*/

#include "ace3ds.h"

#include <nds.h>
#include <nds/card.h>
#include <string.h>

// Port 40001A4h values Pico Loader uses: KEY2-encrypted commands and data, 4-byte and 512-byte blocks.
#define ACE3DS_ROMCTRL_POLL 0xA7586000
#define ACE3DS_ROMCTRL_READ 0xA1586000
#define ACE3DS_POLL_LIMIT   100000

static void sendCommand(u8 command, u32 sector)
{
    // B9 aa bb cc dd 00 00 00, the sector big-endian
    REG_CARD_COMMAND[0] = command;
    REG_CARD_COMMAND[1] = (u8)(sector >> 24);
    REG_CARD_COMMAND[2] = (u8)(sector >> 16);
    REG_CARD_COMMAND[3] = (u8)(sector >> 8);
    REG_CARD_COMMAND[4] = (u8)sector;
    REG_CARD_COMMAND[5] = 0;
    REG_CARD_COMMAND[6] = 0;
    REG_CARD_COMMAND[7] = 0;
}

u32 ace3dsTransfer(const u8* command, u32 romctrl, u32* dest, u32 words)
{
    REG_AUXSPICNTH = CARD_CR1_ENABLE;
    for (int i = 0; i < 8; i++)
    {
        REG_CARD_COMMAND[i] = command[i];
    }
    REG_ROMCTRL = romctrl;
    u32 n       = 0;
    while (REG_ROMCTRL & CARD_BUSY)
    {
        if (REG_ROMCTRL & CARD_DATA_READY)
        {
            u32 word = REG_CARD_DATA_RD;
            if (dest && n < words)
            {
                dest[n] = word;
            }
            n++;
        }
    }
    return n;
}

u32 ace3dsLastPolls;
u32 ace3dsLastAnswer;

u32 ace3dsCommand4(u8 command, u32 param)
{
    REG_AUXSPICNTH = CARD_CR1_ENABLE;
    sendCommand(command, param);
    REG_ROMCTRL = ACE3DS_ROMCTRL_POLL;
    u32 answer  = 0xFFFFFFFF;
    while (REG_ROMCTRL & CARD_BUSY)
    {
        if (REG_ROMCTRL & CARD_DATA_READY)
        {
            answer = REG_CARD_DATA_RD;
        }
    }
    return answer;
}

static bool readSector(u32 sector, u32* out)
{
    REG_AUXSPICNTH = CARD_CR1_ENABLE;

    // B9: ask for the sector, then poll until the card answers 0 (ready).
    u32 answer = 1;
    for (u32 tries = 0; answer != 0; tries++)
    {
        ace3dsLastPolls = tries + 1;
        if (tries >= ACE3DS_POLL_LIMIT)
        {
            return false;
        }
        sendCommand(0xB9, sector);
        REG_ROMCTRL = ACE3DS_ROMCTRL_POLL;
        while (REG_ROMCTRL & CARD_BUSY)
        {
            if (REG_ROMCTRL & CARD_DATA_READY)
            {
                answer = REG_CARD_DATA_RD;
            }
        }
        ace3dsLastAnswer = answer;
    }

    // BA: the 512 bytes of the sector.
    sendCommand(0xBA, 0);
    REG_ROMCTRL = ACE3DS_ROMCTRL_READ;
    u32 words = 0;
    while (REG_ROMCTRL & CARD_BUSY)
    {
        if (REG_ROMCTRL & CARD_DATA_READY)
        {
            u32 word = REG_CARD_DATA_RD;
            if (words < 128)
            {
                out[words] = word;
            }
            words++;
        }
    }
    return words == 128;
}

// BB with the 512 bytes, then BC aa bb cc dd 00 00 xx (xx: XOR of the first five bytes) polled
// until the card answers 0, as Pico Loader's Ace3DSWriteSdPatchCode.s does.
#define ACE3DS_ROMCTRL_WRITE 0xE1586000

bool ace3dsWriteSector(u32 sector, const u32* data)
{
    REG_AUXSPICNTH = CARD_CR1_ENABLE;
    sendCommand(0xBB, sector);
    REG_ROMCTRL = ACE3DS_ROMCTRL_WRITE;
    u32 words   = 0;
    while (REG_ROMCTRL & CARD_BUSY)
    {
        if (REG_ROMCTRL & CARD_DATA_READY)
        {
            REG_CARD_DATA_RD = words < 128 ? data[words] : 0;
            words++;
        }
    }
    if (words != 128)
    {
        return false;
    }

    u8 check = 0xBC ^ (u8)(sector >> 24) ^ (u8)(sector >> 16) ^ (u8)(sector >> 8) ^ (u8)sector;
    u32 answer = 1;
    for (u32 tries = 0; answer != 0; tries++)
    {
        if (tries >= ACE3DS_POLL_LIMIT)
        {
            return false;
        }
        sendCommand(0xBC, sector);
        REG_CARD_COMMAND[7] = check;
        REG_ROMCTRL         = ACE3DS_ROMCTRL_POLL;
        while (REG_ROMCTRL & CARD_BUSY)
        {
            if (REG_ROMCTRL & CARD_DATA_READY)
            {
                answer = REG_CARD_DATA_RD;
            }
        }
    }
    return true;
}

bool ace3dsReadSectors(u32 sector, u32 count, void* buffer)
{
    u32 aligned[128];
    u8* dst = (u8*)buffer;
    for (u32 i = 0; i < count; i++)
    {
        if (((u32)dst & 3) == 0)
        {
            if (!readSector(sector + i, (u32*)dst))
            {
                return false;
            }
        }
        else
        {
            if (!readSector(sector + i, aligned))
            {
                return false;
            }
            memcpy(dst, aligned, 512);
        }
        dst += 512;
    }
    return true;
}

static bool startup(void) { return true; }

static bool isInserted(void) { return !(REG_SCFG_MC & SCFG_MC_IS_EJECTED); }

static bool readSectors(sec_t sector, sec_t count, void* buffer) { return ace3dsReadSectors(sector, count, buffer); }

static bool writeSectors(sec_t sector, sec_t count, const void* buffer)
{
    (void)sector;
    (void)count;
    (void)buffer;
    return false;
}

static bool clearStatus(void) { return true; }

static bool shutdown(void) { return true; }

const DISC_INTERFACE ace3dsReadOnlyInterface = {
    0x44534341, // "ACSD"
    FEATURE_MEDIUM_CANREAD | FEATURE_SLOT_NDS,
    startup,
    isInserted,
    readSectors,
    writeSectors,
    clearStatus,
    shutdown,
};

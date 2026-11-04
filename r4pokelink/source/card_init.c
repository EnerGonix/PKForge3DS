/*
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    Modified for R4 PokéLink: the NTR-card handshake of GodMode9i's read_card.c
    (cardInitInternal), without NAND or DSi-card support, for libnds 2 / calico.
*/

#include "card_init.h"
#include "encryption.h"

#include <nds.h>
#include <nds/card.h>
#include <stdlib.h>
#include <string.h>

typedef union
{
    char title[4];
    u32 key;
} GameCode;

static const u8 cardSeedBytes[] = {0xE8, 0x4D, 0x5A, 0xB1, 0x17, 0x8F, 0x99, 0xD5};

static struct
{
    unsigned int iii, jjj, kkkkk, llll, mmm, nnn;
} key1data;

static u32 headerData[0x200 / sizeof(u32)];
static u32 secureArea[0x4000 / sizeof(u32)];

static void initKey1Encryption(u8* cmdData)
{
    key1data.iii   = rand() & 0x00000fff;
    key1data.jjj   = rand() & 0x00000fff;
    key1data.kkkkk = rand() & 0x000fffff;
    key1data.llll  = rand() & 0x0000ffff;
    key1data.mmm   = rand() & 0x00000fff;
    key1data.nnn   = rand() & 0x00000fff;

    cmdData[7] = CARD_CMD_ACTIVATE_BF;
    cmdData[6] = (u8)(key1data.iii >> 4);
    cmdData[5] = (u8)((key1data.iii << 4) | (key1data.jjj >> 8));
    cmdData[4] = (u8)key1data.jjj;
    cmdData[3] = (u8)(key1data.kkkkk >> 16);
    cmdData[2] = (u8)(key1data.kkkkk >> 8);
    cmdData[1] = (u8)key1data.kkkkk;
    cmdData[0] = (u8)rand();
}

// Note: cmdData must be aligned on a word boundary
static void createEncryptedCommand(u8 command, u8* cmdData, u32 block)
{
    unsigned long iii, jjj;

    if (command != CARD_CMD_SECURE_READ)
    {
        block = key1data.llll;
    }
    if (command == CARD_CMD_ACTIVATE_SEC)
    {
        iii = key1data.mmm;
        jjj = key1data.nnn;
    }
    else
    {
        iii = key1data.iii;
        jjj = key1data.jjj;
    }

    cmdData[7] = (u8)(command | (block >> 12));
    cmdData[6] = (u8)(block >> 4);
    cmdData[5] = (u8)((block << 4) | (iii >> 8));
    cmdData[4] = (u8)iii;
    cmdData[3] = (u8)(jjj >> 4);
    cmdData[2] = (u8)((jjj << 4) | (key1data.kkkkk >> 16));
    cmdData[1] = (u8)(key1data.kkkkk >> 8);
    cmdData[0] = (u8)key1data.kkkkk;

    crypt_64bit_up((u32*)cmdData);

    key1data.kkkkk += 1;
}

const u32* cardSecureArea(void) { return secureArea; }

void cardMainRead(const NtrHeader* header, u32 address, u32* dest)
{
    u32 flags = (header->cardControl13 & ~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1);
    cardParamCommand(CARD_CMD_DATA_READ, address, flags, dest, 0x200 / sizeof(u32));
}

// The secure area delay, in 131kHz units: waiting longer than asked is harmless.
static void cardDelay(u16 readTimeout)
{
    swiDelay(((readTimeout & 0x3FFF) + 3) * 128);
}

static void waitFrames(int n)
{
    for (int i = 0; i < n; i++)
    {
        swiWaitForVBlank();
    }
}

int cardInitMainMode(NtrHeader* header, u32* chipId)
{
    u8 cmdData[8] __attribute__((aligned(4)));

    if (REG_SCFG_MC & SCFG_MC_IS_EJECTED)
    {
        return CARD_INIT_NO_CARD;
    }
    if (!ntrcardOpen())
    {
        return CARD_INIT_BUSY;
    }

    // Power-cycle Slot-1, as GodMode9i does before talking to a flashcard in DSi mode.
    disableSlot1();
    waitFrames(25);
    enableSlot1();
    waitFrames(15);
    cardParamCommand(CARD_CMD_DUMMY, 0,
        CARD_ACTIVATE | CARD_nRESET | CARD_CLK_SLOW | CARD_BLK_SIZE(1) | CARD_DELAY1(0x1FFF) | CARD_DELAY2(0x3F),
        NULL, 0);

    REG_ROMCTRL   = 0;
    REG_AUXSPICNT = 0;
    waitFrames(25);
    REG_AUXSPICNT = CARD_CR1_ENABLE | CARD_CR1_IRQ;
    REG_ROMCTRL   = CARD_nRESET | CARD_SEC_SEED;
    while (REG_ROMCTRL & CARD_BUSY) {}
    cardReset();
    while (REG_ROMCTRL & CARD_BUSY) {}

    *chipId = cardReadID(CARD_CLK_SLOW);
    while (REG_ROMCTRL & CARD_BUSY) {}
    bool normalChip = (*chipId & BIT(31)) != 0; // secure area in 0x200 blocks; others use 0x1000

    // The header, unencrypted.
    memset(headerData, 0, sizeof(headerData));
    cardParamCommand(CARD_CMD_HEADER_READ, 0,
        CARD_ACTIVATE | CARD_nRESET | CARD_CLK_SLOW | CARD_BLK_SIZE(1) | CARD_DELAY1(0x1FFF) | CARD_DELAY2(0x3F),
        headerData, 0x200 / sizeof(u32));
    memcpy(header, headerData, sizeof(NtrHeader));
    int result = header->headerCRC16 == swiCRC16(0xFFFF, header, 0x15E) ? CARD_INIT_OK : CARD_INIT_HEADER_CRC;

    // KEY1 (Blowfish) for the commands that follow, seeded by the game code.
    GameCode* gameCode = (GameCode*)header->gameCode;
    init_keycode(gameCode->key, 2, 8);

    u32 portFlagsKey1 = CARD_ACTIVATE | CARD_nRESET | (header->cardControl13 & (CARD_WR | CARD_CLK_SLOW)) |
                        ((header->cardControlBF & (CARD_CLK_SLOW | CARD_DELAY1(0x1FFF))) +
                            ((header->cardControlBF & CARD_DELAY2(0x3F)) >> 16));
    if (!normalChip)
    {
        portFlagsKey1 |= CARD_SEC_LARGE;
    }

    // 3Ciiijjj xkkkkkxx - Activate KEY1 Encryption Mode
    initKey1Encryption(cmdData);
    cardPolledTransfer((header->cardControl13 & (CARD_WR | CARD_nRESET | CARD_CLK_SLOW)) | CARD_ACTIVATE, NULL, 0,
        cmdData);

    // 4llllmmm nnnkkkkk - Activate KEY2 Encryption Mode
    createEncryptedCommand(CARD_CMD_ACTIVATE_SEC, cmdData, 0);
    if (normalChip)
    {
        cardPolledTransfer(portFlagsKey1, NULL, 0, cmdData);
        cardDelay(header->readTimeout);
    }
    cardPolledTransfer(portFlagsKey1, NULL, 0, cmdData);

    // The KEY2 seeds; the card bus encrypts every later command and its data with them.
    REG_ROMCTRL  = 0;
    REG_CARD_1B0 = cardSeedBytes[header->deviceType & 0x07] | (key1data.nnn << 15) | (key1data.mmm << 27) | 0x6000;
    REG_CARD_1B4 = 0x879b9b05;
    REG_CARD_1B8 = key1data.mmm >> 5;
    REG_CARD_1BA = 0x5c;
    REG_ROMCTRL  = CARD_nRESET | CARD_SEC_SEED | CARD_SEC_EN | CARD_SEC_DAT;

    portFlagsKey1 |= CARD_SEC_EN | CARD_SEC_DAT;

    // 1lllliii jjjkkkkk - 2nd Get ROM Chip ID / Get KEY2 Stream
    createEncryptedCommand(CARD_CMD_SECURE_CHIPID, cmdData, 0);
    if (normalChip)
    {
        cardPolledTransfer(portFlagsKey1, NULL, 0, cmdData);
        cardDelay(header->readTimeout);
    }
    cardPolledTransfer(portFlagsKey1 | CARD_BLK_SIZE(7), NULL, 0, cmdData);

    // 2bbbbiii jjjkkkkk - Get Secure Area Block (read and dropped: the card expects it)
    u32 portFlagsSecRead = (header->cardControlBF & (CARD_CLK_SLOW | CARD_DELAY1(0x1FFF) | CARD_DELAY2(0x3F))) |
                           CARD_ACTIVATE | CARD_nRESET | CARD_SEC_EN | CARD_SEC_DAT;
    int offset = 0;
    for (int block = 4; block < 8; block++)
    {
        createEncryptedCommand(CARD_CMD_SECURE_READ, cmdData, block);
        if (normalChip)
        {
            cardPolledTransfer(portFlagsSecRead, NULL, 0, cmdData);
            cardDelay(header->readTimeout);
            for (int i = 8; i > 0; i--)
            {
                cardPolledTransfer(portFlagsSecRead | CARD_BLK_SIZE(1), secureArea + offset, 0x200, cmdData);
                offset += 0x200 / sizeof(u32);
            }
        }
        else
        {
            cardPolledTransfer(portFlagsSecRead | CARD_BLK_SIZE(4) | CARD_SEC_LARGE, secureArea + offset, 0x1000,
                cmdData);
            offset += 0x1000 / sizeof(u32);
        }
    }

    // Alllliii jjjkkkkk - Enter Main Data Mode
    createEncryptedCommand(CARD_CMD_DATA_MODE, cmdData, 0);
    if (normalChip)
    {
        cardPolledTransfer(portFlagsKey1, NULL, 0, cmdData);
        cardDelay(header->readTimeout);
    }
    cardPolledTransfer(portFlagsKey1, NULL, 0, cmdData);

    return result;
}

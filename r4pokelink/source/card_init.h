// Brings a DS card in Slot-1 from power-on to Main Data mode (KEY2 active) from DSi mode,
// the state the DS firmware leaves a card in when it boots it.
#pragma once

#include <nds/ndstypes.h>

typedef struct
{
    char title[12];      // 0x000
    char gameCode[4];    // 0x00C
    char makerCode[2];   // 0x010
    u8 unitCode;         // 0x012: 0 NDS, 2 NDS+DSi, 3 DSi
    u8 deviceType;       // 0x013: KEY2 seed select
    u8 deviceSize;       // 0x014
    u8 reserved1[7];     // 0x015
    u8 dsiFlags;         // 0x01C
    u8 region;           // 0x01D
    u8 romVersion;       // 0x01E
    u8 autostart;        // 0x01F
    u32 arm9romOffset;   // 0x020
    u32 unused1[15];     // 0x024..0x05F
    u32 cardControl13;   // 0x060: port 40001A4h for normal commands
    u32 cardControlBF;   // 0x064: port 40001A4h for KEY1 commands
    u32 bannerOffset;    // 0x068
    u16 secureCRC16;     // 0x06C
    u16 readTimeout;     // 0x06E: secure area delay, 131kHz units
    u8 unused2[0xEE];    // 0x070..0x15D
    u16 headerCRC16;     // 0x15E
    u8 unused3[0xA0];    // 0x160..0x1FF
} __attribute__((packed)) NtrHeader;

_Static_assert(sizeof(NtrHeader) == 0x200, "NtrHeader is one 0x200 header");

enum
{
    CARD_INIT_OK         = 0,
    CARD_INIT_NO_CARD    = 1,
    CARD_INIT_BUSY       = 2, // the other CPU owns the card bus
    CARD_INIT_HEADER_CRC = 3, // header read back wrong: the card did not answer as a DS card
};

// Power-cycles Slot-1 and runs the KEY1/KEY2 handshake. On return the header and the chip ID
// are filled in; a bad header CRC is reported but the handshake still ran.
int cardInitMainMode(NtrHeader* header, u32* chipId);

// Diagnostics: the secure area blocks read during the handshake (0x4000 bytes, still KEY1-encrypted)
// and one 0x200-byte ROM read in Main Data mode (command B7).
const u32* cardSecureArea(void);
void cardMainRead(const NtrHeader* header, u32 address, u32* dest);

// Brings an Ace3DS-family card from a fresh boot to working microSD access: the anti-anti-piracy
// reads that switch it from game emulation to flashcard mode, then the SD card's own start-up
// (CMD0, CMD8, ACMD41, CMD2, CMD3, CMD9, CMD7, CMD16, ACMD6) sent through the card.
#pragma once

#include <nds/ndstypes.h>

#include "card_init.h"

typedef void (*Ace3dsLog)(const char* format, ...);

// The card must be in Main Data mode (cardInitMainMode). Returns the B0 version/status word.
u32 ace3dsLeaveGameMode(const NtrHeader* header, Ace3dsLog log);
// True when the microSD answered every step; sector reads (B9/BA) work afterwards.
bool ace3dsSdInit(Ace3dsLog log);

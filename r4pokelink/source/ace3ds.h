// The microSD of Ace3DS-family flashcards (Ace3DS+, r4isdhc.hk 2020+...) over the Slot-1 bus.
// The card must already be in Main Data mode (see card_init.h).
#pragma once

#include <nds/ndstypes.h>
#include <nds/disc_io.h>

bool ace3dsReadSectors(u32 sector, u32 count, void* buffer);

// Diagnostics: one command answered by a single 4-byte word (B0 status, B8 chip ID...),
// and how many B9 polls the last sector read took.
u32 ace3dsCommand4(u8 command, u32 param);
extern u32 ace3dsLastPolls;
extern u32 ace3dsLastAnswer; // the last B9 answer: 1 busy, 0 ready

// One raw 512-byte sector write. Only the save write-back uses it, on sectors it has checked.
bool ace3dsWriteSector(u32 sector, const u32* data);

// One 8-byte command in Main Data mode with the given port 40001A4h value (block size included);
// reads up to `words` answer words into dest (dest may be NULL). Returns how many words came.
u32 ace3dsTransfer(const u8* command, u32 romctrl, u32* dest, u32 words);

#define ACE3DS_ROMCTRL_NODATA 0xA0586000
#define ACE3DS_ROMCTRL_WORD   0xA7586000
#define ACE3DS_ROMCTRL_SECTOR 0xA1586000

// Read-only on purpose: the filesystem layer never writes to the flashcard's microSD.
extern const DISC_INTERFACE ace3dsReadOnlyInterface;

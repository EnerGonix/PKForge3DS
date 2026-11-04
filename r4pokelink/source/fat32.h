// Finds where a file's bytes sit on the R4's FAT32 microSD, read-only, so a save of the same size
// can be rewritten in place sector by sector without touching the FAT or the directory.
#pragma once

#include <nds/ndstypes.h>

typedef struct
{
    u32 partitionStart;
    u32 fatStart;
    u32 dataStart;
    u32 sectorsPerCluster;
    u32 rootCluster;
} Fat32;

// False when the card holds no FAT32 volume this code understands.
bool fat32Open(Fat32* fs);
// path like "roms/nds/Pokemon - White Version.sav" (case-insensitive, long names allowed).
bool fat32Find(const Fat32* fs, const char* path, u32* firstCluster, u32* size);
// The sectors holding the file, one per 512 bytes; returns how many, or -1 on a broken chain.
int fat32FileSectors(const Fat32* fs, u32 firstCluster, u32 size, u32* sectors, int max);

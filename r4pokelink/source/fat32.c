#include "fat32.h"
#include "ace3ds.h"

#include <ctype.h>
#include <string.h>

static u32 sectorBuf[128];
static u32 fatCacheSector = 0xFFFFFFFF;
static u32 fatCache[128];

static u16 le16(const u8* p) { return p[0] | (p[1] << 8); }
static u32 le32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static bool isFat32Boot(const u8* s)
{
    return s[510] == 0x55 && s[511] == 0xAA && le16(s + 11) == 512 && s[13] != 0 && le16(s + 17) == 0 &&
           memcmp(s + 82, "FAT32", 5) == 0;
}

bool fat32Open(Fat32* fs)
{
    if (!ace3dsReadSectors(0, 1, sectorBuf))
    {
        return false;
    }
    const u8* s = (const u8*)sectorBuf;
    u32 start   = 0;
    if (!isFat32Boot(s))
    {
        // An MBR: the first partition.
        if (s[510] != 0x55 || s[511] != 0xAA)
        {
            return false;
        }
        start = le32(s + 0x1C6);
        if (!ace3dsReadSectors(start, 1, sectorBuf) || !isFat32Boot(s))
        {
            return false;
        }
    }
    fs->partitionStart    = start;
    fs->sectorsPerCluster = s[13];
    fs->fatStart          = start + le16(s + 14);
    fs->dataStart         = fs->fatStart + s[16] * le32(s + 36);
    fs->rootCluster       = le32(s + 44);
    fatCacheSector        = 0xFFFFFFFF;
    return true;
}

static u32 clusterSector(const Fat32* fs, u32 cluster) { return fs->dataStart + (cluster - 2) * fs->sectorsPerCluster; }

// 0 on a read error; >= 0x0FFFFFF8 at the end of a chain.
static u32 nextCluster(const Fat32* fs, u32 cluster)
{
    u32 sector = fs->fatStart + (cluster * 4) / 512;
    if (sector != fatCacheSector)
    {
        if (!ace3dsReadSectors(sector, 1, fatCache))
        {
            fatCacheSector = 0xFFFFFFFF;
            return 0;
        }
        fatCacheSector = sector;
    }
    return fatCache[(cluster * 4 % 512) / 4] & 0x0FFFFFFF;
}

static void utf8Append(char* out, int* len, int max, u32 c)
{
    if (c < 0x80 && *len + 1 < max)
    {
        out[(*len)++] = (char)c;
    }
    else if (c < 0x800 && *len + 2 < max)
    {
        out[(*len)++] = (char)(0xC0 | (c >> 6));
        out[(*len)++] = (char)(0x80 | (c & 0x3F));
    }
    else if (*len + 3 < max)
    {
        out[(*len)++] = (char)(0xE0 | (c >> 12));
        out[(*len)++] = (char)(0x80 | ((c >> 6) & 0x3F));
        out[(*len)++] = (char)(0x80 | (c & 0x3F));
    }
    out[*len] = 0;
}

static bool sameName(const char* a, const char* b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
        {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

// Looks for one name in a directory; fills the entry's first cluster and size.
static bool findInDir(const Fat32* fs, u32 dirCluster, const char* want, u32* cluster, u32* size, bool* isDir)
{
    u16 lfn[260];
    int lfnParts = 0;
    memset(lfn, 0, sizeof(lfn));
    for (u32 c = dirCluster; c >= 2 && c < 0x0FFFFFF8; c = nextCluster(fs, c))
    {
        for (u32 si = 0; si < fs->sectorsPerCluster; si++)
        {
            if (!ace3dsReadSectors(clusterSector(fs, c) + si, 1, sectorBuf))
            {
                return false;
            }
            const u8* e = (const u8*)sectorBuf;
            for (int i = 0; i < 16; i++, e += 32)
            {
                if (e[0] == 0x00)
                {
                    return false; // end of the directory
                }
                if (e[0] == 0xE5)
                {
                    lfnParts = 0;
                    continue;
                }
                if (e[11] == 0x0F)
                {
                    // A long-name piece: 13 UTF-16 units at 1, 14 and 28.
                    int ord = (e[0] & 0x1F) - 1;
                    if (ord >= 0 && ord < 20)
                    {
                        static const u8 at[13] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};
                        for (int k = 0; k < 13; k++)
                        {
                            lfn[ord * 13 + k] = le16(e + at[k]);
                        }
                        lfnParts++;
                    }
                    continue;
                }
                if (e[11] & 0x08)
                {
                    lfnParts = 0; // volume label
                    continue;
                }
                char name[256];
                int len = 0;
                name[0] = 0;
                if (lfnParts)
                {
                    for (int k = 0; k < 260 && lfn[k] && lfn[k] != 0xFFFF; k++)
                    {
                        utf8Append(name, &len, sizeof(name), lfn[k]);
                    }
                }
                else
                {
                    for (int k = 0; k < 8 && e[k] != ' '; k++)
                    {
                        name[len++] = e[k];
                    }
                    if (e[8] != ' ')
                    {
                        name[len++] = '.';
                        for (int k = 8; k < 11 && e[k] != ' '; k++)
                        {
                            name[len++] = e[k];
                        }
                    }
                    name[len] = 0;
                }
                lfnParts = 0;
                memset(lfn, 0, sizeof(lfn));
                if (sameName(name, want))
                {
                    *cluster = (le16(e + 20) << 16) | le16(e + 26);
                    *size    = le32(e + 28);
                    *isDir   = (e[11] & 0x10) != 0;
                    return true;
                }
            }
        }
    }
    return false;
}

bool fat32Find(const Fat32* fs, const char* path, u32* firstCluster, u32* size)
{
    u32 cluster = fs->rootCluster;
    char part[256];
    const char* p = path;
    while (*p)
    {
        const char* slash = strchr(p, '/');
        int len           = slash ? slash - p : (int)strlen(p);
        if (len <= 0 || len >= (int)sizeof(part))
        {
            return false;
        }
        memcpy(part, p, len);
        part[len] = 0;
        u32 next, bytes;
        bool dir;
        if (!findInDir(fs, cluster, part, &next, &bytes, &dir))
        {
            return false;
        }
        if (!slash)
        {
            if (dir)
            {
                return false;
            }
            *firstCluster = next;
            *size         = bytes;
            return true;
        }
        if (!dir)
        {
            return false;
        }
        cluster = next ? next : fs->rootCluster;
        p       = slash + 1;
    }
    return false;
}

int fat32FileSectors(const Fat32* fs, u32 firstCluster, u32 size, u32* sectors, int max)
{
    int needed = (size + 511) / 512;
    if (needed > max)
    {
        return -1;
    }
    int n = 0;
    for (u32 c = firstCluster; n < needed; c = nextCluster(fs, c))
    {
        if (c < 2 || c >= 0x0FFFFFF8)
        {
            return -1;
        }
        u32 base = clusterSector(fs, c);
        for (u32 si = 0; si < fs->sectorsPerCluster && n < needed; si++)
        {
            sectors[n++] = base + si;
        }
    }
    return n;
}

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

    Modified for R4 PokéLink: NTR (DS) cards only, as taken from GodMode9i.
*/

#include <string.h>
#include "encryption.h"
#include "key1.h"

#define KEYSIZE 0x1048

static u32 keycode[3];
static u32 keybuf[KEYSIZE / sizeof(u32)];

void crypt_64bit_up(u32* ptr)
{
    u32 x = ptr[1];
    u32 y = ptr[0];
    u32 z;

    for (int i = 0; i < 0x10; i++)
    {
        z = keybuf[i] ^ x;
        x = keybuf[0x012 + ((z >> 24) & 0xff)];
        x = keybuf[0x112 + ((z >> 16) & 0xff)] + x;
        x = keybuf[0x212 + ((z >> 8) & 0xff)] ^ x;
        x = keybuf[0x312 + ((z >> 0) & 0xff)] + x;
        x = y ^ x;
        y = z;
    }

    ptr[0] = x ^ keybuf[0x10];
    ptr[1] = y ^ keybuf[0x11];
}

void crypt_64bit_down(u32* ptr)
{
    u32 x = ptr[1];
    u32 y = ptr[0];
    u32 z;

    for (int i = 0x11; i > 0x01; i--)
    {
        z = keybuf[i] ^ x;
        x = keybuf[0x012 + ((z >> 24) & 0xff)];
        x = keybuf[0x112 + ((z >> 16) & 0xff)] + x;
        x = keybuf[0x212 + ((z >> 8) & 0xff)] ^ x;
        x = keybuf[0x312 + ((z >> 0) & 0xff)] + x;
        x = y ^ x;
        y = z;
    }

    ptr[0] = x ^ keybuf[0x01];
    ptr[1] = y ^ keybuf[0x00];
}

static u32 bswap_32bit(u32 in)
{
    return ((in & 0xff) << 24) | (((in >> 8) & 0xff) << 16) | (((in >> 16) & 0xff) << 8) | ((in >> 24) & 0xff);
}

static void apply_keycode(u32 modulo)
{
    u32 scratch[2] = {0, 0};
    modulo = modulo / sizeof(*keycode);

    crypt_64bit_up(&keycode[1]);
    crypt_64bit_up(&keycode[0]);

    for (int i = 0; i < 0x12; i++)
    {
        keybuf[i] = keybuf[i] ^ bswap_32bit(keycode[i % modulo]);
    }
    for (int i = 0; i < 0x412; i += 2)
    {
        crypt_64bit_up(scratch);
        keybuf[i]     = scratch[1];
        keybuf[i + 1] = scratch[0];
    }
}

void init_keycode(u32 idcode, u32 level, u32 modulo)
{
    memcpy(keybuf, gEncrData, KEYSIZE);

    keycode[0] = idcode;
    keycode[1] = idcode / 2;
    keycode[2] = idcode * 2;

    if (level >= 1)
    {
        apply_keycode(modulo); // first apply (always)
    }
    if (level >= 2)
    {
        apply_keycode(modulo); // second apply (optional)
    }
    keycode[1] = keycode[1] * 2;
    keycode[2] = keycode[2] / 2;
    if (level >= 3)
    {
        apply_keycode(modulo); // third apply (optional)
    }
}

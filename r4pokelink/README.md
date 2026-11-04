# R4 PokéLink

A small DSi-mode tool for the 3DS that moves Pokémon saves between the microSD of an R4 in the
cartridge slot and the 3DS SD card (`sd:/3ds/PKForge/saves/R4/<nds|gba|gb|gbc>/`), where PKForge for
3DS edits them. No PC needed.

Two tabs, one checkbox per save:

- **R4 > 3DS** copies the chosen saves to the 3DS SD card. A differing copy already there is kept as
  `.old1`, `.old2`...
- **3DS > R4** writes chosen 3DS copies back over the R4's save of the same size. The R4 is never
  written any other way: the save's sectors are found on the FAT32 volume, checked against the file,
  backed up to `sd:/3ds/PKForge/r4backups/`, rewritten in place (FAT and directory untouched) and read
  back; on a mismatch the old save is put back.

Status: copying, writing back and deleting saves work on an r4isdhc.hk 2024 card, started from
PKForge's R4 button. If the R4's microSD cannot be read, R4 PokéLink writes a diagnosis to
`sd:/3ds/PKForge/r4pokelink.log` and changes nothing.

## Cards

Ace3DS family, as Pico Loader names it: Ace3DS+, Gateway 3DS (blue), r4isdhc.com.cn carts,
r4isdhc.hk carts 2020+ and their derivatives.

## Use

1. With the R4 in the cartridge slot, press the **R4** button in PKForge. PKForge carries R4 PokéLink and
   installs it as a hidden DSiWare title (TWiLight Menu++ is the fallback), so it runs in DSi mode.
2. It lists the Pokémon saves in `/roms/nds`, `/roms/gba`, `/roms/gb` and `/roms/gbc` on the R4.
   A (or a tap) marks a save, Y marks all or none, L/R switch tabs, X copies the marked ones, SELECT
   deletes (with a backup first). Saves the game never wrote (all `FF` or `00`) are left out.
3. HOME, then PKForge again: the saves are under "R4".

Everything R4 PokéLink does is logged in `sd:/3ds/PKForge/r4pokelink.log`.

## How it works

R4 PokéLink powers Slot-1 off and on, runs the DS card handshake (header, KEY1, KEY2, Main Data mode)
the way the DS firmware does, then reads 512-byte sectors of the R4's microSD with the Ace3DS
commands `B9` (request, poll until ready) and `BA` (read), and mounts its FAT read-only.

## Build

devkitPro `nds-dev` (libnds 2 with calico): `make`.

## Licences

- R4 PokéLink is GPL-3.0-or-later (`LICENSE`).
- `source/font.h` is generated from NDS12 by Caveras (CC BY-NC-SA 3.0) by `tools/make_font.py`.
- `card_init.c`, `encryption.c`, `encryption.h` and `key1.h` come from GodMode9i
  (DS-Homebrew, GPL-3.0), which took them from NitroHax by Michael "Chishm" Chisholm (GPL-3.0);
  they were trimmed to NTR cards for libnds 2.
- `ace3ds.c` is the Ace3DS microSD read code of Pico Loader (LNH team, Zlib licence), rewritten in
  C with a timeout and a read-only disc interface. Its notice is kept in the file:

  > Copyright (c) 2025 LNH team. This software is provided 'as-is', without any express or implied
  > warranty. In no event will the authors be held liable for any damages arising from the use of
  > this software. Permission is granted to anyone to use this software for any purpose, including
  > commercial applications, and to alter it and redistribute it freely, subject to the following
  > restrictions: 1. The origin of this software must not be misrepresented; you must not claim that
  > you wrote the original software. If you use this software in a product, an acknowledgment in the
  > product documentation would be appreciated but is not required. 2. Altered source versions must
  > be plainly marked as such, and must not be misrepresented as being the original software. 3. This
  > notice may not be removed or altered from any source distribution.

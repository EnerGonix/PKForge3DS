<p align="center">
  <img src="docs/assets/banner.png" alt="PKForge for Nintendo 3DS" width="256">
</p>

<h1 align="center">PKForge for Nintendo 3DS</h1>

<p align="center">
  <a href="https://github.com/EnerGonix/PKForge3DS/releases/latest"><img src="https://img.shields.io/github/v/release/EnerGonix/PKForge3DS?label=release" alt="Latest release"></a>
  <a href="https://github.com/EnerGonix/PKForge3DS/releases"><img src="https://img.shields.io/github/downloads/EnerGonix/PKForge3DS/total" alt="Downloads"></a>
  <img src="https://img.shields.io/badge/platform-Nintendo%203DS-red" alt="Platform: Nintendo 3DS">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-GPLv3-blue" alt="License: GPLv3"></a>
</p>

<p align="center">
  <a href="#-download-straight-to-your-3ds">Download</a> ·
  <a href="#-r4-pokélink-the-saves-on-your-r4-card-edited-on-the-3ds">R4 PokéLink</a> ·
  <a href="#screenshots">Screenshots</a> ·
  <a href="#what-it-does">Features</a> ·
  <a href="CHANGELOG.md">Changelog</a> ·
  <a href="#build">Build</a> ·
  <a href="#credits-and-licences">Credits</a>
</p>

A native 3DS homebrew port of [PKForge](https://github.com/sofianeelhor/PKForge), the Pokémon save
editor and Bank for Android. PKForge itself is C# on .NET MAUI and PKHeX.Core, which cannot run on a
3DS, so this port rebuilds its screens in C++ with citro2d and uses
[PKSM-Core](https://github.com/FlagBrew/PKSM-Core) (the engine of PKSM, which PKForge's look comes
from) for reading and writing saves.

**Games covered: Generation 1 to 7, from Red/Blue/Green up to Ultra Sun and Ultra Moon** (Game Boy, Game
Boy Advance, DS and 3DS). Let's Go and the Nintendo Switch games are out of scope.

It opens on a title screen (PKForge's logo over the Android splash colour; touch or A to go on).

## 📲 Download straight to your 3DS

<img src="docs/assets/download-qr.png" alt="QR code: latest PKForge.cia" width="320" align="right">

Scan the QR code on the 3DS and it installs the newest `PKForge.cia`, no PC needed:

- **FBI**: Remote Install › Scan QR Code
- **3dsDL** (or any QR CIA installer): scan it the same way

It always points to the latest release:
`https://github.com/EnerGonix/PKForge3DS/releases/latest/download/PKForge.cia`

<br clear="right">

## ⭐ R4 PokéLink: the saves on your R4 card, edited on the 3DS

New in this port. If you play DS, GBA or Game Boy Pokémon games on an **R4 (Ace3DS) flashcart**, their
saves live on the R4's own microSD, out of reach of every 3DS save editor. **R4 PokéLink**
([`r4pokelink/`](r4pokelink)) talks to the R4 straight from the cartridge slot: PKForge's **R4 button**
starts it, it copies the R4's Pokémon saves to the 3DS SD card for PKForge to edit, and writes the edited
saves back to the R4. No PC, no taking the microSD out. Every write to the R4 backs the save up first and
reads it back to check it. Developed and tested on an r4isdhc.hk 2024 card.

## Screenshots

Top and bottom screens, captured in Azahar.

| Home | Boxes | Summary |
|:---:|:---:|:---:|
| ![Home](docs/screenshots/home.png) | ![Boxes](docs/screenshots/boxes.png) | ![Summary](docs/screenshots/summary-stats.png) |
| **Editor** | **Bank** | **RNG / IVs** |
| ![Editor](docs/screenshots/editor.png) | ![Bank](docs/screenshots/bank.png) | ![RNG](docs/screenshots/rng.png) |
| **Dex Autopilot** | **Poképark** | **Color themes** |
| ![Dex Autopilot](docs/screenshots/dex-autopilot.png) | ![Poképark](docs/screenshots/pokepark.png) | ![Fire theme](docs/screenshots/theme-fire.png) |

The top screen is PKForge's second-screen inspector; the touch screen holds the boxes, editor and menus.

## What it does

- Opens the saves of the 3DS games themselves: the cartridge in the slot and installed copies of
  X, Y, Omega Ruby, Alpha Sapphire, Sun, Moon, Ultra Sun and Ultra Moon, and the Virtual Console
  Red, Blue, Yellow, Gold, Silver and Crystal. Writing commits the save and clears its secure value,
  as PKSM and Checkpoint do, after a restore point of what the game held.
- Finds saves anywhere on the SD card (emulators, TWiLight Menu++, Checkpoint and JKSV dumps,
  `sdmc:/3ds/PKForge/saves`), Generation I to VII, as cartridge cards.
- Boxes and party: grab and drop with A or by dragging with the stylus, L/R for boxes.
- The Bank, in PKForge's own layout (`index.json` plus one `.bin` per Pokémon) in
  `sdmc:/3ds/PKForge/bank`. SELECT switches between the save and the Bank. Moves between them become
  final when the save is written: leaving without writing puts deposits back, and withdrawn Pokémon
  stay in the Bank until then.
- Editor: species, form, nickname, level, nature, ability, item, gender, shiny, ball, friendship,
  language, egg, Pokérus, IVs, EVs, Hyper Training, moves, PP Ups, relearn moves, trainer and origin.
- Searchable pickers with Pokédex entries and move details on the top screen.
- Bag editor, trainer card, box names.
- Showdown sets: export one Pokémon or a whole box to `sdmc:/3ds/PKForge/showdown`, import the sets in
  `showdown/import.txt` into the empty slots (species, moves and items the game lacks are skipped).
- Box tools: sort a box by Pokédex number, clone a Pokémon; complete the Pokédex (seen and caught).
- Living Dex (START): every species and where you have it, over the Bank and every save found;
  search "missing" for the gaps.
- New egg (X on an empty slot): an unhatched egg of any species the game has, built as the Day Care
  makes one (egg location, level, level-up moves, egg cycles from PokeAPI's `romfs/hatch.bin`).
- World & events (START), on the save's bytes at PKHeX's offsets: Ruby/Sapphire/Emerald clock repair
  (reset, berry fix), the Gen 3 roaming legendary (roam again, perfect IVs, shiny), HeartGold/SoulSilver
  Apricorn Box and Pokéwalker (steps, watts, courses), Diamond/Pearl/Platinum honey trees, and key item
  events (Eon/Aurora/Mystic Tickets, Old Sea Map, Member Card, Oak's Letter, Azure Flute, Secret Key,
  Enigma Stone, Crystal's GS Ball, Black/White's Liberty Pass, ORAS's Eon Ticket) set as the real
  distributions do: the item plus their flags and vars. Also Sun/Moon/Ultra Poké Beans and, for
  Red/Blue/Yellow, re-arming the one-time encounters (Mewtwo, the birds, Power Plant, fossils, gifts).
- QR codes in the Android PKForge's PKF1 format, so the two apps read each other's: X > Show QR code on
  a Pokémon; START > Receive by QR (the outer camera) or from a QR picture (a PNG/JPEG/BMP screenshot or
  photo in `sdmc:/3ds/PKForge/qr`), into the save's first empty slot or the Bank.
- Poképark (START): up to 24 of the Bank's Pokémon (one per species) living on the Android PKForge's park
  map, wandering its land, with the light of the hour; each has a pace, a range and a favourite gesture
  from its species, they meet and react to each other, sleep at night (Z), rain falls some days and
  fireflies come out on dry nights, night owls sit at a campfire; A throws a ball the awake ones chase,
  now and then two of them race, and SELECT shows the park's journal (`sdmc:/3ds/PKForge/park-journal.txt`);
  each has fun and energy (tired ones nap at Blissey's Daycare by day) and says a short line now and then;
  Seek starts hide and seek (the hidden only show when the view comes close, A finds them);
  X plays with the chosen one, Y calls it over; L/R picks one, the
  D-pad looks around, the bottom-screen map jumps the view and names the area. `tools/park.py` cuts the map into 256x256 tiles and takes its grid.
- Sound (START > Sound): music by place (menus, park by day, park by night), effects and a cry for each
  Pokémon. Your own tracks go in `sdmc:/3ds/PKForge/music/{title,menu,park-day,park-night}` (MP3, OGG, WAV) and
  your own cries in `sdmc:/3ds/PKForge/cries/<national number>.wav|ogg|mp3`; without them PKForge plays the
  MP3s in `romfs/music/{title,menu,park-day,park-night}` in turn (the release build: Kevin MacLeod's tracks from
  `music-cc/`, see Build) (converted once, at lowest priority, to 32 kHz stereo PCM in `sdmc:/3ds/PKForge/cache`,
  about what the 3DS's DSP plays out, so playing them costs no frames) and a voice made up for each species.
  Menu sounds are soft and quiet. Needs Luma's DSP firmware dump.
- HaX mode (START): the editor offers every species, move, item and ball the generation can store,
  the 510 EV total goes, and Pokémon move into games that lack them. Off by default (`hax.txt`).
- Hex editor (X on a Pokémon): its decrypted bytes, edited in hex, kept with a fresh checksum.
- Mystery Gift (START): PKHeX's database of every real distribution for Generations 4 to 7, searchable, sent into the save's gift inbox for the game to hand
  over. `tools/mysterygift.py` packs PKHeX's `Resources/legality/mgdb/*.pkl` into `romfs/mg`.
- Export and import of `.pk1`–`.pk9`, `.pb7`, `.pa8`, `.pa9` files (`sdmc:/3ds/PKForge/export`
  and `import`), readable by PKForge, PKHeX and PKSM.
- Every write keeps a restore point first: the 20 newest per save, in `sdmc:/3ds/PKForge/backups`.
- SELECT on a card deletes that save file, after a restore point. A game's own save data (cartridge or
  installed title) is never deleted. In R4 PokéLink, SELECT clears the R4's save (FF, with a backup in
  `r4backups`) or moves the 3DS copy aside as `.oldN`.
- The R4 button starts R4 PokéLink (`r4pokelink/`, carried in the RomFS), which copies saves between
  an R4 (Ace3DS) card and the SD card in DSi mode. It needs TWiLight Menu++: PKForge puts
  `R4PokeLink.nds` in `sdmc:/3ds/PKForge`, keeps TWiLight's `settings.ini` aside as
  `twilight-settings.bak`, sets it to auto-run R4 PokéLink once and restarts into TWiLight.
  R4 PokéLink copies the settings back as it starts; PKForge does it too if the launch never got
  that far. Afterwards, HOME and PKForge again: the copied saves are under `saves/R4`.

- DS cartridges (Diamond to Black 2) in the 3DS slot: the save chip is read and written over SPI, with a
  restore point first and a read-back check after.
- START > Legalize this box / the party, offline: moves its species line cannot learn in that game (or
  the games it can come from) go, empty slots take its latest level-up moves; ability, item and ball the
  game has; IV, EV and PP limits; levels. The learnsets are `romfs/learnsets.bin`, packed from PokeAPI's
  csv files by `tools/learnsets.py`. Pokémon from another game only get in when the game has their
  species, form, moves, ability, item and ball.
- Legalize with PKHeX (a Pokémon's X menu): full legality through FlagBrew's
  [local-gpss](https://github.com/FlagBrew/local-gpss) on a computer on the same Wi-Fi, as PKSM does
  since FlagBrew's own server closed. Its address goes in START > Legality server
  (`sdmc:/3ds/PKForge/legality-server.txt`).

Not in this port yet (they need parts of PKHeX ported to C++): Day Care editing, Generate and the encounter
browser, Colosseum/XD saves, ROM hacks. Let's Go and the Switch games are out of scope.

## Install

Install `PKForge.cia` with FBI; PKForge gets its own icon on the HOME menu (title 00040000-0E4F0B00).
To update, install the new CIA over it: everything PKForge keeps is in `sdmc:/3ds/PKForge`, which stays.

`build.sh` makes the CIA with `tools/ctr/cia.sh` (makerom from Project_CTR, bannertool from
Epicpkmn11). `PKForge.3dsx` is still built, for the tests in Azahar (`tools/run-autotest.ps1`).

The HOME Menu banner is a 3D model the HOME Menu turns and shows in stereoscopic 3D: PKForge's logo with
its cyan lines in relief and a plain blue back. `tools/ctr/make_banner3d.py` writes it as glTF and
[pycgfx](https://github.com/skyfloogle/pycgfx) (by skyfloogle) converts it to `tools/ctr/banner.cgfx`, which
the build uses as it is; the banner sound (a device clacking open, then a ball opening) is synthesized by
`tools/ctr/make_art.py`. Both are original.

## Build

devkitPro with `3ds-dev` (devkitARM, libctru, citro2d) and `nds-dev` (for R4 PokéLink), MSYS2 with
Python 3, and the PKForge source tree for its art and fonts. `build.sh` builds R4 PokéLink first and
copies it into `romfs/r4`.

```bash
git clone --recursive https://github.com/FlagBrew/PKSM-Core external/PKSM-Core
PKFORGE_SRC=/c/path/to/PKForge bash build.sh
```

The music is not generated: put MP3s in `romfs/music/{title,menu,park-day,park-night}`. The release uses
Kevin MacLeod's CC BY 4.0 tracks kept in `music-cc/`: `carefree.mp3` for title and menu, `wholesome.mp3`
for park-day, `dreamy_flashback.mp3` for park-night. `tools/ctr/makerom.exe` and `bannertool.exe` are
not in the repository: get them from Project_CTR and Epicpkmn11's bannertool releases.

`tools/assets.py` converts PKForge's resources once into `romfs/` (delete `romfs/.assets-done` to redo
it). `tools/run-autotest.ps1` runs a script from `tests/` in Azahar and saves screenshots; scripts can
only write saves in `sdmc:/3ds/PKForge/saves`. `tests/check_gen3.py` checks a written Gen 3 save
independently (section and Pokémon checksums).

## Credits and licences

- **En3rGonix** ([github.com/EnerGonix](https://github.com/EnerGonix)): PKForge for Nintendo 3DS and R4 PokéLink.
  Source: [github.com/EnerGonix/PKForge3DS](https://github.com/EnerGonix/PKForge3DS).
- PKForge by sofianeelhor, GPLv3 or later. This port is GPLv3 or later as well (see `LICENSE`).
- PKSM-Core by FlagBrew (Bernardo Giordano, Admiral Fish, piepie62 and contributors), GPLv3 with
  Additional Terms 7.b and 7.c, kept unmodified in `external/PKSM-Core`; memecrypto by SciresM;
  pcg-cpp by Melissa O'Neill.
- PKHeX by Kaphotics and contributors: the research behind PKSM-Core.
- AlchemyBats: the tileset the Poképark's map is built on (from the Android PKForge's resources).
- Audio decoding: [dr_mp3 and dr_wav](https://github.com/mackron/dr_libs) by David Reid (public domain /
  MIT-0) and [stb_vorbis](https://github.com/nothings/stb) by Sean Barrett (public domain), in `external/audio`.
  The effects and made-up cries are original to this port.
- Built-in music: "Carefree" (menus), "Wholesome" (park by day) and "Dreamy Flashback" (park by night) by
  Kevin MacLeod (incompetech.com), licensed under Creative Commons: By Attribution 4.0
  (http://creativecommons.org/licenses/by/4.0/); also in `romfs/licenses/music.txt`.
- QR codes: [QR Code generator](https://github.com/nayuki/QR-Code-generator) by Project Nayuki (MIT),
  [quirc](https://github.com/dlbeer/quirc) by Daniel Beer (ISC), and
  [stb_image](https://github.com/nothings/stb) by Sean Barrett (public domain), in `external/`.
- Updates download with [curl](https://curl.se) (curl licence, Copyright Daniel Stenberg and contributors),
  [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) (Apache-2.0) and [zlib](https://zlib.net) (zlib licence),
  the devkitPro portlibs builds; their licence texts ship in the app's `romfs/licenses`.
- DS cartridge saves over SPI: `source/spi.cpp` / `spi.hpp` from Checkpoint (Bernardo Giordano,
  FlagBrew) and TWLSaveTool (TuxSH), GPLv3 with Additional Terms 7.b and 7.c, marked as modified
  (logging removed).
- PKSM's pixel chrome (GPLv3) and spritedmistery's PKForge logo, look and color schemes. PKForge's author
  and the artist of the logo and button images approved their use in this unofficial port
  ([sofianeelhor/PKForge#73](https://github.com/sofianeelhor/PKForge/issues/73)).
- Pokémon Showdown / Smogon sprite artists: front sprites and box icons.
- PokeAPI (BSD-3): move, ability and Pokédex facts.
- game-icons.net (CC-BY 3.0) icons; SteamGridDB community cartridge art.
- NDS12 font by Caveras (CC BY-NC-SA 3.0), shipped with its licence and readme in `romfs/licenses`.

Pokémon and Pokémon character names are trademarks of Nintendo, Creatures Inc. and GAME FREAK inc.
Not affiliated with Nintendo, Game Freak or The Pokémon Company.

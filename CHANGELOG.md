# Changelog

All notable changes to PKForge for Nintendo 3DS. Versions follow [Semantic Versioning](https://semver.org);
release tags are `3ds-vX.Y.Z` (the plain `vX.Y.Z` tags in this fork belong to the Android PKForge).

## [1.2.0] - 2026-11-11

### Added
- **Catch in the wild** (X on an empty box slot): every wild encounter of the open game, Generations 1 to 7,
  from PKHeX's tables (place, kind, levels, time of day in Crystal), plus its **static and gift encounters**
  (legendaries, starters, fossils, gifts), and the other games where the species is wild. Pick one and the
  Pokémon is made the way the game makes it, so PKHeX accepts it: Method H in Generation 3 (with the Rock
  Smash check), Methods J/K in Generation 4 (ground tile, fishing bite check), Method 1 for Gen 3/4 statics,
  the Monochrome PID rules in Generation 5 (Hidden Grottos with their hidden ability), perfect IVs, locks,
  relearn moves, region and memory data in Generations 6 and 7, and the **in-game trades** with their fixed
  PID, IVs, contest stats, trainer and nickname in the save's language. Also the **Safari Zone** and Great
  Marsh (Safari Ball; R/S/E's extra RNG call; HG/SS's 0-9 slot roll, fixed level and 31-IV re-roll), the
  **Bug Catching Contest** (Sport Ball), **headbutt trees** (Gold/Silver/Crystal: only the trees your trainer ID
  shakes) and Diamond/Pearl/Platinum **honey trees** (honey level roll; Munchlax only on your ID's trees).
  Also **Unown** (Gen 3 Tanoby letters from the PID, Solaceon's single-letter rooms, the Ruins of Alph
  entrance), **Mt. Coronet's Feebas** (the tile check call), the X/Y **Friend Safari** (two perfect IVs, hidden
  ability possible) and **Vivillon's pattern** from the save's country and region (PKHeX's table, via
  `tools/vivillon.py`). Every encounter kind of Generations 1 to 7 can now be caught.
- **Restore points** in the save's START menu: put back the save as it was before an earlier write (what it
  holds now is kept as a new restore point first).
- **Undo changes**: back to the save as it was opened or last written, Bank moves included.
- **All boxes**: every box of the save or the Bank at a glance (how full, who is in it); a pick opens it.
- **Day Care in Gold, Silver and Crystal** (from PKHeX's layout; not tested on a real save yet).
- **Editor, like the original's**: ribbons (give or clear each, or all), box markings (Gen 7 blue/pink),
  contest stats and sheen, handler friendship, affection and trainer memories (Gen 6/7), met date, egg
  location and date, and **Suggest legal fixes** (PKForge's rules on a copy, every change listed before it
  applies).
- **Pokédex editor**: every species of the game unseen, seen or caught (Generations 1 to 7).
- **Event flags editor** (World & events): every flag and work value of Generations 1 to 7 with PKHeX's
  names and named values; story flags ask first.
- **Entralink** (Black/White, Black 2/White 2): White Forest and Black City levels, the Entree Forest (who is
  there, fill it from PKHeX's legal Dream World templates, open every area, clear it), Pass Powers and the
  Funfest missions.
- **Gifts in this save** (the Mystery Gift cards it holds) and **Trainer records** (Gen 6/7), read only, as
  in the original.
- **Music menu** like the original's: play/pause, skip, what the menus and the park by day and by night play
  (every track, one track, the menus' music or silence), shuffle, autostart, and your own files in a library.
- **Bank box wallpapers** (the storage palette), kept in the same file as the Android app.
- **RetroArch compressed saves** (#RZIP) and GBA saves with chip padding or an RTC footer open and are written
  back in the same wrapper.
- **Pokémon Colosseum and XD: Gale of Darkness** saves (Dolphin .gci): boxes and party as Generation 3
  Pokémon, Shadow data kept, written back encrypted with valid checksums.
- **ROM hacks: Radical Red, Unbound and GS Chronicles**: boxes, party, the editor with each hack's own
  species, moves and items, and the Bank in the Android app's formats (PK3RR, PK3UB, PK3GSC), with moves
  between hacks that have the species. As in the original, hack Pokémon are not legality-checked.
- **Poképark, the rest of ParkLife**: wild visitors by place and time (some evolve), flyers, ghosts that fade,
  teleporters, habits by type, room made for legends, the morning call and Hoothoot's hours, the rain chorus,
  storms, Snorlax asleep on the bridge, troublemakers, Lapras's ferry and other famous moves, scenes,
  hunger and a bond kept between visits, illness and healers, "while you were away", three goals a day with a
  party, the chronicle, digging for treasure, brushing, sparring, head pats, Blissey's daycare, the Wilds
  (released Pokémon may come back to visit), the photo album and the PKForge statue's credits.
- **Collection Center**: the community's event collections (RoC's PC on GitHub), a file or a whole folder
  into the Bank.
- **HOME renders** from the original's sprite pack, fetched one by one when shown (or all at once).
- **Move a Bank box** to another place from All boxes.
- **Bags** for Colosseum/XD and the ROM hacks, the **Pokédex editor** for Colosseum's Strategy Memo and the
  Radical Red / GS Chronicles Pokédex.
- **Animated Showdown sprites** from the original's sprite pack (Settings > Sprites: built-in, HOME renders or
  animated).
- The About screen's **Bad Apple!!** easter egg (↑↑↓↓←→←→ B A), with the original's video and song, starting at
  once (the AAC song is decoded as it plays, by faad2).

### Fixed
- **Updates that stopped half-way**: a dropped or stalled connection now resumes where it stopped (HTTP
  Range), up to six attempts, instead of giving up.

### Changed
- **Faster updates**: the installer gets the download in 256 KB blocks, the socket has a bigger receive
  buffer and the download runs above the screen's priority.
- On a New 3DS, PKForge runs with the faster clock and the L2 cache.
- The title screen and About show the app's own version (v1.2.0), which updates change, instead of the
  Android PKForge version it is based on.

## [1.1.2] - 2026-11-11

### Fixed
- **In-app updates stuck at 0.0 MB on the console**: the download thread had too little stack for verifying
  the download server's RSA-4096 certificates; it now has 256 KB.

### Added
- **Reinstall** in Settings › Check for updates when PKForge is already up to date.
- `sdmc:/3ds/PKForge/update.log` records every step of a check or install, and the update screen says so
  when nothing arrives for a minute.

## [1.1.1] - 2026-11-11

### Added
- **Day Care / Nursery** (Generations 3 to 7): who is deposited, the EXP gained, whether an egg waits, and a
  withdraw into a free box slot (save START menu › Day Care).
- Bug report and feature request forms on GitHub; the README is grouped by sections with an install and
  update guide.

### Changed
- **Games up to Ultra Sun and Ultra Moon** (Generations 1 to 7): saves from Let's Go and the Nintendo
  Switch games are no longer listed or opened, their Mystery Gift cards are gone (a smaller CIA), and the
  Living Dex and Dex Autopilot count the 807 species of Ultra Sun and Ultra Moon. Bank entries from newer
  games are kept as they are.

## [1.1.0] - 2026-11-10

### Added
- **In-app updates**: Settings › Check for updates downloads the latest `PKForge.cia` from GitHub over
  verified HTTPS (curl + Mbed TLS), checks its SHA-256 against GitHub's digest, installs it and restarts.
  A quiet check runs once a day.
- **Quick guide**: a few pages on the basics, shown on the first start and from Settings › Quick guide.
- **Search this save**: find Pokémon by name across the party and every box (save START menu).
- **3D HOME Menu banner**: PKForge's logo as a real 3D model (cyan lines in relief, plain blue back),
  shown in stereoscopic 3D, with a new banner sound (a device clacking open, then a ball opening).
- **Cursor sound**: a soft tick when the D-pad or Circle Pad moves the cursor.
- **Download QR code** on the README and releases (FBI / 3dsDL).

### Fixed
- FBI and 3dsDL showed the CIA as version 0.0.0: the CIA now carries the app's version.

## [1.0.0] - 2026-11-04

First release: a native 3DS port of [PKForge](https://github.com/sofianeelhor/PKForge) by sofianeelhor,
with **R4 PokéLink** for the saves on an R4 (Ace3DS) flashcart, Bank, editor, Dex Autopilot, Poképark,
Mystery Gift database, world & event editors, QR codes, themes and more.

[1.2.0]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.2.0
[1.1.2]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.2
[1.1.1]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.1
[1.1.0]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.0
[1.0.0]: https://github.com/EnerGonix/PKForge3DS/releases/tag/v1.0.0

# Parity with the Android PKForge 3.1.1

Every screen and service of the original (`src/PKForge.App/Views`, `Services`, `PKForge.Engine`,
`PKForge.Domain`), and where it stands in the 3DS port. Scope: Generations 1 to 7 (up to Ultra Sun and Ultra
Moon) and GameCube Colosseum/XD; Let's Go and the Switch games are out of scope.

| Original | Port | Status |
|---|---|---|
| Home, save picker, game art, filters, edit game | screen_home | done |
| Box browser, party, grab/drag, multi-select, box manager, locks | screen_storage | done |
| Bank, Bank search, box overview | bank, screen_storage | done (overview = All boxes; no drag-a-box) |
| Bank wallpapers (BankWallpaperPicker) | Bank box manager > Wallpaper (the 8 palette colours, box-wallpapers.json shared with Android) | done (games' pictures: PKHeX.Drawing art not bundled) |
| Pokémon editor: main, stats, moves, trainer, Hyper Training, Pokérus | screen_editor | done |
| Awards: ribbons (AwardsEditor) | screen_editor (Ribbons) | done |
| Cosmetics: markings, contest stats, friendship, affection, memories (CosmeticsEditor) | screen_editor | done (memories as numbers) |
| Met / origin: met level, met date, egg location and date (MetOriginEditor) | screen_editor | done |
| Move details: PP Ups, relearn moves (MoveDetailsEditor) | screen_editor | done |
| Potential: ability slot, Hyper Training (PotentialEditor) | screen_editor | done (Tera = Gen 9, out of scope) |
| Stats popup (manual IV/EV) | screen_editor | done |
| Legality check, legalize, Suggest (LegalityAssistUi) | legality | partly (**preview-first Suggest to do**) |
| Generate wizard / encounter gallery | encounters (Catch in the wild), New egg, egg factory | done |
| Mystery Gift album (EventGallery) | mysterygift | done |
| Mystery Gift inbox, read-only (MysteryGiftInboxEditor) | START > Gifts in this save | done |
| Day Care (DaycareEditor) | world | done |
| Dex editor: seen/caught per species (DexEditorPage) | dexedit, START > Pokédex editor | done |
| Living dex tracker (CollectionDexPage) | livingdex | done |
| Dex Autopilot | autopilot | done |
| Event flags and work vars (EventFlagsEditor) | World & events > Event flags (Gen 1-7, PKHeX labels via tools/eventlabels.py) | done |
| Event reset, key item events, roamers, clock, Apricorns, Pokéwalker, honey trees, Poké Beans | world | done |
| Entralink (Gen 5) | World & events > Entralink (levels, Entree Forest legal fill, Pass Powers, Funfest; tools/entralink.py) | done |
| Trainer records, read-only (TrainerRecordsEditor) | START > Trainer records (Gen 6-7, as the original) | done |
| Trainer card, trainer profiles, Bag, presets | screen_trainer, screen_bag, profiles | done |
| Restore points (BackupHistoryPage), undo | saves | done |
| HaX, Hardcore, themes, Showdown, QR, hex editor | hax, themes, showdown, qr, screen_hex | done |
| Music menu: play/pause, skip, per-place music, order, autostart, library | Settings > Sound | done (library = files copied to the SD) |
| Poképark: map, residents, emotes, ball, races, campfire, journal, hide and seek, feed, photo | screen_park | done |
| Poképark ParkLife: evolution, Snorlax, trolls, visitors, powers, quirks, habits, respect, chronicle, evenings, games, daycare, talk, lore, goals, absence | - | **to do** |
| Poképark album, credits statue, Wilds (release and return), treasures | - | **to do** |
| ROM hacks: Pokémon Unbound, Radical Red (CFRU), Gold & Silver Chronicles | - | **to do** |
| ROM hack: Luminescent Platinum | - | out of scope (Switch BDSP hack) |
| Colosseum / XD (.gci) | - | **to do** |
| RetroArch save containers, SRAM padding | - | **to do** (check what R4/emulators write) |
| Collection Center (community event collections, online) | - | **to do** |
| Sprite pack downloader (online) | - | to decide (sprites are built in) |
| Second screen (Thor/Ayn dual screen) | top screen inspector | done by design (the 3DS has two screens) |
| Gen 8/9 save blocks, Move Shop, Grand Underground, Tera | - | out of scope (Switch) |
| Bad Apple!! easter egg | - | not ported: the song is not free to redistribute |
| In-app updates | updater | done |

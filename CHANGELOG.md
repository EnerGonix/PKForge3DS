# Changelog

All notable changes to PKForge for Nintendo 3DS. Versions follow [Semantic Versioning](https://semver.org);
release tags are `3ds-vX.Y.Z` (the plain `vX.Y.Z` tags in this fork belong to the Android PKForge).

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

[1.1.2]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.2
[1.1.1]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.1
[1.1.0]: https://github.com/EnerGonix/PKForge3DS/releases/tag/3ds-v1.1.0
[1.0.0]: https://github.com/EnerGonix/PKForge3DS/releases/tag/v1.0.0

#!/bin/bash
# Builds PKForge.3dsx. Run from MSYS2: bash build.sh [make args]
# Assets are converted once from the PKForge source tree (PKFORGE_SRC); delete romfs/ to redo them.
set -e
cd "$(dirname "$0")"
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:/usr/bin:$PATH
PKFORGE_SRC="${PKFORGE_SRC:-/c/Users/Administrator/Desktop/PKForge-3.1.1}"

if [ ! -f romfs/.assets-done ]; then
	/usr/bin/python3 -I tools/assets.py "$PKFORGE_SRC" .
	touch romfs/.assets-done
fi
# The HOME menu art: icon.png (for the SMDH), the banner image and sound.
/usr/bin/python3 -I tools/ctr/make_art.py .
# The title screen's logo (PKForge's own, docs/images/logo-transparent.png), at full size.
mkdir -p romfs/gfx romfs/certs
# The root certificates GitHub chains to, for the in-app updater's verified HTTPS.
cp certs/*.der romfs/certs/
tex3ds tools/ctr/title-logo.png -f rgba8 -z auto -o romfs/gfx/title.t3x
# R4 PokéLink rides inside the RomFS; PKForge puts it on the SD card before starting it.
make -C r4pokelink
mkdir -p romfs/r4
cp r4pokelink/R4PokeLink.nds romfs/r4/R4PokeLink.nds
# ...and as a DSiWare CIA, which PKForge installs as a hidden title (TWiLight is the fallback).
tools/ctr/makerom.exe -srl r4pokelink/R4PokeLink.nds -o romfs/r4/R4PokeLink.cia
make -j"$(nproc)" "$@"
# PKForge ships as a CIA only (HOME menu icon); the .3dsx stays as a build step for tests in Azahar.
bash tools/ctr/cia.sh

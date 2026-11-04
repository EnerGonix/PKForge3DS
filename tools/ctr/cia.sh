#!/bin/bash
# Packs PKForge.cia from what build.sh made (PKForge.elf, PKForge.smdh, romfs/, build/cia/banner.*).
# Run from MSYS2 after build.sh.
# makerom (Project_CTR) and bannertool (Epicpkmn11) live beside this script.
set -e
cd "$(dirname "$0")/../.."
CTR=tools/ctr
mkdir -p build/cia
$CTR/bannertool.exe makebanner -i build/cia/banner.png -a build/cia/banner.wav -o build/cia/banner.bnr
$CTR/makerom.exe -f cia -o PKForge.cia -elf PKForge.elf -rsf $CTR/pkforge.rsf -icon PKForge.smdh \
	-banner build/cia/banner.bnr -DAPP_ROMFS=romfs -target t
ls -l PKForge.cia

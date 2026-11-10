#!/bin/bash
# Packs PKForge.cia from what build.sh made (PKForge.elf, PKForge.smdh, romfs/, build/cia/banner.*).
# Run from MSYS2 after build.sh.
# makerom (Project_CTR) and bannertool (Epicpkmn11) live beside this script.
set -e
cd "$(dirname "$0")/../.."
CTR=tools/ctr
mkdir -p build/cia
# The HOME Menu banner is a 3D model (tools/ctr/banner.cgfx, from make_banner3d.py + pycgfx) that the HOME
# Menu turns and shows in stereoscopic 3D; the flat picture is the fallback without it.
if [ -f $CTR/banner.cgfx ]; then
	$CTR/bannertool.exe makebanner -ci $CTR/banner.cgfx -a build/cia/banner.wav -o build/cia/banner.bnr
else
	$CTR/bannertool.exe makebanner -i build/cia/banner.png -a build/cia/banner.wav -o build/cia/banner.bnr
fi
# The CIA's SMDH says the banner is an extended (3D) one and that 3D is allowed (flags 0x20 | 0x4 at 0x2028).
/usr/bin/python3 -I -c "
import struct, sys
d = bytearray(open('PKForge.smdh', 'rb').read())
flags = struct.unpack_from('<I', d, 0x2028)[0] | 0x24
struct.pack_into('<I', d, 0x2028, flags)
open('build/cia/PKForge.smdh', 'wb').write(d)
print('SMDH flags', hex(flags))"
$CTR/makerom.exe -f cia -o PKForge.cia -elf PKForge.elf -rsf $CTR/pkforge.rsf -icon build/cia/PKForge.smdh \
	-banner build/cia/banner.bnr -DAPP_ROMFS=romfs -target t
# The title version FBI / 3dsDL show = PORT_VERSION from source/version.hpp (makerom drops the minor number).
VER=$(sed -n 's/^#define PORT_VERSION "\([0-9.]*\)".*/\1/p' source/version.hpp)
/usr/bin/python3 -I $CTR/cia_version.py PKForge.cia "$VER"
ls -l PKForge.cia

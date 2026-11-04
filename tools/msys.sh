#!/bin/bash
# Runs a command inside MSYS2 with devkitPro on PATH: tools/msys.sh "<command>"
exec env MSYSTEM=MINGW64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -lc "export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM; export PATH=/opt/devkitpro/tools/bin:/opt/devkitpro/devkitARM/bin:/usr/bin:\$PATH; cd /c/Users/Administrator/Desktop/PKForge3DS && $1"

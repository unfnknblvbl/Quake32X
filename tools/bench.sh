#!/bin/sh
# real-hardware estimate: 3 s standing at the start, then 3 s walking forward
cd "$(dirname "$0")/.."
/opt/toolchains/sega/bin/sh-elf-nm -n build/q32x.elf > build/syms.txt
./hostbuild/sh2sim quake32x.32x build/syms.txt 7 -pad 200 420 U "$@"

#!/bin/sh
# Build hostbuild/sh2sim, the SH-2 timing model, with the Mega Drive side
# (68000 + VDP, tools/mdsim.h). The 68000 core is Musashi (Karl Stenerud;
# free for non-commercial use), taken from a PicoDrive source tree:
#   MUSASHI=/path/to/picodrive/cpu/musashi tools/build_sim.sh
# Without Musashi it builds the SH-2-only model.
set -e
cd "$(dirname "$0")/.."
MUSASHI=${MUSASHI:-/opt/src/picodrive/cpu/musashi}
mkdir -p hostbuild
if [ -f "$MUSASHI/m68k_in.c" ]; then
    M=hostbuild/musashi
    rm -rf $M && mkdir -p $M
    cp "$MUSASHI"/m68k.h "$MUSASHI"/m68kconf.h "$MUSASHI"/m68kcpu.c "$MUSASHI"/m68kcpu.h \
       "$MUSASHI"/m68k_in.c "$MUSASHI"/m68kmake.c $M/
    # our configuration: no trace emulation
    sed -i \
           -e 's/^#define M68K_EMULATE_TRACE .*/#define M68K_EMULATE_TRACE OPT_OFF/' $M/m68kconf.h
    (cd $M && cc -O1 -w -o m68kmake m68kmake.c && ./m68kmake . m68k_in.c > /dev/null)
    cc -O2 -w -DMD68K -I$M -o hostbuild/sh2sim tools/sh2sim.c $M/m68kcpu.c $M/m68kops.c -lm
    echo "sh2sim with the Mega Drive side"
else
    cc -O2 -o hostbuild/sh2sim tools/sh2sim.c -lm
    echo "sh2sim without the Mega Drive side (no Musashi at $MUSASHI)"
fi

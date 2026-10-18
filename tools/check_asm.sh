#!/bin/bash
# Checks the assembly stages against their C versions in the cycle model:
#   tools/check_asm.sh OUT SECS "CFLAGS for both" "CFLAGS for the reference"
# e.g.  tools/check_asm.sh /tmp/chk 40 "" "-DC_RASTER -DC_GEOM -DC_MODEL"
#       tools/check_asm.sh /tmp/chk 40 "-DTEST_CFG=cfg.textures=0,cfg.mesh=1" "-DC_GEOM"
# Two builds of the attract demo with a fixed game step (FIXED_TICKS), the
# game run between frames (SERIAL_FRAME) and a fixed split between the CPUs
# (FIXED_SPLIT), so that both draw the same frames whatever their speed.
# The sequences of distinct 32X frames (colour indices, -video32x) must be
# the same. KEEP=1 keeps the videos (OUT/ref.rgb, OUT/new.rgb).
Q=$(cd "$(dirname "$0")/.." && pwd); O=$1; SECS=$2; BOTH="-DSERIAL_FRAME -DFIXED_SPLIT $3"; REF=$4; mkdir -p $O
build() { D=$1; rm -rf $D; mkdir -p $D/data; cp -r $Q/Makefile $Q/src $Q/src-md $Q/tools $Q/VERSION $D/; cp $Q/data/*.json $D/data/; ln -sf $Q/data/pak0.pak $D/data/pak0.pak
  mkdir -p $D/build && cp $Q/build/*.lvl $Q/build/md_data.bin $D/build/ && touch $D/build/*.lvl $D/build/md_data.bin
  (cd $D && make -s EXTRA_CFLAGS="$2" >/dev/null 2>&1) || { echo "build failed $D"; exit 1; }
  /opt/toolchains/sega/bin/sh-elf-nm -n $D/build/q32x.elf > $D/syms.txt; }
build $O/ref "-DFIXED_TICKS=4 $BOTH $REF" & build $O/new "-DFIXED_TICKS=4 $BOTH" & wait
for b in ref new; do $Q/hostbuild/sh2sim $O/$b/quake32x.32x $O/$b/syms.txt $SECS -video32x $O/$b.rgb > $O/$b.sim 2>&1 & done; wait
python3 - $O <<'PY'
import sys, hashlib
o = sys.argv[1]; F = 320 * 224 * 3; skip = 0
def seq(p):
    out = []; last = None
    with open(p, 'rb') as f:
        while True:
            b = f.read(F)
            if len(b) < F: break
            h = hashlib.md5(b[skip:]).hexdigest()
            if h != last: out.append(h); last = h
    return out
a, b = seq(o + '/ref.rgb'), seq(o + '/new.rgb')
n = min(len(a), len(b)); bad = [i for i in range(n) if a[i] != b[i]]
print('distinct frames: ref %d, new %d; compared %d, first difference at %s' % (len(a), len(b), n, bad[0] if bad else 'none'))
PY
[ -n "$KEEP" ] || rm -f $O/ref.rgb $O/new.rgb

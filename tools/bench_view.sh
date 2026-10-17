#!/bin/sh
# Real-hardware estimates (sh2sim) for screen size and water settings from a
# fixed viewpoint: tools/bench_view.sh "x,y,z,yaw,pitch" [level] (0 = E1M1)
# Builds a benchmark ROM (in a copy of the tree) that starts at that view,
# then sets the menu with scripted presses (START, Down, Left/Right) and
# averages the frame rate over seconds 4-8 with the player standing still.
cd "$(dirname "$0")/.."
POS=$1
B=${BENCH_DIR:-${TMPDIR:-/tmp}/q32x-bench}
rm -rf $B && mkdir -p $B/data
cp -r Makefile src src-md tools $B/ && cp data/*.json $B/data/ && ln -s "$PWD/data/pak0.pak" $B/data/pak0.pak
make -s build/e1m1.lvl build/e1m2.lvl build/e1m3.lvl build/md_data.bin >/dev/null || exit 1
mkdir -p $B/build && cp build/*.lvl build/md_data.bin $B/build/ && touch $B/build/*.lvl $B/build/md_data.bin
(cd $B && make -s EXTRA_CFLAGS="-DSTART_POS=$POS -DSTART_LEVEL=${2:-0}" >/dev/null) || exit 1
/opt/toolchains/sega/bin/sh-elf-nm -n $B/build/q32x.elf > $B/syms.txt
press() {   # keys -> -pad args, one press every 12 vblanks from vblank 60
    t=60; for k in $(echo "$1" | sed 's/^S/SD/; s/./& /g'); do
        printf -- "-pad %d %d %s " $t $((t + 3)) $k; t=$((t + 12)); done
}
run() {   # label, keys
    out=$(./hostbuild/sh2sim $B/quake32x.32x $B/syms.txt 9 $(press "$2") -vlog /dev/stdout)
    fps=$(echo "$out" | awk 'NF == 3 && $1 >= 240 && $1 < 480 { s += $3; n++ } END { printf "%.1f", s / n / 10 }')
    printf "%-36s %5s fps\n" "$1" "$fps"
}
run "textured, water see-through"        "S"
run "textured, water solid"               "SDDRS"
run "flat, water see-through"             "SRS"
run "flat, water solid"                   "SRDDRS"
for n in 1 2 3 4 5; do
    keys="SDDDD"; i=0; while [ $i -lt $n ]; do keys="${keys}L"; i=$((i + 1)); done
    run "textured, screen size $((100 - 10 * n))%"  "${keys}S"
    run "flat, screen size $((100 - 10 * n))%"      "SR${keys#S}S"
done

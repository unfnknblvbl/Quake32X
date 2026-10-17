#!/bin/sh
# Real-hardware estimates for each menu setting (sh2sim): the spawn view, then
# walking down the first hall. Builds a ROM (in a copy of the tree) that
# starts in E1M1 instead of the attract demo. Menu input is scripted, one
# press every 12 vblanks (longer than a frame, so no press is lost): S
# opens/closes the pause menu, D moves down, L/R change a value. The menu
# opens on New game; the D added after the first S moves to Textures, then
# Lighting, Water, Draw distance, Screen size, ...
cd "$(dirname "$0")/.."
B=${BENCH_DIR:-${TMPDIR:-/tmp}/q32x-bench}
rm -rf $B && mkdir -p $B/data
cp -r Makefile src src-md tools $B/ && cp data/*.json $B/data/ && ln -s "$PWD/data/pak0.pak" $B/data/pak0.pak
make -s build/e1m1.lvl build/e1m2.lvl build/e1m3.lvl build/md_data.bin >/dev/null || exit 1
mkdir -p $B/build && cp build/*.lvl build/md_data.bin $B/build/ && touch $B/build/*.lvl $B/build/md_data.bin
(cd $B && make -s EXTRA_CFLAGS="-DSTART_LEVEL=0" >/dev/null) || exit 1
/opt/toolchains/sega/bin/sh-elf-nm -n $B/build/q32x.elf > $B/syms.txt
press() {   # keys -> -pad args, from vblank 60
    t=60; for k in $(echo "$1" | sed 's/^S/SD/; s/./& /g'); do
        printf -- "-pad %d %d %s " $t $((t + 3)) $k; t=$((t + 12)); done
}
run() {   # name, keys
    out=$(./hostbuild/sh2sim $B/quake32x.32x $B/syms.txt 9 $(press "$2") -pad 300 540 U -vlog /dev/stdout)
    still=$(echo "$out" | awk 'NF == 3 && $1 >= 200 && $1 < 300 { s += $3; n++ } END { printf "%.1f", s / n / 10 }')
    walk=$(echo "$out" | awk 'NF == 3 && $1 >= 420 && $1 < 540 { s += $3; n++ } END { printf "%.1f", s / n / 10 }')
    printf "%-34s spawn view %5s fps   walking %5s fps\n" "$1" "$still" "$walk"
}
run "textured + lit (default)"        "SS"
run "textured, lighting off"          "SDRS"
run "flat shaded + lit"               "SRS"
run "flat shaded, lighting off"       "SRDRS"
run "textured, draw distance 512"     "SDDDLLLLS"
run "flat, draw distance 512"         "SRDDDLLLLS"
run "textured, screen size 70%"       "SDDDDLLLS"
run "flat, screen size 70%"           "SRDDDDLLLS"
run "textured, screen size 50%"       "SDDDDLLLLLS"
run "flat, screen size 50%"           "SRDDDDLLLLLS"

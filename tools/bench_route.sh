#!/bin/sh
# Real-hardware estimate (sh2sim) averaged over the whole walkthrough route
# (data/e1m1_route.json: start, lift, courtyard, pool, channel), one row per
# menu setting. Builds a DEMO=bench ROM in a copy of the tree; settings are
# made with scripted presses before the camera starts moving.
cd "$(dirname "$0")/.."
B=${BENCH_DIR:-${TMPDIR:-/tmp}/q32x-bench}
rm -rf $B && mkdir -p $B/data
cp -r Makefile src src-md tools $B/ && cp data/*.lvl data/*.json $B/data/ && ln -s "$PWD/data/pak0.pak" $B/data/pak0.pak
touch $B/data/*.lvl
(cd $B && make -s DEMO=bench >/dev/null) || exit 1
/opt/toolchains/sega/bin/sh-elf-nm -n $B/build/q32x.elf > $B/syms.txt
press() {   # keys -> -pad args, one press every 12 vblanks from vblank 30
    t=30; for k in $(echo "$1" | sed 's/./& /g'); do
        printf -- "-pad %d %d %s " $t $((t + 3)) $k; t=$((t + 12)); done
}
run() {   # label, keys: average of the FPS counter once the menu is closed
    out=$(./hostbuild/sh2sim $B/quake32x.32x $B/syms.txt 50 $(press "$2") -vlog /dev/stdout)
    echo "$out" | awk -v l="$1" 'NF == 3 && $1 >= 300 && $1 < 2900 { s += $3; n++; if ($3 < lo || lo == 0) lo = $3; if ($3 > hi) hi = $3 }
        END { printf "%-34s average %5.1f fps   (range %4.1f-%4.1f)\n", l, s / n / 10, lo / 10, hi / 10 }'
}
run "textured + lit"                 "${PRE}SS"
run "textured, see-through water"    "${PRE}SDDRS"
run "flat shaded"                    "${PRE}SRS"
run "flat, see-through water"        "${PRE}SRDDRS"
for n in 1 2 3 4 5; do
    keys="DDDD"; i=0; while [ $i -lt $n ]; do keys="${keys}L"; i=$((i + 1)); done
    run "textured, screen size $((100 - 10 * n))%"  "S${keys}S"
    run "flat, screen size $((100 - 10 * n))%"      "SR${keys}S"
done
run "textured, 70%, see-through"     "SDDRDDLLLS"
run "flat, 70%, see-through"         "SRDDRDDLLLS"

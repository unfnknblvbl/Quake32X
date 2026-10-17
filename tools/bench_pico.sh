#!/bin/sh
# Same scenarios as tools/bench_modes.sh in PicoDrive (zero-wait-state
# timing, optimistic). Run bench_modes.sh first: this uses the ROM it builds,
# which starts in E1M1 instead of the attract demo.
cd "$(dirname "$0")/.."
B=${BENCH_DIR:-${TMPDIR:-/tmp}/q32x-bench}
[ -f $B/quake32x.32x ] || { echo "run tools/bench_modes.sh first" >&2; exit 1; }
S=${TMPDIR:-/tmp}/q32x_pico; mkdir -p $S
# the game's frame rate x 10 (_dbg_fps10, an SDRAM address)
FPS=$(awk '$3 == "_dbg_fps10" { print $1 }' $B/syms.txt)
press() {   # keys -> hold lines, one press every 10 frames from frame 100
    t=100; for k in $(echo "$1" | sed 's/^S/SD/; s/./& /g'); do
        printf "hold %d %d %s\n" $t $((t + 1)) $k; t=$((t + 10)); done
}
run() {
    { press "$2"; printf "hold 300 540 U\npeek 240 $FPS 1\npeek 540 $FPS 1\nend 540\n"; } > $S/script.txt
    out=$(./hostbuild/harness /opt/src/picodrive/picodrive_libretro.so $B/quake32x.32x $S/script.txt $S 2>/dev/null | grep "^frame")
    a=$(echo "$out" | sed -n 1p | awk '{print $4}'); b=$(echo "$out" | sed -n 2p | awk '{print $4}')
    printf "%-34s start view %5.1f fps   walking %5.1f fps\n" "$1" $(echo "ibase=16; $(echo $a | tr a-f A-F)" | bc | awk '{print $1/10}') $(echo "ibase=16; $(echo $b | tr a-f A-F)" | bc | awk '{print $1/10}')
}
run "textured + lit (default)"    "SS"
run "textured, lighting off"      "SDRS"
run "flat shaded + lit"           "SRS"
run "flat shaded, lighting off"   "SRDRS"
run "textured, draw distance 512" "SDDDLLLLS"
run "flat, draw distance 512"     "SRDDDLLLLS"

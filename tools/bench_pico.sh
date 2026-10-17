#!/bin/sh
# Same scenarios in PicoDrive (zero-wait-state timing, optimistic)
cd "$(dirname "$0")/.."
S=${TMPDIR:-/tmp}/q32x_pico; mkdir -p $S
# the game's frame rate x 10 (_dbg_fps10, an SDRAM address)
FPS=$(/opt/toolchains/sega/bin/sh-elf-nm build/q32x.elf | awk '$3 == "_dbg_fps10" { print $1 }')
run() {
    name=$1; shift
    printf "%s\n" "$@" > $S/script.txt
    printf "hold 300 540 U\npeek 240 $FPS 1\npeek 540 $FPS 1\nend 540\n" >> $S/script.txt
    out=$(./hostbuild/harness /opt/src/picodrive/picodrive_libretro.so quake32x.32x $S/script.txt $S 2>/dev/null | grep "^frame")
    a=$(echo "$out" | sed -n 1p | awk '{print $4}'); b=$(echo "$out" | sed -n 2p | awk '{print $4}')
    printf "%-34s start view %5.1f fps   walking %5.1f fps\n" "$name" $(echo "ibase=16; $(echo $a | tr a-f A-F)" | bc | awk '{print $1/10}') $(echo "ibase=16; $(echo $b | tr a-f A-F)" | bc | awk '{print $1/10}')
}
run "textured + lit (default)"    "end 1"
run "textured, lighting off"      "hold 100 101 S" "hold 110 111 D" "hold 120 121 R" "hold 130 131 S"
run "flat shaded + lit"           "hold 100 101 S" "hold 110 111 R" "hold 130 131 S"
run "flat shaded, lighting off"   "hold 100 101 S" "hold 110 111 R" "hold 120 121 D" "hold 130 131 R" "hold 140 141 S"
run "textured, draw distance 512" "hold 100 101 S" "hold 110 111 D" "hold 120 121 D" "hold 130 131 L" "hold 140 141 L" "hold 150 151 L" "hold 160 161 L" "hold 170 171 S"
run "flat, draw distance 512"     "hold 100 101 S" "hold 104 105 R" "hold 110 111 D" "hold 120 121 D" "hold 130 131 L" "hold 140 141 L" "hold 150 151 L" "hold 160 161 L" "hold 170 171 S"

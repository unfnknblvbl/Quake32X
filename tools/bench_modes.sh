#!/bin/sh
# Real-hardware estimates for each menu setting (sh2sim). Menu input is
# scripted: START opens the pause menu, D moves down, L/R change a value.
cd "$(dirname "$0")/.."
/opt/toolchains/sega/bin/sh-elf-nm -n build/q32x.elf > build/syms.txt
run() {   # name, extra -pad args...
    name=$1; shift
    out=$(./hostbuild/sh2sim quake32x.32x build/syms.txt 9 "$@" -pad 300 540 U)
    still=$(echo "$out" | grep "t=  4s" | awk '{print $4}')
    walk=$(echo "$out" | grep "t=  9s" | awk '{print $4}')
    avg=$(echo "$out" | grep "^frames" | sed 's/.*avg frame \([0-9.]*\) ms/\1/')
    printf "%-34s start view %5s fps   walking %5s fps   (avg frame %s ms)\n" "$name" "$still" "$walk" "$avg"
}
run "textured + lit (default)"
run "textured, lighting off"      -pad 100 101 S -pad 110 111 D -pad 120 121 R -pad 130 131 S
run "flat shaded + lit"           -pad 100 101 S -pad 110 111 R -pad 130 131 S
run "flat shaded, lighting off"   -pad 100 101 S -pad 110 111 R -pad 120 121 D -pad 130 131 R -pad 140 141 S
run "textured, draw distance 512" -pad 100 101 S -pad 110 111 D -pad 120 121 D -pad 130 131 L -pad 140 141 L -pad 150 151 L -pad 160 161 L -pad 170 171 S
run "flat, draw distance 512"     -pad 100 101 S -pad 104 105 R -pad 110 111 D -pad 120 121 D -pad 130 131 L -pad 140 141 L -pad 150 151 L -pad 160 161 L -pad 170 171 S

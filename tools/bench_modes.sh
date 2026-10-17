#!/bin/sh
# Real-hardware estimates for each menu setting (sh2sim): the spawn view, then
# walking down the first hall. Menu input is scripted, one press every 12
# vblanks (longer than a frame, so no press is lost): S opens/closes the pause
# menu, D moves down, L/R change a value. Menu order: Textures, Lighting,
# Water, Draw distance, Screen size, Show FPS, Movement, Resume.
cd "$(dirname "$0")/.."
/opt/toolchains/sega/bin/sh-elf-nm -n build/q32x.elf > build/syms.txt
press() {   # keys -> -pad args, from vblank 60
    t=60; for k in $(echo "$1" | sed 's/./& /g'); do
        printf -- "-pad %d %d %s " $t $((t + 3)) $k; t=$((t + 12)); done
}
run() {   # name, keys
    out=$(./hostbuild/sh2sim quake32x.32x build/syms.txt 9 $(press "$2") -pad 300 540 U -vlog /dev/stdout)
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

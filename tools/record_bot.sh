#!/bin/sh
# Record the demo bot in the SH-2 cycle model (with the Mega Drive side):
# builds the normal ROM, sets the pause menu with scripted presses, starts
# Demo, and writes OUT/raw.mkv (every displayed frame, 60.07 Hz, lossless),
# OUT/audio.raw (the PWM output, s16le stereo) and OUT/vlog.txt (per vblank:
# demo phase, FPS, player, player stats, teleport count and class).
#   tools/record_bot.sh OUT [seconds] [menu keys]
# Menu keys, one press every 12 vblanks from 1 s in (U D L R A B C S): the
# default opens the menu, turns textures off, makes water see-through, makes
# the screen two sizes smaller (80%), and starts Demo.
set -e
cd "$(dirname "$0")/.."
OUT=$1; SECS=${2:-150}
KEYS=${3:-SRDDRDDLLDDDDDDR}
mkdir -p "$OUT"
make -s >/dev/null
/opt/toolchains/sega/bin/sh-elf-nm -n build/q32x.elf > build/syms.txt
cp quake32x.32x "$OUT/quake32x.32x"
PADS=$(t=60; for k in $(echo "$KEYS" | sed 's/./& /g'); do printf -- "-pad %d %d %s " $t $((t + 3)) $k; t=$((t + 12)); done)
rm -f "$OUT/raw.fifo"; mkfifo "$OUT/raw.fifo"
ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 320x224 -framerate 60.0749 -i "$OUT/raw.fifo" -c:v ffv1 "$OUT/raw.mkv" &
./hostbuild/sh2sim quake32x.32x build/syms.txt $SECS $PADS -video "$OUT/raw.fifo" -audio "$OUT/audio.raw" \
    -vlog "$OUT/vlog.txt" -watch _player 5 -watch _ps 12 -watch _g_teleports 1 -watch _g_last_tele_cls 1 > "$OUT/sim.txt"
wait
rm -f "$OUT/raw.fifo"
tail -8 "$OUT/sim.txt"

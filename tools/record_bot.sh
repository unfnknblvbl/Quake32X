#!/bin/sh
# Record the walkthrough demo in the SH-2 cycle model: builds the DEMO=bot
# ROM, then writes OUT/raw.mkv (every displayed frame, 60.07 Hz, lossless),
# OUT/audio.raw (the PWM output, s16le stereo) and OUT/vlog.txt (per vblank:
# demo phase, FPS, player, player stats, teleport count and class).
# usage: tools/record_bot.sh OUT [seconds]
set -e
cd "$(dirname "$0")/.."
OUT=$1; SECS=${2:-70}
mkdir -p "$OUT"
B=$(tools/build_bot_rom.sh)
cp $B/quake32x.32x "$OUT/quake32x-demo.32x"
rm -f "$OUT/raw.fifo"; mkfifo "$OUT/raw.fifo"
ffmpeg -v error -y -f rawvideo -pix_fmt rgb24 -s 320x224 -framerate 60.0749 -i "$OUT/raw.fifo" -c:v ffv1 "$OUT/raw.mkv" &
./hostbuild/sh2sim $B/quake32x.32x $B/syms.txt $SECS -video "$OUT/raw.fifo" -audio "$OUT/audio.raw" \
    -vlog "$OUT/vlog.txt" -watch _player 5 -watch _ps 12 -watch _g_teleports 1 -watch _g_last_tele_cls 1 > "$OUT/sim.txt"
wait
rm -f "$OUT/raw.fifo"
tail -8 "$OUT/sim.txt"

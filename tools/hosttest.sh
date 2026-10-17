#!/bin/sh
# build the host renderer and render a few reference views
set -e
cd "$(dirname "$0")/.."
gcc -DHOST -O1 -g -fsanitize=address,undefined -fwrapv -Wall -Wno-unused-function \
    -o hostbuild/q32x_host host/host_main.c host/dbg.c src/r_main.c src/r_geom.c src/r_setup.c src/r_surf.c src/r_span.c src/r_model.c src/tables.c \
    src/game.c src/menu.c src/g_main.c src/g_monst.c src/g_world.c src/g_mover.c src/snd.c src/bot.c
OUT=${1:-/tmp}
./hostbuild/q32x_host data/e1m1_le.lvl $OUT/v1.ppm 480 -352 110 90 0 1 1 4096
./hostbuild/q32x_host data/e1m1_le.lvl $OUT/v2.ppm 480 -100 110 90 10 1 1 4096
./hostbuild/q32x_host data/e1m1_le.lvl $OUT/v3.ppm 480 -100 110 90 10 0 1 4096

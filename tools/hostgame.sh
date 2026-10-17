#!/bin/sh
# build the host game (renderer + game logic + demo bot), for tools/hostgame runs
set -e
cd "$(dirname "$0")/.."
mkdir -p hostbuild
# the host reads little-endian copies of the levels
for m in e1m1 e1m2 e1m3; do
    if [ ! -f data/${m}_le.lvl ] || [ tools/mkmap.py -nt data/${m}_le.lvl ] || [ tools/mkents.py -nt data/${m}_le.lvl ] \
       || [ tools/movers.py -nt data/${m}_le.lvl ]; then
        python3 -I tools/mkmap.py --le data/pak0.pak maps/$m.bsp data/${m}_le.lvl > /dev/null
    fi
done
python3 -I tools/make_bot.py data/e1m1_botroute.json hostbuild/bot_data.c >/dev/null
gcc -DHOST -DHOST_CHECK -DDEMO -DDEMO_BOT -O${OPT:-1} -g ${SAN--fsanitize=address,undefined} -fwrapv -Wall -Wno-unused-function -Isrc \
    -o hostbuild/q32x_game host/host_game.c src/r_main.c src/r_geom.c src/r_setup.c src/r_surf.c \
    src/r_span.c src/r_model.c src/tables.c src/game.c src/menu.c src/g_main.c src/g_monst.c \
    src/g_world.c src/g_mover.c src/g_weapon.c src/snd.c src/bot.c host/md_host.c hostbuild/bot_data.c

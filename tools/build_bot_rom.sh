#!/bin/sh
# Build the walkthrough demo ROM (make DEMO=bot) in a copy of the tree, so
# the normal build is left alone. Prints the copy's directory.
cd "$(dirname "$0")/.."
B=${BOT_DIR:-${TMPDIR:-/tmp}/q32x-bot}
rm -rf $B && mkdir -p $B/data
cp -r Makefile src src-md tools $B/ && cp data/*.lvl data/*.json $B/data/ && ln -s "$PWD/data/pak0.pak" $B/data/pak0.pak
touch $B/data/*.lvl
(cd $B && make -s DEMO=bot >/dev/null) || exit 1
/opt/toolchains/sega/bin/sh-elf-nm -n $B/build/q32x.elf > $B/syms.txt
echo $B

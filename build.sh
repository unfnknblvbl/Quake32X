#!/usr/bin/env bash
# Builds Quake 32X (quake32x.32x) on CachyOS / Arch Linux.
#
#   ./build.sh              install what is missing, build the cross compilers
#                           once (about 20-40 minutes), then the cartridge
#   ./build.sh toolchain    only the cross compilers
#   ./build.sh rom          only the cartridge (the compilers must exist)
#   ./build.sh clean        remove the build output (not the compilers)
#
# You need Quake's pak0.pak (the shareware one is enough). Put it at
# data/pak0.pak, or run
#   PAK=/path/to/pak0.pak ./build.sh
#
# Cartridges are 4 MB at most for now, so that they need no mapper (some
# flash carts and consoles have trouble with one). Run in a terminal, the
# script asks which maps to put on the cartridge and keeps a running total:
# whether to keep the textures (full or half size; without them the levels
# are smaller, so more fit), whether liquids, skies and stained glass are
# drawn in plain colours, whether flat shading is in greys, and how the
# sounds are stored. Every sound, texture and model is stored once however
# many maps use it. Not in a terminal (or with MAPS=default), it builds the
# usual cartridge: START and E1M1, half-size textures, flat shading in
# greys, packed sounds (about 3.9 MB).
#
# Settings (environment variables):
#   MAPS        the maps, in order, without asking (e.g. MAPS="e1m1 e1m2"),
#               or "default" for the usual cartridge
#   TEXTURES    full, half or none (1 and 0: full and none) (with MAPS)
#   FLAT_SPECIALS 1 liquids, skies and stained glass in plain colours with
#               textures on too (they then take no space), 0 textured (with MAPS)
#   GREY        1 flat shading in greys, 0 Quake's colours (with MAPS)
#   SOUNDS      full, half, packed or both (with MAPS; see choose_maps)
#   TC_PREFIX   where the compilers go   (default ~/.local/share/q32x-toolchain)
#   JOBS        parallel jobs            (default: all cores)
#   SKIP_DEPS=1 do not run pacman
#   FAST=1      the build with stacks in on-chip RAM (see README)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

TC_PREFIX=${TC_PREFIX:-$HOME/.local/share/q32x-toolchain}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}
BINUTILS_VER=2.43.1
GCC_VER=13.3.0
GNU_MIRROR=${GNU_MIRROR:-https://ftpmirror.gnu.org}

say() { printf '\033[1m== %s\033[0m\n' "$*"; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

deps() {
    [ "${SKIP_DEPS:-0}" = 1 ] && return
    command -v pacman >/dev/null || die "pacman not found: this script is for CachyOS / Arch (set SKIP_DEPS=1 to skip)"
    local pkgs=(base-devel cmake wget texinfo gmp mpfr libmpc)
    local missing=()
    for p in "${pkgs[@]}"; do pacman -Q "$p" >/dev/null 2>&1 || missing+=("$p"); done
    if [ ${#missing[@]} -gt 0 ]; then
        say "installing ${missing[*]}"
        sudo pacman -S --needed --noconfirm "${missing[@]}"
    fi
}

have_toolchain() {
    [ -x "$TC_PREFIX/bin/sh-elf-gcc" ] && [ -x "$TC_PREFIX/bin/m68k-elf-as" ] && [ -x "$TC_PREFIX/bin/m68k-elf-ld" ]
}

fetch() {   # url file
    [ -f "$2" ] && return
    wget -q --show-progress -O "$2.part" "$1" || die "could not download $1"
    mv -f "$2.part" "$2"
}

toolchain() {
    if have_toolchain; then
        say "compilers already in $TC_PREFIX"
        return
    fi
    local work="$PWD/build/toolchain-src"
    mkdir -p "$work" "$TC_PREFIX"
    say "building binutils $BINUTILS_VER and gcc $GCC_VER into $TC_PREFIX (this takes a while)"
    (
        cd "$work"
        fetch "$GNU_MIRROR/binutils/binutils-$BINUTILS_VER.tar.xz" "binutils-$BINUTILS_VER.tar.xz"
        fetch "$GNU_MIRROR/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz" "gcc-$GCC_VER.tar.xz"
        [ -d "binutils-$BINUTILS_VER" ] || tar xf "binutils-$BINUTILS_VER.tar.xz"
        [ -d "gcc-$GCC_VER" ] || tar xf "gcc-$GCC_VER.tar.xz"
        # newer host compilers default to C23, which older GNU sources trip on
        export CFLAGS="-O2 -std=gnu17" CXXFLAGS="-O2"
        for target in sh-elf m68k-elf; do
            if [ ! -x "$TC_PREFIX/bin/$target-as" ]; then
                say "binutils for $target"
                rm -rf "build-binutils-$target" && mkdir "build-binutils-$target"
                (cd "build-binutils-$target" &&
                 "../binutils-$BINUTILS_VER/configure" --target=$target --prefix="$TC_PREFIX" \
                     --disable-nls --disable-werror --disable-gdb --disable-sim --disable-gdbserver \
                     --disable-readline --disable-gprofng --without-zstd --disable-multilib \
                     MAKEINFO=true > configure.log &&
                 make -j"$JOBS" MAKEINFO=true > make.log && make install MAKEINFO=true > install.log) || die "binutils for $target failed (see $work/build-binutils-$target)"
            fi
        done
        say "gcc for sh-elf (the SH-2s: big-endian SH-2, C only, no C library)"
        rm -rf build-gcc && mkdir build-gcc
        (cd build-gcc &&
         PATH="$TC_PREFIX/bin:$PATH" "../gcc-$GCC_VER/configure" --target=sh-elf --prefix="$TC_PREFIX" \
             --enable-languages=c --without-headers --with-newlib --disable-nls --disable-libssp \
             --disable-libquadmath --disable-libgomp --disable-shared --disable-threads --disable-tls \
             --disable-libatomic --disable-libstdcxx --with-endian=big --with-cpu=m2 --with-multilib-list=m2 \
             --disable-werror MAKEINFO=true > configure.log &&
         PATH="$TC_PREFIX/bin:$PATH" make -j"$JOBS" all-gcc all-target-libgcc > make.log &&
         make install-gcc install-target-libgcc > install.log) || die "gcc failed (see $work/build-gcc)"
    )
    have_toolchain || die "the compilers are not where they should be ($TC_PREFIX)"
    say "compilers ready; build/toolchain-src can be deleted"
}

rom() {
    have_toolchain || die "no compilers in $TC_PREFIX: run ./build.sh toolchain first"
    if [ -n "${PAK:-}" ]; then
        [ -f "$PAK" ] || die "PAK=$PAK: no such file"
        mkdir -p data && cp -f "$PAK" data/pak0.pak
    fi
    [ -f data/pak0.pak ] || die "put Quake's pak0.pak at data/pak0.pak (or run PAK=/path/to/pak0.pak ./build.sh)"
    command -v cmake >/dev/null || die "cmake is needed for the converters (pacman -S cmake)"
    if [ "${MAPS:-}" = default ] || { [ -z "${MAPS:-}" ] && [ ! -t 0 ]; }; then
        # the usual cartridge
        MAPS="start e1m1" TEXTURES=${TEXTURES:-half} FLAT_SPECIALS=${FLAT_SPECIALS:-1} GREY=${GREY:-1} SOUNDS=${SOUNDS:-packed} choose_maps
    else
        choose_maps
    fi
    say "building quake32x.32x $(cat VERSION)"
    rm -f quake32x.32x
    make -j"$JOBS" TC="$TC_PREFIX/bin" ${FAST:+FAST=$FAST} PROJECT=build/cartridge.q32x
    local size
    size=$(stat -c %s quake32x.32x)
    say "done: $PWD/quake32x.32x ($((size >> 20)) MB, no mapper needed)"
}

ask_yes() {   # question default(y/n) -> 0 for yes
    local a
    while :; do
        read -r -p "$1 [$( [ "$2" = y ] && echo Y/n || echo y/N )] " a || a=""
        a=${a:-$2}
        case "$a" in y|Y|yes) return 0 ;; n|N|no) return 1 ;; esac
    done
}

# The maps for the cartridge: listed with their sizes on their own, picked,
# then measured together (what each adds, with what the maps before it
# already put in the shared library) until they fit in 4 MB (the first
# 512 KB is the game program); written as build/cartridge.q32x from
# data/quake32x.q32x's settings.
choose_maps() {
    local conv=build/tools/q32x-convert textures specials grey sounds opts=() pal=data/palettes/quake32x-greys.gpl
    make -s TC="$TC_PREFIX/bin" "$conv" >/dev/null
    if [ -n "${MAPS:-}" ]; then
        textures=${TEXTURES:-full}
        specials=${FLAT_SPECIALS:-1}
        grey=${GREY:-1}
        sounds=${SOUNDS:-full}
    else
        echo
        echo "Textures take a large part of each level. Without them every level is flat"
        echo "shaded, and more levels fit on a 4 MB cartridge. With them, the pause menu's"
        echo "Textures setting switches between textured and flat shading as you play."
        echo "  1) full size: as Quake has them"
        echo "  2) half size: a quarter of the space. The finest level of each texture is"
        echo "     left out, so walls look softer close up; from a few steps away the game"
        echo "     draws the smaller levels anyway. No cost in speed"
        echo "  3) none: flat shading only"
        local c
        while :; do
            read -r -p "Textures [1-3, default 2]: " c || c=""
            case "${c:-2}" in 1) textures=full ;; 2) textures=half ;; 3) textures=none ;; *) continue ;; esac
            break
        done
        specials=0
        if [ "$textures" != none ]; then
            echo
            echo "Water, slime, lava, teleporters, skies and stained glass can be drawn in plain"
            echo "colours with the textures on too: they then take no space (and are a little"
            echo "faster to draw), but lose their moving surfaces. With textures on they take"
            echo "the nearest of Quake's own colours, which has no bright greens or purples."
            if ask_yes "Liquids, skies and stained glass in plain colours?" y; then specials=1; fi
        fi
        echo
        if ask_yes "Flat shading in greys (with lava, water, slime, teleporters, sky, stained glass and blood in colour)?" y; then grey=1; else grey=0; fi
        echo
        echo "Sounds take a large part of each level (E1M1's are about 540 KB). Smaller ones"
        echo "take the mixer a little more work, on the CPU that also draws half the"
        echo "picture, so they cost a little speed (measured over E1M3's attract demo,"
        echo "which plays many sounds at once):"
        echo "  1) full quality: as Quake has them, 8-bit at 11 kHz. No cost"
        echo "  2) half rate: half the space. Every other sample is kept (5.5 kHz) and played"
        echo "     twice, so sounds are duller and muffled, with a slight metallic edge; high"
        echo "     sounds suffer most (pickups, the shotgun's crack). About 2-3% slower"
        echo "  3) packed: a little over half the space (57%). 4 bits a sample, each a small"
        echo "     step from the one before: the sounds keep their brightness, with a faint"
        echo "     hiss on quiet ones. The console decodes them as they play: about 2-3%"
        echo "     slower"
        echo "  4) both: about a quarter of the space (28%); duller, with a faint hiss."
        echo "     About 4-5% slower"
        while :; do
            read -r -p "Sounds [1-4, default 3]: " c || c=""
            case "${c:-3}" in 1) sounds=full ;; 2) sounds=half ;; 3) sounds=packed ;; 4) sounds=both ;; *) continue ;; esac
            break
        done
    fi
    case "$textures" in 1|full) textures=full ;; half) ;; 0|none) textures=none ;; *) die "TEXTURES=$textures: full, half or none" ;; esac
    case "$sounds" in full|half|packed|both) ;; *) die "SOUNDS=$sounds: full, half, packed or both" ;; esac
    opts+=(--sounds "$sounds")
    case "$textures" in none) opts+=(--no-textures) ;; half) opts+=(--half-textures) ;; esac
    if [ "$textures" != none ] && [ "$specials" = 1 ]; then opts+=(--flat-specials); fi
    if [ "$grey" = 1 ]; then opts+=(--palette "$pal"); fi
    local names=() sizes=() titles=() missing=() line n s t x i
    local budget=$((4 << 20)) step=65536
    blocks() { echo $(( ($1 + step - 1) / step * step )); }
    if [ -z "${MAPS:-}" ]; then
        say "measuring the maps in data/pak0.pak (about a minute)"
        while IFS=$'\t' read -r n s t x; do
            names+=("$n"); sizes+=("$s"); titles+=("$t"); missing+=("$x")
        done < <("$conv" maps "${opts[@]}" --template data/quake32x.q32x data/pak0.pak)
        [ ${#names[@]} -gt 0 ] || die "no maps found in data/pak0.pak"
        echo
        printf "  %-3s %-8s %8s   %s\n" "" map size title
        for i in "${!names[@]}"; do
            if [ "${sizes[$i]}" = - ]; then
                printf "  %-3s %-8s %8s   (cannot be converted: %s)\n" "" "${names[$i]}" "" "${titles[$i]}"
            else
                printf "  %-3s %-8s %6d KB  %s\n" "$((i + 1)))" "${names[$i]}" $(( $(blocks "${sizes[$i]}") >> 10 )) "${titles[$i]}"
                if [ -n "${missing[$i]}" ]; then
                    local note="not in the game yet: ${missing[$i]}"
                    case "${missing[$i]}" in *Chthon*) note="$note (so it cannot be finished)" ;; esac
                    echo "$note" | fold -s -w 52 | sed 's/ *$//; s/^/                          /'
                fi
            fi
        done
        echo "  Quake 32X is a proof of concept: E1M1-E1M3 play as Quake's do, apart from what"
        echo "  is listed; the other maps load, but without their missing monsters and events."
        echo "  Each size is the map on its own, with all its sounds, textures and models. On"
        echo "  one cartridge the maps share those (each is stored once), so maps together"
        echo "  take less: the total is worked out once you have chosen. Room for maps on a"
        echo "  4 MB cartridge: $(( (budget - (512 << 10)) >> 10 )) KB."
    fi
    local picked=() want w ok
    while :; do
        if [ -n "${MAPS:-}" ]; then
            want=$MAPS
        else
            echo
            read -r -p "Maps to include, in order (numbers or names, e.g. 1 2): " want || die "no maps chosen"
        fi
        picked=(); ok=1
        for w in $want; do
            local found=""
            if [ ${#names[@]} -eq 0 ]; then found=${w,,}; picked+=("$found"); continue; fi
            for i in "${!names[@]}"; do
                if [ "$w" = "$((i + 1))" ] || [ "${w,,}" = "${names[$i]}" ]; then found=$i; fi
            done
            if [ -z "$found" ] || [ "${sizes[$found]}" = - ]; then echo "  $w: not a map that can be used"; ok=0; continue; fi
            picked+=("${names[$found]}")
        done
        if [ ${#picked[@]} -eq 0 ]; then ok=0; echo "  choose at least one map"; fi
        if [ ${#picked[@]} -gt 8 ]; then ok=0; echo "  at most 8 maps"; fi
        if [ $ok = 1 ]; then
            say "measuring ${picked[*]} together"
            local total=0 st=0 lname own adds run
            printf "  %-8s %10s %14s %10s\n" map "its own" "shared, new" total
            while IFS=$'\t' read -r lname own adds run; do
                if [ "$lname" = total ]; then total=$own; continue; fi
                printf "  %-8s %7d KB %11d KB %7d KB\n" "$lname" $(( own >> 10 )) $(( adds >> 10 )) $(( run >> 10 ))
            done < <("$conv" size "${opts[@]}" --template data/quake32x.q32x data/pak0.pak "${picked[@]}" || true)
            [ "$total" -gt 0 ] || die "could not measure ${picked[*]}"
            if [ "$total" -gt "$budget" ]; then
                echo "  ${picked[*]}: $((total >> 10)) KB with the game program, $(( (total - budget) >> 10 )) KB more than fits in 4 MB"
                ok=0
            else
                echo "  the cartridge: $((total >> 10)) KB of 4096 KB"
            fi
        fi
        if [ $ok = 1 ]; then break; fi
        [ -z "${MAPS:-}" ] || die "MAPS=\"$MAPS\" does not fit in 4 MB or names a map that cannot be used"
    done
    say "the cartridge: ${picked[*]}, $([ "$textures" = none ] && echo "flat shading only" || echo "$textures-size textures")$([ "$specials" = 1 ] && [ "$textures" != none ] && echo ", plain liquids, skies and glass")$([ "$grey" = 1 ] && echo ", flat shading in greys"), $sounds sounds"
    "$conv" make-project "${opts[@]}" --template data/quake32x.q32x data/pak0.pak build/cartridge.q32x "${picked[@]}"
}

case "${1:-all}" in
    all) deps; toolchain; rom ;;
    toolchain) deps; toolchain ;;
    rom) rom ;;
    clean) make clean ;;
    *) echo "usage: $0 [all|toolchain|rom|clean]"; exit 2 ;;
esac

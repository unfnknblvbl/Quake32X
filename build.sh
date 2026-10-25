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
# Run in a terminal, the script asks which maps to put on the cartridge, so
# that it stays within 4 MB and needs no mapper (some flash carts and
# consoles have trouble with one): whether to keep the textures (without
# them the levels are smaller, so more fit), and whether flat shading is in
# greys. Not in a terminal (or with MAPS=default), it builds the usual
# cartridge: E1M1-E1M3 with textures, 9 MB, which needs Sega's mapper.
#
# Settings (environment variables):
#   MAPS        the maps, in order, without asking (e.g. MAPS="e1m1 e1m7"),
#               or "default" for the usual cartridge
#   TEXTURES    1 keep the textures, 0 flat shading only (with MAPS)
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
    local project=""
    if [ "${MAPS:-}" != default ] && { [ -n "${MAPS:-}" ] || [ -t 0 ]; }; then
        choose_maps
        project=build/cartridge.q32x
    fi
    say "building quake32x.32x $(cat VERSION)"
    make -j"$JOBS" TC="$TC_PREFIX/bin" ${FAST:+FAST=$FAST} ${project:+PROJECT=$project}
    local size
    size=$(stat -c %s quake32x.32x)
    if [ "$size" -le $((4 << 20)) ]; then
        say "done: $PWD/quake32x.32x ($((size >> 20)) MB, no mapper needed)"
    else
        say "done: $PWD/quake32x.32x ($((size >> 20)) MB, needs a flash cart with Sega's mapper)"
    fi
}

ask_yes() {   # question default(y/n) -> 0 for yes
    local a
    while :; do
        read -r -p "$1 [$( [ "$2" = y ] && echo Y/n || echo y/N )] " a || a=""
        a=${a:-$2}
        case "$a" in y|Y|yes) return 0 ;; n|N|no) return 1 ;; esac
    done
}

# The maps for the cartridge: measured with the chosen settings, picked
# until they fit in 4 MB (the first 512 KB is the game program), then
# written as build/cartridge.q32x from data/quake32x.q32x's settings.
choose_maps() {
    local conv=build/tools/q32x-convert textures grey sounds opts=() pal=data/palettes/quake32x-greys.gpl
    make -s TC="$TC_PREFIX/bin" "$conv" >/dev/null
    if [ -n "${MAPS:-}" ]; then
        textures=${TEXTURES:-1}
        grey=${GREY:-1}
        sounds=${SOUNDS:-full}
    else
        echo
        echo "Textures make the levels bigger: without them every level is flat shaded,"
        echo "and more of them fit on a 4 MB cartridge."
        if ask_yes "Keep the textures?" y; then textures=1; else textures=0; fi
        if ask_yes "Flat shading in greys (with lava, water, slime, teleporters, sky and blood in colour)?" y; then grey=1; else grey=0; fi
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
        local c
        while :; do
            read -r -p "Sounds [1-4, default 1]: " c || c=""
            case "${c:-1}" in 1) sounds=full ;; 2) sounds=half ;; 3) sounds=packed ;; 4) sounds=both ;; *) continue ;; esac
            break
        done
    fi
    case "$sounds" in full|half|packed|both) ;; *) die "SOUNDS=$sounds: full, half, packed or both" ;; esac
    opts+=(--sounds "$sounds")
    [ "$textures" = 1 ] || opts+=(--no-textures)
    if [ "$grey" = 1 ]; then opts+=(--palette "$pal"); fi
    say "measuring the maps in data/pak0.pak (about a minute)"
    local names=() sizes=() titles=() line n s t
    while IFS=$'\t' read -r n s t; do
        names+=("$n"); sizes+=("$s"); titles+=("$t")
    done < <("$conv" maps "${opts[@]}" --template data/quake32x.q32x data/pak0.pak)
    [ ${#names[@]} -gt 0 ] || die "no maps found in data/pak0.pak"
    local budget=$(( (4 << 20) - (512 << 10) )) step=65536 i
    blocks() { echo $(( ($1 + step - 1) / step * step )); }
    echo
    printf "  %-3s %-8s %8s   %s\n" "" map size title
    for i in "${!names[@]}"; do
        if [ "${sizes[$i]}" = - ]; then
            printf "  %-3s %-8s %8s   (cannot be converted: %s)\n" "" "${names[$i]}" "" "${titles[$i]}"
        else
            printf "  %-3s %-8s %6d KB  %s\n" "$((i + 1)))" "${names[$i]}" $(( $(blocks "${sizes[$i]}") >> 10 )) "${titles[$i]}"
        fi
    done
    echo "  Room for maps on a 4 MB cartridge: $((budget >> 10)) KB (sizes include each map's sounds;"
    echo "  maps that share sounds take a little less together)."
    local picked=() total want w ok
    while :; do
        if [ -n "${MAPS:-}" ]; then
            want=$MAPS
        else
            echo
            read -r -p "Maps to include, in order (numbers or names, e.g. 1 7): " want || die "no maps chosen"
        fi
        picked=(); total=0; ok=1
        for w in $want; do
            local found=""
            for i in "${!names[@]}"; do
                if [ "$w" = "$((i + 1))" ] || [ "${w,,}" = "${names[$i]}" ]; then found=$i; fi
            done
            if [ -z "$found" ] || [ "${sizes[$found]}" = - ]; then echo "  $w: not a map that can be used"; ok=0; continue; fi
            picked+=("${names[$found]}")
            total=$(( total + $(blocks "${sizes[$found]}") ))
        done
        if [ ${#picked[@]} -eq 0 ]; then ok=0; echo "  choose at least one map"; fi
        if [ ${#picked[@]} -gt 8 ]; then ok=0; echo "  at most 8 maps"; fi
        if [ $ok = 1 ] && [ $total -gt $budget ]; then
            echo "  ${picked[*]}: $((total >> 10)) KB, $(( (total - budget) >> 10 )) KB more than fits in 4 MB"
            ok=0
        fi
        if [ $ok = 1 ]; then break; fi
        [ -z "${MAPS:-}" ] || die "MAPS=\"$MAPS\" does not fit in 4 MB or names a map that cannot be used"
    done
    say "the cartridge: ${picked[*]}, $([ "$textures" = 1 ] && echo textured || echo flat shading only)$([ "$grey" = 1 ] && echo ", flat shading in greys"), $sounds sounds, about $((total >> 10)) KB of maps"
    "$conv" make-project "${opts[@]}" --template data/quake32x.q32x data/pak0.pak build/cartridge.q32x "${picked[@]}"
}

case "${1:-all}" in
    all) deps; toolchain; rom ;;
    toolchain) deps; toolchain ;;
    rom) rom ;;
    clean) make clean ;;
    *) echo "usage: $0 [all|toolchain|rom|clean]"; exit 2 ;;
esac

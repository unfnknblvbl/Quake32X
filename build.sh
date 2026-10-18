#!/usr/bin/env bash
# Builds Quake 32X (quake32x.32x) on CachyOS / Arch Linux.
#
#   ./build.sh              install what is missing, build the cross compilers
#                           once (about 20-40 minutes), then the cartridge
#   ./build.sh toolchain    only the cross compilers
#   ./build.sh rom          only the cartridge (the compilers must exist)
#   ./build.sh clean        remove the build output (not the compilers)
#
# You need Quake's pak0.pak (the shareware one is enough: the cartridge uses
# the first three levels of episode 1). Put it at data/pak0.pak, or run
#   PAK=/path/to/pak0.pak ./build.sh
#
# Settings (environment variables):
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
    local pkgs=(base-devel python python-numpy wget texinfo gmp mpfr libmpc)
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
    command -v python3 >/dev/null || die "python3 is needed"
    python3 -c 'import numpy' 2>/dev/null || die "python numpy is needed (pacman -S python-numpy)"
    say "building quake32x.32x $(cat VERSION)"
    make -j"$JOBS" TC="$TC_PREFIX/bin" ${FAST:+FAST=$FAST}
    say "done: $PWD/quake32x.32x"
}

case "${1:-all}" in
    all) deps; toolchain; rom ;;
    toolchain) deps; toolchain ;;
    rom) rom ;;
    clean) make clean ;;
    *) echo "usage: $0 [all|toolchain|rom|clean]"; exit 2 ;;
esac

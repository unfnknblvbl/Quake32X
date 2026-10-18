#!/usr/bin/env python3
"""
mkrom.py - put the cartridge together: the program in the first 512 KB
page, then each level blob from a page boundary.

usage: mkrom.py [--version X.Y.Z] build/q32x.bin out.32x e1m1.lvl e1m2.lvl e1m3.lvl

The version goes in the Mega Drive header's memo field (0x1C8).

The ROM is bigger than the 32X's 4 MB window, so it uses Sega's standard
mapper (Super Street Fighter II's): the window is eight 512 KB pages, the
first fixed, the other seven set through the registers at 0xA130F3..FF.
The SH-2s see a level at 0x02080000 once the 68000 has mapped its pages
there (src/level.c, src-md/md.s). The level directory in the program
(LEVELDIR, src/crt0.s) gets each level's first page, page count and name.

Emulators turn the mapper on for 32X images over 4 MB (PicoDrive does).
"""
import os
import struct
import sys

PAGE = 512 * 1024
WINDOW_PAGES = 7
MAX_LEVELS = 8


def main():
    version = None
    if len(sys.argv) > 2 and sys.argv[1] == '--version':
        version = sys.argv[2]
        del sys.argv[1:3]
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    prog = bytearray(open(sys.argv[1], 'rb').read())
    if version:
        memo = ('Version ' + version).encode('ascii')[:40]
        prog[0x1C8:0x1C8 + 40] = memo.ljust(40, b' ')
    if len(prog) > PAGE:
        raise SystemExit('program too big for the fixed page: %d bytes' % len(prog))
    levels = sys.argv[3:]
    assert len(levels) <= MAX_LEVELS
    d = prog + b'\xff' * (PAGE - len(prog))
    dirent = []
    for path in levels:
        blob = open(path, 'rb').read()
        npages = (len(blob) + PAGE - 1) // PAGE
        if npages > WINDOW_PAGES:
            raise SystemExit('%s: %d bytes, more than the %d KB window' % (path, len(blob),
                                                                         WINDOW_PAGES * PAGE // 1024))
        first = len(d) // PAGE
        d += blob + b'\xff' * (npages * PAGE - len(blob))
        name = os.path.basename(path).split('.')[0][:11]
        # flags: 1 = the level has a demo (its LVL_DEMO lump, src/level.h)
        LVL_DEMO = 28
        has_demo = struct.unpack_from('>i', blob, 8 + LVL_DEMO * 8 + 4)[0] > 0
        dirent.append((first, npages, name, 1 if has_demo else 0))
        print('  %-6s pages %2d..%2d  %7d bytes (%d KB free in the window)%s' % (
            name, first, first + npages - 1, len(blob), (WINDOW_PAGES * PAGE - len(blob)) // 1024,
            ', with a demo' if has_demo else ''))
    # whole megabytes, at least 8 MB (some loaders expect a power of two)
    size = 8 << 20
    while size < len(d):
        size += 1 << 20
    d += b'\xff' * (size - len(d))
    at = d.find(b'LEVELDIR', 0, PAGE)
    assert at >= 0 and d.find(b'LEVELDIR', at + 1, PAGE) < 0, 'no single LEVELDIR in the program'
    struct.pack_into('>I', d, at + 8, len(dirent))
    for i, (first, npages, name, flags) in enumerate(dirent):
        struct.pack_into('>BBH12s', d, at + 12 + 16 * i, first, npages, flags, name.encode('latin1'))
    struct.pack_into('>I', d, 0x1A4, size - 1)
    s = 0
    for i in range(0x200, size, 2):
        s += (d[i] << 8) | d[i + 1]
    struct.pack_into('>H', d, 0x18E, s & 0xFFFF)
    open(sys.argv[2], 'wb').write(d)
    print('%s: %d KB (program %d KB), checksum %04X' % (sys.argv[2], size // 1024, len(prog) // 1024,
                                                       s & 0xFFFF))


if __name__ == '__main__':
    main()

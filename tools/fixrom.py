#!/usr/bin/env python3
"""Pad a 32X ROM image, fill in the ROM end address and the MD checksum."""
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read())
size = 1 << 19
while size < len(d):
    size <<= 1
if size > 4 << 20:
    raise SystemExit('ROM too big: %d bytes' % len(d))
d += b'\xff' * (size - len(d))
struct.pack_into('>I', d, 0x1A4, size - 1)
s = 0
for i in range(0x200, size, 2):
    s += (d[i] << 8) | d[i + 1]
struct.pack_into('>H', d, 0x18E, s & 0xFFFF)
open(sys.argv[2], 'wb').write(d)
print('%s: %d KB, checksum %04X' % (sys.argv[2], size // 1024, s & 0xFFFF))

/* 68000 program: ROM code at 0x880800 (ROM offset 0x800), followed by the
   image of the loop and vblank handler that it copies to work RAM. The
   Mega Drive data (tools/mkmd.py) starts at ROM offset 0x4000. */
SECTIONS
{
    .text 0x880800 : { *(.text) }
    .ramtext 0xFF0000 : AT(LOADADDR(.text) + SIZEOF(.text)) { _ram_start = .; *(.ramtext) . = ALIGN(2); _ram_end = .; }
    _ram_load = LOADADDR(.ramtext);
    _md_end = LOADADDR(.ramtext) + SIZEOF(.ramtext);
    ASSERT(_md_end <= 0x884000, "68000 code overlaps the Mega Drive data")
}

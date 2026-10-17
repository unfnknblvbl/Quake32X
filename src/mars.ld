/* Quake 32X memory map
 *   ROM   0x02000000  4 MB  (SH-2 view of the cartridge while RV=0)
 *   SDRAM 0x06000000  256 KB
 * The SH-2 program (code + data) runs from SDRAM: the 32X boot ROM copies
 * it there using the Mars header. Level data and the cold code (game
 * logic, the bot, the menu, the Mega Drive link, the sine table) stay in ROM.
 */
OUTPUT_FORMAT("elf32-sh")
OUTPUT_ARCH(sh)
ENTRY(pri_start)

MEMORY
{
    rom   (rx)  : ORIGIN = 0x02000000, LENGTH = 4M
    sdram (rwx) : ORIGIN = 0x06000000, LENGTH = 0x40000
}

SECTIONS
{
    .romhdr 0x02000000 :
    {
        KEEP(*(.romhdr))
    } > rom

    . = ALIGN(16);
    __sdram_load = .;

    .sdram 0x06000000 : AT(__sdram_load)
    {
        __sdram_start = .;
        KEEP(*(.vectors))
        *(.text.hot .text.hot.*)
        *(EXCLUDE_FILE(*g_main.o *g_monst.o *g_mover.o *bot.o *menu.o *game.o *mdlink.o *tables.o) .text EXCLUDE_FILE(*g_main.o *g_monst.o *g_mover.o *bot.o *menu.o *game.o *mdlink.o *tables.o) .text.*)
        *(EXCLUDE_FILE(*g_main.o *g_monst.o *g_mover.o *bot.o *menu.o *game.o *mdlink.o *tables.o) .rodata EXCLUDE_FILE(*g_main.o *g_monst.o *g_mover.o *bot.o *menu.o *game.o *mdlink.o *tables.o) .rodata.*)
        *(.data .data.*)
        *(.sdata .sdata.*)
        . = ALIGN(16);
        __sdram_end = .;
    } > sdram

    __sdram_size = __sdram_end - __sdram_start;

    /* game logic: cold code, run from cartridge ROM through the cache */
    .romtext (__sdram_load + __sdram_size) : AT(__sdram_load + __sdram_size)
    {
        *(.romtext .romtext.*)
        *g_main.o(.text .text.* .rodata .rodata.*)
        *g_monst.o(.text .text.* .rodata .rodata.*)
        *g_mover.o(.text .text.* .rodata .rodata.*)
        *bot.o(.text .text.* .rodata .rodata.*)
        *menu.o(.text .text.* .rodata .rodata.*)
        *game.o(.text .text.* .rodata .rodata.*)
        *mdlink.o(.text .text.* .rodata .rodata.*)
        *tables.o(.text .text.* .rodata .rodata.*)
        . = ALIGN(16);
    } > rom

    .romdata : AT(ADDR(.romdata))
    {
        KEEP(*(.romdata))
        . = ALIGN(16);
        __rom_end = .;
    } > rom

    .bss __sdram_end (NOLOAD) :
    {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > sdram

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

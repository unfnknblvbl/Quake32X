| Mega Drive (68000) side for Quake 32X.
|
| The 68000 sets up the Mega Drive, releases the SH-2s, reads the joypad
| once per frame into COMM12, and draws on the Mega Drive VDP, whose
| picture lies over the 32X one (the 32X shows wherever the Mega Drive
| shows nothing):
|   plane A  text (Quake's console font), 4 lines down from the tile grid
|   plane B  the border around a reduced view, the status bar background,
|            the dither that shades the screen behind the menu
|   sprites  the view weapon, pre-rendered for each screen size
| The tiles, palettes and weapon frames come from md_data in cartridge ROM
| (tools/mkmd.py, at ROM offset 0x4000).
|
| The SH-2 master sends 16-bit words through COMM0, numbered in COMM10's
| low byte; the 68000 acknowledges each one in COMM14's high byte. Words:
|   0nnn nnnn nnnn nnnn  a tile word: written at the cursor (with the
|                        priority bit set), the cursor moves one cell right
|   1000 0ccc cccc cccc  cursor to plane A cell c (row * 64 + column)
|   1001 0ccc cccc cccc  cursor to plane B cell c
|   1010 llll ffff ffff  the weapon: frame f (255 = none), light level l;
|                        shown at the next vertical blank
|   1011 ssss ssss ssss  load weapon set s (one weapon at one screen size)
|
| The loop runs from work RAM, so the 68000 stays off the cartridge bus the
| SH-2s depend on, except when it copies tiles.

        .equ    MD_DATA,  0x884000
        .equ    VDP_DATA, 0xC00000
        .equ    VDP_CTRL, 0xC00004
        .equ    COMM0,    0xA15120
        .equ    SEQ,      0xA1512B      /* COMM10 low byte */
        .equ    COMM12,   0xA1512C
        .equ    ACK,      0xA1512E      /* COMM14 high byte */
        .equ    VCOUNT,   0xA1512F      /* COMM14 low byte: vblank count */
        .equ    SAT_CMD,  0x70000003    /* VRAM write 0xF000: sprite table */
        .equ    GUN_CMD,  0x72000000    /* VRAM write 0x3200: weapon tiles (tile 400) */
        .equ    CRAM2_CMD, 0xC0400000   /* CRAM write, palette line 2 */

        .text
        .org    0x000

| 0x880800 - entry after the 32X boot/security code
        .global _start
_start:
        bra.w   main_start

| 0x880840 - general exception handler (entered via "jsr" from table)
        .org    0x040
        addq.l  #4,sp
        rte

| 0x880880 - level 4 (HBlank)
        .org    0x080
        rte

| 0x8808C0 - level 6 (VBlank): the handler is in work RAM
        .org    0x0C0
        jmp     vblank

| 0x880900 - level 2 (EXT)
        .org    0x100
        rte

vdp_regs:
        .word   0x8004                  /* no HBL int, HV counter on */
        .word   0x8134                  /* display off, DMA on, mode 5 */
        .word   0x8230                  /* plane A at 0xC000 */
        .word   0x8334                  /* window at 0xD000 (unused) */
        .word   0x8407                  /* plane B at 0xE000 */
        .word   0x8578                  /* sprites at 0xF000 */
        .word   0x8700                  /* backdrop colour 0 */
        .word   0x8AFF
        .word   0x8B00                  /* full-screen scroll */
        .word   0x8C81                  /* H40 */
        .word   0x8D3D                  /* hscroll at 0xF400 */
        .word   0x8F02                  /* auto-increment 2 */
        .word   0x9001                  /* 64x32 planes */
        .word   0x9100                  /* no window */
        .word   0x9200
        .equ    NREGS, 15

main_start:
        move.w  #0x2700,sr
        lea     0x00FFFFE0,sp

        lea     VDP_CTRL,a0
        lea     VDP_DATA,a1
        move.w  (a0),d0                 /* reset VDP write latch */
        lea     vdp_regs(pc),a2
        moveq   #NREGS-1,d1
1:      move.w  (a2)+,(a0)
        dbra    d1,1b

        | clear VRAM, CRAM and VSRAM
        moveq   #0,d0
        move.l  #0x40000000,(a0)
        move.w  #16384-1,d1
2:      move.l  d0,(a1)
        dbra    d1,2b
        move.l  #0xC0000000,(a0)
        moveq   #32-1,d1
3:      move.l  d0,(a1)
        dbra    d1,3b
        move.l  #0x40000010,(a0)        /* VSRAM: plane A 4 lines down */
        move.w  #0x3FC,(a1)
        move.w  #0,(a1)

        | fixed tiles (font, border) and their palettes, from ROM
        lea     MD_DATA,a2
        move.l  a2,a3
        add.l   4(a2),a3
        move.l  8(a2),d1
        lsl.l   #3,d1
        subq.w  #1,d1
        move.l  #0x40000000,(a0)
4:      move.l  (a3)+,(a1)
        dbra    d1,4b
        move.l  a2,a3
        add.l   12(a2),a3
        move.l  #0xC0000000,(a0)
        moveq   #16-1,d1
5:      move.l  (a3)+,(a1)
        dbra    d1,5b

        | the main loop and the vblank handler run from work RAM
        lea     _ram_load,a3
        lea     _ram_start,a4
        move.l  #_ram_end,d1
        sub.l   #_ram_start,d1
        lsr.l   #1,d1
        subq.w  #1,d1
6:      move.w  (a3)+,(a4)+
        dbra    d1,6b

        | joypad ports: TH as output
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

        | let the SH-2s at the cartridge
        move.w  #0,0xA15104             /* bank select 0 */
        move.b  #0,0xA15107             /* clear RV */
7:
        cmp.l   #0x4D5F4F4B,0xA15120    /* "M_OK" */
        bne.b   7b
8:
        cmp.l   #0x535F4F4B,0xA15124    /* "S_OK" */
        bne.b   8b

        move.l  #0,0xA1512C             /* no buttons, ack 0, vblank count 0 */
        move.w  #0,0xA1512A             /* no words sent yet */

        move.w  0xA15100,d0
        or.w    #0x8000,d0
        move.w  d0,0xA15100             /* FM = 1: SH-2s own the 32X VDP */
        move.l  #0,0xA15120             /* release the master SH-2 */

        move.w  #0x8174,0xC00004        /* display on, vblank int on */
        move.w  #0x2000,sr
        jmp     main_loop

|-----------------------------------------------------------------------
| work RAM
|-----------------------------------------------------------------------
        .section .ramtext,"ax"

main_loop:
        lea     VDP_DATA,a5
        lea     VDP_CTRL,a6
        moveq   #0,d7                   /* last word number seen */
        move.l  #0x40000003,d6          /* cursor: plane A cell 0 */
        clr.l   cur_set
        move.w  #-1,cur_level
        clr.b   gun_dirty
        clr.b   vbl_flag
loop:
        tst.b   vbl_flag
        bne.s   vbl_work
        move.b  SEQ,d0
        cmp.b   d0,d7
        beq.s   loop
        move.b  d0,d7
        move.w  COMM0,d1
        move.b  d7,ACK
        tst.w   d1
        bmi.s   command
        move.l  d6,(a6)                 /* a tile at the cursor */
        or.w    #0x8000,d1
        move.w  d1,(a5)
        add.l   #0x00020000,d6
        bra.s   loop

vbl_work:
        clr.b   vbl_flag
        tst.b   gun_dirty
        beq.s   loop
        clr.b   gun_dirty
        bsr.w   show_gun
        bra.s   loop

command:
        move.w  d1,d0
        rol.w   #4,d0
        and.w   #7,d0
        add.w   d0,d0
        move.w  cmd_table(pc,d0.w),d0
        jmp     cmd_table(pc,d0.w)
cmd_table:
        .word   cur_a - cmd_table, cur_b - cmd_table, gun - cmd_table, load - cmd_table
        .word   loop - cmd_table, loop - cmd_table, loop - cmd_table, loop - cmd_table

cur_a:
        and.w   #0x7FF,d1
        moveq   #0,d6
        move.w  d1,d6
        add.w   d6,d6
        swap    d6
        or.l    #0x40000003,d6
        bra.w   loop
cur_b:
        and.w   #0x7FF,d1
        moveq   #0,d6
        move.w  d1,d6
        add.w   d6,d6
        add.w   #0x2000,d6
        swap    d6
        or.l    #0x40000003,d6
        bra.w   loop
gun:
        move.w  d1,gun_req
        st      gun_dirty
        bra.w   loop
load:
        bsr.w   load_set
        bra.w   loop

| the weapon's sprites and palette for the frame in gun_req
show_gun:
        move.w  gun_req,d1
        move.l  cur_set,d0
        beq.w   hide_gun
        move.l  d0,a0
        moveq   #0,d2
        move.b  d1,d2                   /* frame */
        cmp.w   6(a0),d2
        bhs.w   hide_gun
        move.w  d1,d3                   /* light level */
        lsr.w   #8,d3
        and.w   #15,d3
        cmp.w   cur_level,d3
        beq.s   1f
        move.w  d3,cur_level
        lea     MD_DATA,a1
        add.l   12(a0),a1
        lsl.w   #5,d3
        add.w   d3,a1
        move.l  #CRAM2_CMD,(a6)
        moveq   #8-1,d4
2:      move.l  (a1)+,(a5)
        dbra    d4,2b
1:
        lea     MD_DATA,a1
        add.l   8(a0),a1
        add.w   d2,d2
        add.w   d2,d2
        move.l  0(a1,d2.w),d0
        lea     MD_DATA,a1
        add.l   d0,a1
        move.w  (a1)+,d4                /* sprite count */
        beq.s   hide_gun
        move.l  #SAT_CMD,(a6)
        moveq   #1,d5                   /* link to the next sprite */
        subq.w  #1,d4
3:      move.w  (a1)+,(a5)              /* y */
        move.w  (a1)+,d0                /* size */
        tst.w   d4
        beq.s   4f
        or.b    d5,d0
4:      move.w  d0,(a5)
        move.w  (a1)+,(a5)              /* tile */
        move.w  (a1)+,(a5)              /* x */
        addq.w  #1,d5
        dbra    d4,3b
        rts
hide_gun:
        move.l  #SAT_CMD,(a6)
        move.l  #0,(a5)                 /* above the screen, end of the list */
        move.l  #0,(a5)
        rts

| d1: 1011 ssss ssss ssss - copy weapon set s's tiles to VRAM
load_set:
        bsr.s   hide_gun
        and.l   #0xFFF,d1
        lea     MD_DATA,a0
        cmp.l   16(a0),d1
        bhs.s   9f
        add.l   20(a0),a0
        lsl.w   #5,d1
        add.w   d1,a0
        move.l  a0,cur_set
        move.w  #-1,cur_level
        lea     MD_DATA,a1
        add.l   (a0),a1
        moveq   #0,d2
        move.w  4(a0),d2
        lsl.w   #3,d2
        beq.s   8f
        subq.w  #1,d2
        move.l  #GUN_CMD,(a6)
7:      move.l  (a1)+,(a5)
        dbra    d2,7b
8:      rts
9:      clr.l   cur_set
        rts

| level 6: read pad 1 into COMM12, count the frame, wake the loop
vblank:
        movem.l d0-d2/a0,-(sp)
        move.w  VDP_CTRL,d0             /* ack VDP interrupt */
        lea     0xA10003,a0
        bsr.s   get_pad
        move.w  d2,COMM12
        addq.b  #1,VCOUNT
        st      vbl_flag
        movem.l (sp)+,d0-d2/a0
        rte

| get current pad value (Chilly Willy's routine)
| entry: a0 = pad data port
| exit:  d2 = 0 0 0 1 M X Y Z S A C B R L D U (1 = pressed) or 0xF000 (no pad)
get_pad:
        bsr.b   get_input       /* - 0 s a 0 0 d u - 1 c b r l d u */
        move.w  d0,d1
        andi.w  #0x0C00,d0
        bne.b   no_pad
        bsr.b   get_input
        bsr.b   get_input       /* - 0 s a 0 0 0 0 - 1 c b m x y z */
        move.w  d0,d2
        bsr.b   get_input       /* - 0 s a 1 1 1 1 - 1 c b r l d u */
        andi.w  #0x0F00,d0
        cmpi.w  #0x0F00,d0
        beq.b   common          /* six button pad */
        move.w  #0x010F,d2      /* three button pad */
common:
        lsl.b   #4,d2
        lsl.w   #4,d2
        andi.w  #0x303F,d1
        move.b  d1,d2
        lsr.w   #6,d1
        or.w    d1,d2
        eori.w  #0x1FFF,d2
        rts
no_pad:
        move.w  #0xF000,d2
        rts

get_input:
        move.b  #0x00,(a0)
        nop
        nop
        move.b  (a0),d0
        move.b  #0x40,(a0)
        lsl.w   #8,d0
        move.b  (a0),d0
        rts

        .even
cur_set:    .long   0                   /* ROM address of the loaded set's entry */
cur_level:  .word   -1
gun_req:    .word   0x00FF
gun_dirty:  .byte   0
vbl_flag:   .byte   0
        .even

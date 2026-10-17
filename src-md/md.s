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
|   1010 0000 ffff ffff  the weapon: frame f (255 = none), shown at the
|                        next vertical blank
|   1011 ssss ssss ssss  load weapon set s (one weapon at one screen size)
|   1100 0000 00pp pppp  the cartridge mapper: pages p .. p+6 into the
|                        window's pages 1..7 (the SH-2s then see page p at
|                        0x02080000)
|   1111 xxxx xxxx xxxx  nothing (the SH-2 sends one to know the rest is done)
|
| The pad: an ordinary 3/6-button pad, or the XE-1 AP analogue pad (with
| 32X-HD's extension for a modern pad's right stick Y and triggers), tried
| when no ordinary pad answers. COMM12 holds the buttons in the SH-2's pad
| format; for the analogue pad bit 13 is set, bits 14/15 are the left/right
| trigger, the stick also sets the d-pad bits (for the menus), and COMM2
| cycles through three words of the axes (7 bits each, centre 64):
|   00xx xxxx xyyy yyyy  left stick X, Y
|   01xx xxxx xyyy yyyy  right stick X (channel 3), right stick Y (64 without
|                        the extension)
|   10zz zzzz ze00 0000  throttle Z (channel 2), e = the extension is there
|
| The loop runs from work RAM, so the 68000 stays off the cartridge bus the
| SH-2s depend on, except when it copies tiles.

        .equ    MD_DATA,  0x884000
        .equ    VDP_DATA, 0xC00000
        .equ    VDP_CTRL, 0xC00004
        .equ    COMM0,    0xA15120
        .equ    COMM2,    0xA15122
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
        clr.b   pal_dirty
        clr.b   gun_dirty
        clr.b   vbl_flag
loop:
        tst.b   vbl_flag
        bne.s   vbl_work
        move.b  SEQ,d0
        cmp.b   d0,d7
        beq.s   idle
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

| nothing to do: every 32 turns, the next analogue word into COMM2
idle:
        subq.b  #1,d5
        bne.s   loop
        move.b  #32,d5
        move.w  an_idx,d0
        addq.w  #2,d0
        cmp.w   #6,d0
        blo.s   1f
        moveq   #0,d0
1:      move.w  d0,an_idx
        lea     an_words,a0
        move.w  0(a0,d0.w),COMM2
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
        .word   bank - cmd_table, loop - cmd_table, loop - cmd_table, loop - cmd_table

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
| Sega's mapper (SSF2): 0xA130F3, F5 .. FF choose the pages at 512 KB,
| 1 MB .. 3.5 MB. Only the SH-2s read there; this loop runs from work RAM
bank:
        and.w   #0x3F,d1
        lea     0xA130F3,a0
        moveq   #7-1,d2
1:      move.b  d1,(a0)
        addq.w  #2,a0
        addq.b  #1,d1
        dbra    d2,1b
        bra.w   loop

| the weapon's sprites for the frame in gun_req, and its two palettes
| (lines 2 and 3) after a new set was loaded
show_gun:
        move.w  gun_req,d1
        move.l  cur_set,d0
        beq.w   hide_gun
        move.l  d0,a0
        moveq   #0,d2
        move.b  d1,d2                   /* frame */
        cmp.w   6(a0),d2
        bhs.w   hide_gun
        tst.b   pal_dirty
        beq.s   1f
        clr.b   pal_dirty
        lea     MD_DATA,a1
        add.l   12(a0),a1
        move.l  #CRAM2_CMD,(a6)
        moveq   #16-1,d4
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
        st      pal_dirty
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
        movem.l d0-d7/a0-a2,-(sp)
        move.w  VDP_CTRL,d0             /* ack VDP interrupt */
        lea     0xA10003,a0
        tst.b   pad_xe
        bne.s   1f
        bsr.w   get_pad
        cmp.w   #0xF000,d2
        bne.s   3f                      /* an ordinary pad */
1:      bsr.w   xe1ap_read              /* nothing ordinary there: an XE-1 AP? */
        lea     nib,a1
        cmp.w   #12,d0
        blo.s   2f
        cmp.b   #15,10(a1)
        bne.s   2f
        st      pad_xe
        bsr.w   xe1ap_pack
        bra.s   3f
2:      clr.b   pad_xe
        move.w  #0xF000,d2
3:      move.w  d2,COMM12
        addq.b  #1,VCOUNT
        st      vbl_flag
        movem.l (sp)+,d0-d7/a0-a2
        rte

| read a report from the XE-1 AP on port 1 (a0 = 0xA10003) into nib[]:
| 12 nibbles, or 20 with the extension; d0 = the number read. The pad does
| not wait: each nibble is there for 10 us (77 clocks), so the loop only
| stores what it sees (the nibble with its L/H bit) and checks afterwards.
| A turn of either wait is 28 clocks.
xe1ap_read:
        lea     nib,a1
        move.b  #0x40,6(a0)             /* control: TH output, the rest inputs */
        move.b  #0x40,(a0)              /* REQ high ... */
        nop
        nop
        move.b  #0x00,(a0)              /* ... and low: the report starts */
        moveq   #20-1,d2
        move.w  #200,d1                 /* the first nibble comes after ~50 us */
1:      move.b  (a0),d0                 /* wait for /ACK low */
        btst    #5,d0
        dbeq    d1,1b
        bne.s   3f                      /* no more nibbles */
        move.b  d0,(a1)+                /* the nibble and its L/H */
        move.w  #20,d1
2:      move.b  (a0),d0                 /* wait for /ACK high */
        btst    #5,d0
        dbne    d1,2b
        move.w  #20,d1                  /* the next one is due within ~20 us */
        dbf     d2,1b
3:      move.b  #0x40,(a0)              /* REQ high: idle */
        | keep the nibbles while L/H goes 0, 1, 0, 1 ...
        move.l  a1,d2
        lea     nib,a1
        sub.l   a1,d2                   /* stored */
        moveq   #0,d0                   /* checked */
4:      cmp.w   d2,d0
        bhs.s   5f
        move.b  (a1),d1
        lsr.b   #4,d1
        eor.b   d0,d1
        btst    #0,d1
        bne.s   5f                      /* a nibble was missed: stop here */
        andi.b  #0x0F,(a1)+
        addq.w  #1,d0
        bra.s   4b
5:      rts

| nib[] (d0 nibbles) -> d2 (buttons, SH-2 pad format) and an_words[]
xe1ap_pack:
        lea     nib,a1
        moveq   #0,d3                   /* extension: 1 */
        cmp.w   #20,d0
        bne.s   1f
        cmp.b   #3,12(a1)
        bne.s   1f
        cmp.b   #2,13(a1)
        bne.s   1f
        moveq   #1,d3
1:
        | the axes: X = nib2:nib6, Y = nib3:nib7, RZ = nib4:nib8, Z = nib5:nib9
        move.b  2(a1),d4                /* left stick X */
        lsl.b   #4,d4
        or.b    6(a1),d4
        move.b  3(a1),d5                /* left stick Y */
        lsl.b   #4,d5
        or.b    7(a1),d5
        moveq   #0,d0
        move.b  d4,d0
        lsr.w   #1,d0
        lsl.w   #7,d0
        moveq   #0,d1
        move.b  d5,d1
        lsr.w   #1,d1
        or.w    d1,d0
        move.w  d0,an_words
        move.b  4(a1),d0                /* right stick X (channel 3) */
        lsl.b   #4,d0
        or.b    8(a1),d0
        moveq   #-128,d1                /* right stick Y: centre without the extension */
        tst.b   d3
        beq.s   2f
        move.b  14(a1),d1
        lsl.b   #4,d1
        or.b    15(a1),d1
2:      andi.w  #0xFF,d0
        lsr.w   #1,d0
        lsl.w   #7,d0
        andi.w  #0xFF,d1
        lsr.w   #1,d1
        or.w    d1,d0
        or.w    #0x4000,d0
        move.w  d0,an_words+2
        move.b  5(a1),d6                /* throttle Z */
        lsl.b   #4,d6
        or.b    9(a1),d6
        moveq   #0,d0
        move.b  d6,d0
        lsr.w   #1,d0
        lsl.w   #7,d0
        tst.b   d3
        beq.s   3f
        or.w    #0x40,d0
3:      or.w    #0x8000,d0
        move.w  d0,an_words+4

        | buttons (0 = pressed in the report): nibble 0 = E1 E2 START SELECT,
        | nibble 1 = A B C D
        move.w  #0x2000,d2              /* the analogue pad */
        move.b  1(a1),d0
        btst    #3,d0
        bne.s   4f
        or.w    #0x0040,d2              /* A */
4:      btst    #2,d0
        bne.s   4f
        or.w    #0x0010,d2              /* B */
4:      btst    #1,d0
        bne.s   4f
        or.w    #0x0020,d2              /* C */
4:      btst    #0,d0
        bne.s   4f
        or.w    #0x0400,d2              /* D: X */
4:      move.b  0(a1),d0
        btst    #3,d0
        bne.s   4f
        or.w    #0x0200,d2              /* E1: Y */
4:      btst    #2,d0
        bne.s   4f
        or.w    #0x0100,d2              /* E2: Z */
4:      btst    #1,d0
        bne.s   4f
        or.w    #0x0080,d2              /* START */
4:      btst    #0,d0
        bne.s   4f
        or.w    #0x0800,d2              /* SELECT: MODE */
4:
        | the stick as the d-pad, for the menus
        cmp.b   #80,d4
        bhs.s   5f
        or.w    #0x0004,d2              /* left */
5:      cmp.b   #176,d4
        bls.s   5f
        or.w    #0x0008,d2              /* right */
5:      cmp.b   #80,d5
        bhs.s   5f
        or.w    #0x0001,d2              /* up */
5:      cmp.b   #176,d5
        bls.s   5f
        or.w    #0x0002,d2              /* down */
5:
        | triggers: from the extension, else from the throttle (Z is
        | 128 + (right - left) / 2 when the pad's triggers drive it)
        tst.b   d3
        beq.s   6f
        move.b  16(a1),d0
        lsl.b   #4,d0
        or.b    17(a1),d0
        cmp.b   #64,d0
        bls.s   5f
        or.w    #0x4000,d2              /* left trigger */
5:      move.b  18(a1),d0
        lsl.b   #4,d0
        or.b    19(a1),d0
        cmp.b   #64,d0
        bls.s   7f
        or.w    #0x8000,d2              /* right trigger */
        rts
6:      cmp.b   #96,d6
        bhs.s   5f
        or.w    #0x4000,d2
5:      cmp.b   #160,d6
        bls.s   7f
        or.w    #0x8000,d2
7:      rts

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
an_words:   .word   0x2040, 0x6040, 0xA000   /* the axes for COMM2, centred */
an_idx:     .word   0
nib:        .space  20                  /* the XE-1 AP's last report */
pad_xe:     .byte   0                   /* port 1 holds an XE-1 AP */
        .even
cur_set:    .long   0                   /* ROM address of the loaded set's entry */
gun_req:    .word   0x00FF
gun_dirty:  .byte   0
pal_dirty:  .byte   0                   /* the loaded set's palettes are not in CRAM yet */
vbl_flag:   .byte   0
        .even

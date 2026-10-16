| Mega Drive (68000) side for Quake 32X.
| Linked at 0x880800 (ROM offset 0x800, visible to the 68K while RV=0).
| The 68000 only sets up the Mega Drive, releases the SH-2s and then reads
| the joypad once per frame into COMM12 (0xA1512C). It sleeps with STOP
| between frames so it stays off the cartridge bus the SH-2s depend on.

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

| 0x8808C0 - level 6 (VBlank): read pad 1 into COMM12, frame count in COMM14
        .org    0x0C0
        movem.l d0-d2/a0,-(sp)
        move.w  (0xC00004),d0           /* ack VDP interrupt */
        lea     0xA10003,a0
        bsr.w   get_pad
        move.w  d2,0xA1512C
        addq.w  #1,0xA1512E
        movem.l (sp)+,d0-d2/a0
        rte

| 0x880900 - level 2 (EXT)
        .org    0x100
        rte

main_start:
        move.w  #0x2700,sr
        lea     0x00FFFFE0,sp

        | Mega Drive VDP: H40, V28, display on, vblank IRQ on
        lea     0xC00004,a0
        move.w  (a0),d0                 /* reset VDP write latch */
        move.w  #0x8004,(a0)            /* no HBL int, enable HV counter */
        move.w  #0x8230,(a0)            /* plane A at 0xC000 */
        move.w  #0x832C,(a0)
        move.w  #0x8407,(a0)            /* plane B at 0xE000 */
        move.w  #0x8554,(a0)
        move.w  #0x8700,(a0)            /* backdrop colour 0 */
        move.w  #0x8B00,(a0)
        move.w  #0x8C81,(a0)            /* H40 */
        move.w  #0x8D2B,(a0)
        move.w  #0x8F02,(a0)
        move.w  #0x9001,(a0)

        | MD CRAM entry 0 = black (backdrop behind the 32X layer)
        move.l  #0xC0000000,(a0)
        move.w  #0,0xC00000

        | joypad ports: TH as output
        move.b  #0x40,0xA10009
        move.b  #0x40,0xA1000B
        move.b  #0x40,0xA10003
        move.b  #0x40,0xA10005

        | let the SH-2s at the cartridge
        move.w  #0,0xA15104             /* bank select 0 */
        move.b  #0,0xA15107             /* clear RV */
1:
        cmp.l   #0x4D5F4F4B,0xA15120    /* "M_OK" */
        bne.b   1b
2:
        cmp.l   #0x535F4F4B,0xA15124    /* "S_OK" */
        bne.b   2b

        move.w  #0,0xA1512C             /* no buttons */
        move.w  #0,0xA1512E

        move.w  0xA15100,d0
        or.w    #0x8000,d0
        move.w  d0,0xA15100             /* FM = 1: SH-2s own the 32X VDP */
        move.l  #0,0xA15120             /* release the master SH-2 */

        move.w  #0x8174,0xC00004        /* display on, vblank int on */
        move.w  #0x2000,sr
3:
        stop    #0x2000                 /* sleep until next interrupt */
        bra.b   3b

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

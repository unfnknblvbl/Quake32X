!-----------------------------------------------------------------------
! Hand-written SH-2 inner loops. This block is position independent and
! is copied into each CPU's on-chip RAM (0xC0000000, two-way cache mode)
! at start-up, so it never misses the cache.
!
! void span_tex(texargs_t *a)
!   Perspective-correct texture mapping of a span list from a lit surface,
!   subdivided every 16 pixels. The 1/z divide for the start of the next
!   span runs on the hardware divider while the current span is drawn.
!-----------------------------------------------------------------------

        .section .text
        .align  4
        .global _oram_code_start
_oram_code_start:

        /* texargs_t offsets (keep in sync with r_local.h) */
        .equ    TA_DZIDX,   0
        .equ    TA_DSZDX,   4
        .equ    TA_DTZDX,   8
        .equ    TA_SMAX,    12
        .equ    TA_TMAX,    16
        .equ    TA_SRC,     20
        .equ    TA_WIDTH,   24
        .equ    TA_DBL,     28
        .equ    TA_LEFT,    32
        .equ    TA_SN,      36
        .equ    TA_TN,      40
        .equ    TA_SPANEND, 44
        .equ    TA_FB,      48
        .equ    TA_ZI0,     52
        .equ    TA_SZ0,     56
        .equ    TA_TZ0,     60
        .equ    TA_DZIDY,   64      /* offsets >= 64 need r0-indexed loads */
        .equ    TA_DSZDY,   68
        .equ    TA_DTZDY,   72
        .equ    TA_SPANS,   76
        .equ    TA_NSPANS,  80

        /* r13 = span pointer: start the divide for the span's first pixel.
           r9 = zi, r10 = sz, r11 = tz on exit; clobbers r0, r3, r6 */
        .macro  SPANSTART
        mov.b   @r13,r0
        extu.b  r0,r2                   /* y */
        mov.b   @(1,r13),r0
        extu.b  r0,r1                   /* x */
        mov     #TA_DZIDY,r0
        mov.l   @(r0,r14),r3
        mul.l   r2,r3
        mov.l   @(TA_DZIDX,r14),r3
        sts     macl,r9
        mul.l   r1,r3
        mov.l   @(TA_ZI0,r14),r3
        add     r3,r9
        sts     macl,r3
        add     r3,r9                   /* zi */
        mov.l   .Lzimin,r3
        cmp/ge  r3,r9
        bt      91f
        mov     r3,r9
91:     mov.l   .Ldivu,r6
        mov.l   r9,@r6                  /* DVSR = zi */
        mov.l   .Lone,r3
        mov.l   r3,@(16,r6)             /* DVDNTH = 1 << 16 */
        mov     #0,r3
        mov.l   r3,@(20,r6)             /* DVDNTL = 0: starts 2^48 / zi */
        mov     #TA_DSZDY,r0
        mov.l   @(r0,r14),r3
        mul.l   r2,r3
        mov.l   @(TA_DSZDX,r14),r3
        sts     macl,r10
        mul.l   r1,r3
        mov.l   @(TA_SZ0,r14),r3
        add     r3,r10
        sts     macl,r3
        add     r3,r10                  /* sz */
        mov     #TA_DTZDY,r0
        mov.l   @(r0,r14),r3
        mul.l   r2,r3
        mov.l   @(TA_DTZDX,r14),r3
        sts     macl,r11
        mul.l   r1,r3
        mov.l   @(TA_TZ0,r14),r3
        add     r3,r11
        sts     macl,r3
        add     r3,r11                  /* tz */
        .endm

        /* rD = clamp((rZ * r12) >> 24, 0, limit at @(OFF,r14)); clobbers r0 */
        .macro  PERSP rz, rd, off
        dmuls.l \rz,r12
        sts     mach,r0
        sts     macl,\rd
        shll8   r0
        shlr16  \rd
        shlr8   \rd
        or      r0,\rd
        mov     #1,r0
        shll8   r0                      /* low clamp 1/256 texel: keeps the */
        cmp/ge  r0,\rd                  /* interpolated values >= 0 despite */
        bt      92f                     /* step rounding */
        mov     r0,\rd
92:     mov.l   @(\off,r14),r0
        cmp/gt  r0,\rd
        bf      93f
        mov     r0,\rd
93:
        .endm

        .global _span_tex
_span_tex:
        stc.l   gbr,@-r15
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   r13,@-r15
        mov.l   r14,@-r15
        mov     r4,r14                  /* r14 = args */
        mov     #TA_NSPANS,r0
        mov.l   @(r0,r14),r1
        mov     #TA_SPANS,r0
        mov.l   @(r0,r14),r13           /* r13 = first span */
        shll2   r1
        add     r13,r1
        mov.l   r1,@(TA_SPANEND,r14)
        SPANSTART

.Lspan:
        /* r1 = x, r2 = y (from SPANSTART); count and destination */
        mov.b   @(2,r13),r0
        extu.b  r0,r8
        sub     r1,r8                   /* count */
        mov.l   r8,@(TA_LEFT,r14)
        mov     r2,r0
        shll2   r0
        add     r2,r0                   /* y*5 */
        shll2   r0
        shll2   r0
        shll2   r0                      /* y*320 */
        add     r1,r0
        add     r1,r0
        mov.l   @(TA_FB,r14),r7
        add     r0,r7                   /* dst */
        add     #4,r13

        mov.l   .Ldivu,r6
        mov.l   @(20,r6),r12            /* z16 for the span start */
        PERSP   r10, r1, TA_SMAX        /* s */
        PERSP   r11, r2, TA_TMAX        /* t */

.Lseg:
        mov.l   @(TA_LEFT,r14),r8
        mov     #16,r0
        cmp/gt  r0,r8
        bt      .Lfull
        mov     r8,r5
        add     #-1,r5                  /* m = n - 1 */
        tst     r5,r5
        bf      2f
        mov     #0,r3                   /* single pixel */
        bra     .Lprep
        mov     #0,r4
2:      mov.l   @(TA_DZIDX,r14),r3
        mul.l   r5,r3
        sts     macl,r3
        add     r3,r9
        mov.l   @(TA_DSZDX,r14),r3
        mul.l   r5,r3
        sts     macl,r3
        add     r3,r10
        mov.l   @(TA_DTZDX,r14),r3
        mul.l   r5,r3
        sts     macl,r3
        bra     .Lend
        add     r3,r11

.Lfull:
        mov     #16,r5
        mov.l   @(TA_DZIDX,r14),r3
        shll2   r3
        shll2   r3
        add     r3,r9
        mov.l   @(TA_DSZDX,r14),r3
        shll2   r3
        shll2   r3
        add     r3,r10
        mov.l   @(TA_DTZDX,r14),r3
        shll2   r3
        shll2   r3
        add     r3,r11

.Lend:
        /* endpoint divide */
        mov.l   .Lzimin,r3
        cmp/ge  r3,r9
        bt      1f
        mov     r3,r9
1:      mov.l   .Ldivu,r6
        mov.l   r9,@r6
        mov.l   .Lone,r3
        mov.l   r3,@(16,r6)
        mov     #0,r3
        mov.l   r3,@(20,r6)
        mov.l   @(20,r6),r12
        PERSP   r10, r3, TA_SMAX
        PERSP   r11, r4, TA_TMAX
        mov.l   r3,@(TA_SN,r14)
        mov.l   r4,@(TA_TN,r14)
        sub     r1,r3
        sub     r2,r4
        mov     #16,r0
        cmp/eq  r0,r5
        bf      3f
        shar    r3
        shar    r3
        shar    r3
        shar    r3
        shar    r4
        shar    r4
        shar    r4
        bra     .Lprep
        shar    r4
3:      mova    .Lrecip,r0
        shll2   r5
        mov.l   @(r0,r5),r12
        dmuls.l r3,r12
        sts     mach,r0
        sts     macl,r3
        xtrct   r0,r3
        dmuls.l r4,r12
        sts     mach,r0
        sts     macl,r4
        xtrct   r0,r4

.Lprep:
        /* pixels this piece: min(16, left) */
        mov.l   @(TA_LEFT,r14),r8
        mov     #16,r0
        cmp/gt  r0,r8
        bf      1f
        mov     r0,r8
1:      mov.l   @(TA_LEFT,r14),r0
        sub     r8,r0
        mov.l   r0,@(TA_LEFT,r14)
        tst     r0,r0
        bf      4f
        /* last piece of this span: start the next span's divide now so it
           overlaps with drawing */
        mov.l   @(TA_SPANEND,r14),r0
        cmp/hs  r0,r13
        bt      4f
        mov.l   r1,@-r15
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        SPANSTART
        mov.l   r1,@(TA_SN,r14)         /* stash next span x, y */
        mov.l   r2,@(TA_TN,r14)
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        mov.l   @r15+,r1
4:      ldc     r9,gbr                  /* park zi; r9 is the loop temp */
        mov.l   @(TA_SRC,r14),r5
        mov.l   @(TA_WIDTH,r14),r6
        mov.l   @(TA_DBL,r14),r12

.Lpix:
        mov     r2,r0
        shlr16  r0
        mulu.w  r0,r6
        mov     r1,r0
        shlr16  r0
        add     r3,r1
        add     r4,r2
        sts     macl,r9
        add     r9,r0
        mov.b   @(r0,r5),r0
        dt      r8
        add     r0,r0                   /* (not shll: that would clobber T) */
        mov.w   @(r0,r12),r0
        mov.w   r0,@r7
        bf.s    .Lpix
        add     #2,r7

        stc     gbr,r9
        mov.l   @(TA_LEFT,r14),r0
        tst     r0,r0
        bt      .Lspandone
        mov.l   @(TA_SN,r14),r1
        bra     .Lseg
        mov.l   @(TA_TN,r14),r2

.Lspandone:
        mov.l   @(TA_SPANEND,r14),r0
        cmp/hs  r0,r13
        bt      .Lexit
        mov.l   @(TA_SN,r14),r1         /* next span's x, y */
        bra     .Lspan
        mov.l   @(TA_TN,r14),r2

.Lexit:
        mov.l   @r15+,r14
        mov.l   @r15+,r13
        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        mov.l   @r15+,r8
        ldc.l   @r15+,gbr
        rts
        nop

        .align  2
.Lzimin: .long  0x00040000
.Ldivu:  .long  0xFFFFFF00
.Lone:   .long  0x00010000
.Lrecip: .long  0, 65536, 32768, 21845, 16384, 13107, 10923, 9362, 8192
         .long  7282, 6554, 5958, 5461, 5041, 4681, 4369, 4096

        .global _oram_code_end
_oram_code_end:

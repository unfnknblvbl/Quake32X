/*-----------------------------------------------------------------------
 * Stage C in SH-2 assembly: one convex screen polygon against the per-row
 * coverage gaps, emitting spans (r_geom.c has the C version, used by the
 * host build, and the comments there explain the steps).
 *
 * void raster_solid(rcpu_t *rc, int n, const fixed_t *sx, const fixed_t *sy)
 *
 * Bit for bit the same as raster_poly(..., mesh = 0): the same edge
 * stepping (hardware 64/32 divides, the same rounding), the same gap list
 * updates and the same span order.
 *---------------------------------------------------------------------*/
#include "asmoff.h"

        .section .text
        .align  2

/* the polygon side being walked (on the stack: A at 0, B at 32) */
        .equ    CH_I,    0
        .equ    CH_X,    4
        .equ    CH_DXDY, 8
        .equ    CH_REND, 12
        .equ    CH_SX,   16
        .equ    CH_SY,   20
        .equ    CH_N,    24
        .equ    FR_RBOT, 28             /* (in A's spare slot) */
        .equ    FRAME,   64

/* emit a span y = r12, [xa, xb) at r9 (r11 = end of the buffer); r7 temp */
        .macro  EMIT xa, xb
        cmp/hs  r11,r9
        bf      81f
        sts.l   pr,@-r15
        bsr     Lovf
        nop
        lds.l   @r15+,pr
81:     mov     r12,r7
        shll8   r7
        or      \xa,r7
        shll8   r7
        or      \xb,r7
        shll8   r7
        mov.l   r7,@r9
        add     #4,r9
        .endm

        .global _raster_solid
_raster_solid:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   r13,@-r15
        mov.l   r14,@-r15
        sts.l   pr,@-r15
        add     #-FRAME,r15
        mov     r4,r14
        mov.l   r6,@(CH_SX,r15)
        mov.l   r7,@(CH_SY,r15)
        mov.l   r5,@(CH_N,r15)
        mov.l   r6,@(32+CH_SX,r15)
        mov.l   r7,@(32+CH_SY,r15)
        mov.l   r5,@(32+CH_N,r15)

        /* bounds and the top vertex */
        mov.l   @r7,r8                  /* ymin */
        mov     r8,r9                   /* ymax */
        mov.l   @r6,r10                 /* xmin */
        mov     r10,r11                 /* xmax */
        mov     #0,r12                  /* itop */
        mov     #1,r13                  /* i */
.Lb_loop:
        cmp/ge  r5,r13
        bt      .Lb_done
        mov     r13,r0
        shll2   r0
        mov.l   @(r0,r7),r1             /* sy[i] */
        mov.l   @(r0,r6),r2             /* sx[i] */
        cmp/gt  r1,r8
        bf      1f
        mov     r1,r8
        mov     r13,r12
1:      cmp/gt  r9,r1
        bf      2f
        mov     r1,r9
2:      cmp/gt  r2,r10
        bf      3f
        mov     r2,r10
3:      cmp/gt  r11,r2
        bf      4f
        mov     r2,r11
4:      bra     .Lb_loop
        add     #1,r13
.Lb_done:
        mov.l   r12,@(CH_I,r15)
        mov.l   r12,@(32+CH_I,r15)
        mov.l   .Lk7fff,r0
        add     r0,r8
        shlr16  r8                      /* rtop (all screen values are >= 0) */
        add     r0,r9
        shlr16  r9                      /* rbot */
        add     r0,r10
        shlr16  r10                     /* xs */
        add     r0,r11
        shlr16  r11                     /* xe */
        mov.l   @(RC_ROW0,r14),r1
        cmp/ge  r1,r8
        bt      5f
        mov     r1,r8
5:      mov.l   @(RC_ROW1,r14),r1
        cmp/gt  r1,r9
        bf      6f
        mov     r1,r9
6:
        /* occlusion pre-test: trim rows whose gaps miss [xs, xe) */
.Lt_top:
        cmp/ge  r9,r8
        bt      .Lret
        bsr     Lrowopen
        mov     r8,r1
        tst     r0,r0
        bf      .Lt_bot
        bra     .Lt_top
        add     #1,r8
.Lt_bot:                                /* row rtop is open: this ends */
        mov     r9,r1
        bsr     Lrowopen
        add     #-1,r1
        tst     r0,r0
        bf      .Lt_done
        bra     .Lt_bot
        add     #-1,r9
.Lt_done:
        mov.l   r9,@(FR_RBOT,r15)
        mov     r8,r12                  /* r = rtop */

        /* the two sides from the top vertex */
        mov     r15,r4
        mov     #-1,r5
        bsr     Lchain
        mov     r12,r6
        tst     r0,r0
        bt      .Lret
        mov     r15,r4
        add     #32,r4
        mov     #1,r5
        bsr     Lchain
        mov     r12,r6
        tst     r0,r0
        bt      .Lret

        /* spans go to r9 .. r11 (rc->spans + nspans, rc->spans + MAXSPANS) */
        mov.l   .Lrc_nspans,r0
        mov.l   @(r0,r14),r9
        shll2   r9
        mov.l   .Lrc_spans,r0
        add     r14,r0
        add     r0,r9
        mov.l   .Lspans_bytes,r11
        add     r0,r11
        mov.l   @(CH_X,r15),r8          /* a.x */
        mov.l   @(32+CH_X,r15),r10      /* b.x */

.Lseg:  /* rend = min(a.rend, b.rend, rbot) */
        mov.l   @(CH_REND,r15),r13
        mov.l   @(32+CH_REND,r15),r0
        cmp/gt  r0,r13
        bf      1f
        mov     r0,r13
1:      mov.l   @(FR_RBOT,r15),r0
        cmp/gt  r0,r13
        bf      2f
        mov     r0,r13
2:      cmp/ge  r13,r12
        bt      .Lrows_done
.Lrow:
        mov     #GAPROW_SIZE,r1
        mulu.w  r12,r1
        mov.l   .Lgaps,r4
        sts     macl,r1
        add     r1,r4                   /* &gaps[r] */
        mov.b   @r4,r0
        tst     #255,r0
        bt      .Lrow_next              /* no gaps left on this row */
        mov     r8,r5                   /* xl */
        mov     r10,r6                  /* xr */
        cmp/gt  r6,r5
        bf      3f
        mov     r10,r5
        mov     r8,r6
3:      mov.l   .Lk7fff,r0
        add     r0,r5
        shlr16  r5                      /* xs */
        add     r0,r6
        shlr16  r6                      /* xe */
        mov.l   .Lview_vw,r0
        mov.l   @r0,r0
        cmp/gt  r0,r6
        bf      4f
        mov     r0,r6
4:      cmp/gt  r5,r6
        bf      .Lrow_next
        bsr     Lcover
        nop
.Lrow_next:
        mov.l   @(CH_DXDY,r15),r0
        add     r0,r8
        mov.l   @(32+CH_DXDY,r15),r0
        add     r0,r10
        add     #1,r12
        cmp/ge  r13,r12
        bf      .Lrow
.Lrows_done:
        mov.l   @(FR_RBOT,r15),r0
        cmp/ge  r0,r12
        bt      .Ldone
        mov.l   @(CH_REND,r15),r0
        cmp/gt  r12,r0
        bt      5f
        mov     r15,r4
        mov     #-1,r5
        bsr     Lchain
        mov     r12,r6
        tst     r0,r0
        bt      .Ldone
        mov.l   @(CH_X,r15),r8
5:      mov.l   @(32+CH_REND,r15),r0
        cmp/gt  r12,r0
        bt      .Lseg
        mov     r15,r4
        add     #32,r4
        mov     #1,r5
        bsr     Lchain
        mov     r12,r6
        tst     r0,r0
        bt      .Ldone
        bra     .Lseg
        mov.l   @(32+CH_X,r15),r10

.Ldone: /* rc->nspans = (r9 - rc->spans) / 4 */
        mov.l   .Lrc_spans,r0
        add     r14,r0
        sub     r0,r9
        shlr2   r9
        mov.l   .Lrc_nspans,r0
        mov.l   r9,@(r0,r14)
.Lret:
        add     #FRAME,r15
        lds.l   @r15+,pr
        mov.l   @r15+,r14
        mov.l   @r15+,r13
        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

        .align  2
.Lk7fff:        .long   0x7FFF
.Lgaps:         .long   _gaps
.Lview_vw:      .long   _view + VIEW_VW
.Lrc_spans:     .long   RC_SPANS
.Lrc_nspans:    .long   RC_NSPANS
.Lspans_bytes:  .long   MAXSPANS_ * 4

/* r0 = 1 if row r1 has a gap overlapping [r10, r11); clobbers r2-r5 */
Lrowopen:
        mov     #GAPROW_SIZE,r2
        mulu.w  r1,r2
        mov.l   .Lgaps2,r3
        sts     macl,r2
        add     r2,r3
        mov.b   @r3+,r4
        extu.b  r4,r4
1:      tst     r4,r4
        bt      3f
        mov.b   @r3+,r5
        extu.b  r5,r5                   /* gs */
        mov.b   @r3+,r2
        extu.b  r2,r2                   /* ge */
        cmp/ge  r11,r5                  /* gs >= xe */
        bt      2f
        cmp/gt  r10,r2                  /* ge > xs */
        bt      4f
2:      bra     1b
        add     #-1,r4
3:      rts
        mov     #0,r0
4:      rts
        mov     #1,r0

/* chain_next: advance the side at r4 (direction r5 = -1 / +1) to the next
 * edge that reaches below row r6; r0 = 0 if there is none. Preserves r4,
 * r6 and r8-r14. */
Lchain:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   @(CH_N,r4),r8           /* guard = n */
        mov.l   @(CH_I,r4),r1           /* i0 */
.Lc_top:
        cmp/pl  r8
        bf      .Lc_fail
        add     #-1,r8
        mov     r1,r2
        add     r5,r2                   /* i1 */
        cmp/pz  r2
        bt      1f
        mov.l   @(CH_N,r4),r2
        bra     2f
        add     #-1,r2
1:      mov.l   @(CH_N,r4),r0
        cmp/ge  r0,r2
        bf      2f
        mov     #0,r2
2:      mov.l   r2,@(CH_I,r4)           /* c->i = i1 */
        mov.l   @(CH_SY,r4),r0
        mov     r1,r3
        shll2   r3
        mov.l   @(r0,r3),r3             /* y0 */
        mov     r2,r7
        shll2   r7
        mov.l   @(r0,r7),r7             /* y1 */
        cmp/gt  r7,r3                   /* y1 < y0: the side turns up */
        bt      .Lc_fail
        mov.l   .Lk7fff2,r9
        add     r7,r9
        shlr16  r9                      /* r1 = rows it reaches */
        cmp/gt  r6,r9
        bf      .Lc_next
        cmp/eq  r3,r7
        bt      .Lc_next
        mov.l   @(CH_SX,r4),r0
        shll2   r1
        mov.l   @(r0,r1),r1             /* x0 */
        shll2   r2
        mov.l   @(r0,r2),r2             /* x1 */
        mov     r2,r0
        sub     r1,r0                   /* dx */
        sub     r3,r7                   /* dy */
        mov.l   .Ldvsr2,r5
        mov.l   r7,@r5                  /* DVSR = dy */
        mov     r0,r7
        shlr16  r7
        exts.w  r7,r7
        mov.l   r7,@(16,r5)             /* DVDNTH = dx >> 16 */
        shll16  r0
        mov.l   r0,@(20,r5)             /* DVDNTL = dx << 16: divides */
        mov     r6,r7
        shll16  r7
        mov.l   .Lk80002,r0
        add     r0,r7
        sub     r3,r7                   /* (r << 16) + 0x8000 - y0 */
        mov.l   @(20,r5),r0             /* dxdy */
        mov.l   r0,@(CH_DXDY,r4)
        dmuls.l r7,r0
        sts     mach,r7
        sts     macl,r0
        xtrct   r7,r0                   /* fixmul */
        add     r1,r0                   /* x */
        cmp/gt  r2,r1
        bf      3f
        mov     r1,r3
        mov     r2,r1
        mov     r3,r2                   /* r1 = lo, r2 = hi */
3:      cmp/ge  r1,r0
        bt      4f
        mov     r1,r0
4:      cmp/gt  r2,r0
        bf      5f
        mov     r2,r0
5:      mov.l   r0,@(CH_X,r4)
        mov.l   r9,@(CH_REND,r4)
        mov     #1,r0
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8
.Lc_next:
        bra     .Lc_top
        mov     r2,r1
.Lc_fail:
        mov     #0,r0
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

/* cover_row: [r5, r6) on row r12 (gap row at r4) against its gaps, emitting
 * spans at r9; clobbers r0-r3 and r7 */
Lcover:
        mov.b   @r4,r2
        extu.b  r2,r2
        add     r2,r2
        mov     r4,r3
        add     #1,r3                   /* p: the first gap */
        add     r3,r2                   /* pend */
.Lcv_loop:
        cmp/hs  r2,r3
        bt      .Lcv_end
        mov.b   @(1,r3),r0
        extu.b  r0,r1                   /* ge */
        mov.b   @r3,r0
        extu.b  r0,r0                   /* gs */
        cmp/gt  r5,r1
        bt      1f
        bra     .Lcv_loop               /* ge <= xs: the next gap */
        add     #2,r3
1:      cmp/ge  r6,r0
        bt      .Lcv_end                /* gs >= xe: done */
        cmp/gt  r0,r5
        bt      .Lcv_right              /* xs > gs */
        cmp/ge  r1,r6
        bf      .Lcv_left               /* xe < ge */
        /* the whole gap is covered: emit it and remove it */
        EMIT    r0, r1
        mov     r3,r7
        add     #-2,r2
2:      cmp/hs  r2,r7
        bt      .Lcv_loop               /* (the same p: the next gap moved down) */
        mov.b   @(2,r7),r0
        mov.b   r0,@r7
        bra     2b
        add     #1,r7
.Lcv_left:                              /* its left part is covered */
        EMIT    r0, r6
        mov     r6,r0
        bra     .Lcv_end
        mov.b   r0,@r3
.Lcv_right:
        cmp/ge  r1,r6
        bf      .Lcv_mid
        /* its right part is covered */
        EMIT    r5, r1
        mov     r5,r0
        mov.b   r0,@(1,r3)
        bra     .Lcv_loop
        add     #2,r3
.Lcv_mid:                               /* the middle: split the gap */
        EMIT    r5, r6
        mov     r2,r0
        sub     r4,r0
        add     #-1,r0                  /* 2n */
        mov     #MAXGAPS_ * 2,r7
        cmp/ge  r7,r0
        bt      .Lcv_full
        mov     r2,r7
        add     #-1,r7                  /* q = pend - 1 */
3:      mov     r3,r0
        add     #2,r0
        cmp/hs  r0,r7                   /* q >= p + 2 */
        bf      4f
        mov.b   @r7,r0
        mov.b   r0,@(2,r7)
        bra     3b
        add     #-1,r7
4:      mov     r6,r0
        mov.b   r0,@(2,r3)
        mov     r1,r0
        mov.b   r0,@(3,r3)
        add     #2,r2
.Lcv_setend:
        mov     r5,r0
        mov.b   r0,@(1,r3)
.Lcv_end:
        mov     r2,r0
        sub     r4,r0
        add     #-1,r0
        shlr    r0                      /* n */
        mov.b   r0,@r4
        tst     r0,r0
        bf      5f
        mov.l   @(RC_ROWSLEFT,r14),r0
        add     #-1,r0
        mov.l   r0,@(RC_ROWSLEFT,r14)
5:      rts
        nop
.Lcv_full:                              /* out of gap slots: paint [xe, ge) black */
        sts.l   pr,@-r15
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov     r1,r7
        mov     r12,r5
        mov.l   .Lcover_black2,r0
        jsr     @r0
        mov     r14,r4
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        lds.l   @r15+,pr
        bra     .Lcv_setend
        nop

/* the span buffer is full: draw it (span_overflow) and go on; preserves
 * everything but r7 */
Lovf:
        sts.l   pr,@-r15
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   r2,@-r15
        mov.l   r3,@-r15
        mov.l   r4,@-r15
        mov.l   r5,@-r15
        mov.l   r6,@-r15
        mov.l   .Lrc_spans2,r0
        add     r14,r0
        mov     r9,r1
        sub     r0,r1
        shlr2   r1
        mov.l   .Lrc_nspans2,r0
        mov.l   r1,@(r0,r14)
        mov.l   .Lspan_overflow2,r0
        jsr     @r0
        mov     r14,r4
        mov.l   .Lrc_nspans2,r0
        mov.l   @(r0,r14),r9
        shll2   r9
        mov.l   .Lrc_spans2,r0
        add     r14,r0
        add     r0,r9
        mov.l   @r15+,r6
        mov.l   @r15+,r5
        mov.l   @r15+,r4
        mov.l   @r15+,r3
        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        lds.l   @r15+,pr
        rts
        nop

        .align  2
.Lrc_spans2:     .long  RC_SPANS
.Lrc_nspans2:    .long  RC_NSPANS
.Lspan_overflow2: .long _span_overflow
.Lk7fff2:        .long  0x7FFF
.Lk80002:        .long  0x8000
.Lgaps2:         .long  _gaps
.Ldvsr2:         .long  0xFFFFFF00
.Lcover_black2:  .long  _r_cover_black

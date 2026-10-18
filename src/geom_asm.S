/*-----------------------------------------------------------------------
 * Stage B in SH-2 assembly: one candidate face to a screen polygon record
 * (r_geom.c has the C version, make_poly, used by the host build and for
 * the faces that need clipping; the results are the same bit for bit).
 *
 * int make_poly_asm(rcpu_t *rc, int fi, int clip, int32_t *out)
 *   The bounding sphere against the frustum planes in `clip`, then the
 *   vertices through the vertex cache (transformed, outcoded and projected
 *   on a miss), then the polygon record:
 *     out[0] = fi | n << 16, out[1] = zmin, out[2..4] = first vertex
 *     (view space), out[5..] = screen x, then screen y.
 *   Returns the words used, 0 if culled. A face that crosses a plane it
 *   must be clipped against goes on in C (make_poly_clip).
 *---------------------------------------------------------------------*/
#include "asmoff.h"

        .section .text
        .align  2

        .equ    F_OUT,  0
        .equ    F_FI,   4
        .equ    F_CLIP, 8
        .equ    F_N,    12
        .equ    FRAME,  16

        .global _make_poly_asm
_make_poly_asm:
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
        mov.l   r7,@(F_OUT,r15)
        mov.l   r5,@(F_FI,r15)
        mov     r6,r12                  /* clip */
        stc     sr,r0                   /* MAC.W must not saturate: S = 0 */
        mov     #-3,r1
        and     r1,r0
        ldc     r0,sr
        mov.l   .Llvl_fgeom,r0
        mov.l   @r0,r10
        mov     r5,r0
        shll2   r0
        shll2   r0
        add     r0,r10                  /* g = &lvl.fgeom[fi] */
        mov.b   @(2,r10),r0
        extu.b  r0,r8                   /* n */
        mov     #3,r0
        cmp/ge  r0,r8
        bf      .Lbad
        mov     #18,r0                  /* MAXCLIPVERTS - CP_NUM */
        cmp/gt  r0,r8
        bt      .Lbad
        tst     r12,r12
        bf      .Lsphere
        bra     .Lverts
        nop
.Lbad:  bra     .Lexit
        mov     #0,r0
.Lsphere:

        /* the bounding sphere against the planes still in clip */
        mov     r14,r1
        add     #RC_FR_N,r1
        mov     r14,r2
        add     #RC_FR_D,r2
        mov     r14,r3
        add     #RC_FR_LEN,r3
        mov     #1,r6
.Ls_loop:
        tst     r6,r12
        bt      .Ls_next
        mov.w   @(8,r10),r0             /* sphere x */
        mov.l   @r1,r5
        mul.l   r0,r5
        mov.w   @(10,r10),r0
        mov.l   @(4,r1),r5
        sts     macl,r7
        mul.l   r0,r5
        mov.w   @(12,r10),r0
        mov.l   @(8,r1),r5
        sts     macl,r4
        mul.l   r0,r5
        add     r4,r7
        mov.l   @r2,r0
        sub     r0,r7
        sts     macl,r4
        add     r4,r7                   /* d */
        mov.w   @(14,r10),r0            /* r */
        mov.l   @r3,r5
        mul.l   r0,r5
        sts     macl,r5                 /* rr */
        mov     r5,r0
        neg     r0,r0
        cmp/gt  r7,r0                   /* d < -rr: outside */
        bt      .Lret0
        cmp/ge  r5,r7
        bf      .Ls_next
        mov     r6,r0                   /* d >= rr: inside this plane */
        not     r0,r0
        and     r0,r12
.Ls_next:
        add     #12,r1
        add     #4,r2
        add     #4,r3
        shll    r6
        mov     #64,r0
        cmp/hs  r0,r6
        bf      .Ls_loop

.Lverts:
        mov.l   r12,@(F_CLIP,r15)
        mov.l   r8,@(F_N,r15)
        mov.l   .Llvl_fverts,r0
        mov.l   @r0,r9
        mov.w   @r10,r0
        extu.w  r0,r0
        add     r0,r0
        add     r0,r9                   /* fv = &lvl.fverts[g->firstvert] */
        mov.l   @(F_OUT,r15),r13
        add     #20,r13                 /* &out[5]: sx[i] */
        mov     r8,r11
        shll2   r11                     /* sy[i] = sx[i] + n */
        mov     #63,r10                 /* ocand (bits 0-5), ocor (bits 8-13) */
        mov.l   .Lbig,r12               /* zmin */
.Lvloop:
        mov.w   @r9+,r5
        extu.w  r5,r5                   /* vi */
        extu.b  r5,r4
        shll2   r4
        shll2   r4
        shll    r4
        mov.l   .Lrc_vcache,r0
        add     r0,r4
        add     r14,r4                  /* e = &rc->vcache[vi & 255] */
        mov.l   .Lrc_vcframe,r0
        mov.l   @(r0,r14),r1
        or      r5,r1
        mov.l   @r4,r0
        cmp/eq  r0,r1
        bt      1f
        bsr     Lxform
        nop
1:      mov.l   @(VC_OC,r4),r1
        mov     #-64,r2
        or      r1,r2
        and     r2,r10                  /* ocand &= oc */
        mov     r1,r2
        shll8   r2
        or      r2,r10                  /* ocor |= oc */
        mov.l   @(VC_SX,r4),r2
        mov.l   r2,@r13
        mov.l   @(VC_SY,r4),r2
        mov     r11,r0
        mov.l   r2,@(r0,r13)
        mov.l   @(VC_V+8,r4),r2         /* z */
        cmp/gt  r2,r12
        bf      2f
        mov     r2,r12
2:      mov.l   @(F_OUT,r15),r0
        add     #20,r0
        cmp/eq  r0,r13
        bf      3f
        mov.l   @(F_OUT,r15),r0         /* the first vertex: out[2..4] */
        mov.l   @(VC_V,r4),r2
        mov.l   r2,@(8,r0)
        mov.l   @(VC_V+4,r4),r2
        mov.l   r2,@(12,r0)
        mov.l   @(VC_V+8,r4),r2
        mov.l   r2,@(16,r0)
3:      dt      r8
        bf/s    .Lvloop
        add     #4,r13

        mov.l   @(F_CLIP,r15),r6
        mov     r10,r0
        and     #63,r0
        tst     r6,r0
        bf      .Lret0                  /* all vertices outside one plane */
        mov     r10,r0
        shlr8   r0
        and     #63,r0
        tst     r6,r0
        bf      .Lclip                  /* it crosses a plane: clip in C */
        mov.l   .Lnearz,r0
        cmp/ge  r0,r12
        bt      4f
        mov     r0,r12
4:      mov.l   @(F_OUT,r15),r1
        mov.l   @(16,r1),r2
        cmp/ge  r0,r2
        bt      5f
        mov.l   r0,@(16,r1)
5:      mov.l   @(F_N,r15),r2
        mov     r2,r3
        shll16  r3
        mov.l   @(F_FI,r15),r0
        or      r0,r3
        mov.l   r3,@r1                  /* fi | n << 16 */
        mov.l   r12,@(4,r1)             /* zmin */
        mov     r2,r0
        add     r0,r0
        bra     .Lexit
        add     #5,r0                   /* 5 + 2n words */
.Lclip:
        and     r0,r6                   /* ocor & clip */
        mov     r14,r4
        mov.l   @(F_FI,r15),r5
        mov.l   @(F_OUT,r15),r7
        mov.l   .Lmake_poly_clip,r0
        jsr     @r0
        nop
        bra     .Lexit
        nop
.Lret0:
        mov     #0,r0
.Lexit:
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
.Llvl_fgeom:    .long   _lvl + LVL_FGEOM_
.Llvl_fverts:   .long   _lvl + LVL_FVERTS_
.Lrc_vcache:    .long   RC_VCACHE
.Lrc_vcframe:   .long   RC_VCFRAME
.Lbig:          .long   0x7FFFFFFF
.Lnearz:        .long   4 << 16
.Lmake_poly_clip: .long  _make_poly_clip

/* y (r5) against the top and bottom planes of this CPU's band at depth
   z (r6): sets bits 16 and 32 of r7; clobbers r0, r2 */
        .macro  TOPBOT
        mov.l   @(RC_KY0,r14),r0
        dmuls.l r0,r6
        sts     mach,r2
        sts     macl,r0
        xtrct   r2,r0                   /* fixmul(ky0, z) */
        cmp/gt  r5,r0
        bf      71f
        mov     #16,r0
        or      r0,r7
71:     mov.l   @(RC_KY1,r14),r0
        dmuls.l r0,r6
        sts     mach,r2
        sts     macl,r0
        xtrct   r2,r0                   /* fixmul(ky1, z) */
        cmp/gt  r0,r5
        bf      72f
        mov     #32,r0
        or      r0,r7
72:
        .endm

/* xform_vert: vertex r5 into the cache entry at r4 (kept); rc = r14.
   Clobbers r0-r3 and r5-r7. */
Lxform:
        mov.l   .Lrc_vcframe2,r0
        mov.l   @(r0,r14),r1
        or      r5,r1
        mov.l   r1,@(VC_KEY,r4)         /* key = vi | vcframe */
        mov.l   .Llvl_verts,r0
        mov.l   @r0,r6
        shll2   r5
        shll    r5
        add     r5,r6                   /* v = &lvl.verts[vi] */
        mov.w   @(6,r6),r0              /* pad: a mover's vertex */
        tst     r0,r0
        bf      1f
        mov.l   .Lview_camr,r7
        bra     2f
        nop
1:      add     #-1,r0
        mov     r0,r1
        add     r0,r0
        add     r1,r0
        shll2   r0                      /* m * 12 */
        mov.l   .Lview_mcamr,r7
        add     r0,r7
2:      mov.l   .Lview_r16,r2
        mov     r6,r3
        clrmac
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        sts     macl,r1
        mov.l   @r7,r0
        sub     r0,r1
        shar    r1                      /* x */
        mov     r6,r3
        clrmac
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        sts     macl,r5
        mov.l   @(4,r7),r0
        sub     r0,r5
        shar    r5                      /* y */
        mov     r6,r3
        clrmac
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        mac.w   @r3+,@r2+
        sts     macl,r6
        mov.l   @(8,r7),r0
        sub     r0,r6
        shar    r6                      /* z */
        mov.l   r1,@(VC_V,r4)
        mov.l   r5,@(VC_V+4,r4)
        mov.l   r6,@(VC_V+8,r4)
        mov.l   .Lnearz2,r0
        cmp/ge  r0,r6
        bt      .Lx_front
        mov     #1,r7                   /* in front of the near plane */
        TOPBOT
        bra     .Lx_sides
        nop
.Lx_front:
        mov     #0,r7
        mov.l   .Ldvsr,r3               /* proj * 2^40 / z, in parallel */
        mov.l   r6,@r3
        mov.l   .Lview_proj40,r0
        mov.l   @r0,r2
        mov.l   r2,@(16,r3)
        mov.l   @(4,r0),r2
        mov.l   r2,@(20,r3)
        mov.l   .Lview_far,r0
        mov.l   @r0,r0
        cmp/gt  r0,r6
        bf      3f
        mov     #2,r7
3:      TOPBOT
        mov.l   .Ldvsr,r3
        mov.l   @(20,r3),r3             /* inv */
        dmuls.l r1,r3
        sts     mach,r0
        sts     macl,r2
        shll8   r0
        shlr16  r2
        shlr8   r2
        or      r0,r2                   /* (x * inv) >> 24 */
        mov.l   .Lview_cx16,r0
        mov.l   @r0,r0
        add     r0,r2
        cmp/pz  r2
        bt      4f
        mov     #0,r2
4:      mov.l   .Lview_vw16,r0
        mov.l   @r0,r0
        cmp/gt  r0,r2
        bf      5f
        mov     r0,r2
5:      mov.l   r2,@(VC_SX,r4)
        dmuls.l r5,r3
        sts     mach,r0
        sts     macl,r2
        shll8   r0
        shlr16  r2
        shlr8   r2
        or      r0,r2                   /* (y * inv) >> 24 */
        mov.l   .Lview_cy16,r0
        mov.l   @r0,r0
        add     r0,r2
        mov.l   @(RC_ROW0,r14),r0
        shll16  r0
        cmp/ge  r0,r2
        bt      6f
        mov     r0,r2
6:      mov.l   @(RC_ROW1,r14),r0
        shll16  r0
        cmp/gt  r0,r2
        bf      7f
        mov     r0,r2
7:      mov.l   r2,@(VC_SY,r4)
.Lx_sides:
        mov     r6,r0
        neg     r0,r0
        cmp/gt  r1,r0                   /* x < -z */
        bf      8f
        mov     #4,r0
        or      r0,r7
8:      cmp/gt  r6,r1                   /* x > z */
        bf      9f
        mov     #8,r0
        or      r0,r7
9:      rts
        mov.l   r7,@(VC_OC,r4)

        .align  2
.Lrc_vcframe2:  .long   RC_VCFRAME
.Llvl_verts:    .long   _lvl + LVL_VERTS_
.Lview_camr:    .long   _view + VIEW_CAMR
.Lview_mcamr:   .long   _view + VIEW_MCAMR
.Lview_r16:     .long   _view + VIEW_R16
.Lnearz2:       .long   4 << 16
.Ldvsr:         .long   0xFFFFFF00
.Lview_proj40:  .long   _view + VIEW_PROJ40
.Lview_far:     .long   _view + VIEW_FAR
.Lview_cx16:    .long   _view + VIEW_CX16
.Lview_cy16:    .long   _view + VIEW_CY16
.Lview_vw16:    .long   _view + VIEW_VW16

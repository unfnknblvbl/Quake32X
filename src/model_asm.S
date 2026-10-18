/*-----------------------------------------------------------------------
 * Model vertices in SH-2 assembly (r_model.c has the C version, used by
 * the host build, with the same results bit for bit).
 *
 * void xform_verts_asm(const mxform_t *m, const uint8_t *fv, mvert_t *mv,
 *                      int n)
 *   fv: the frame's vertices (4 bytes each: x, y, z, normal index).
 *   Each vertex: view space = a * vertex + c, the light from its normal,
 *   then 2^28 / (z >> 8) on the hardware divider (started as soon as z is
 *   known, collected after the light) and the projection.
 *---------------------------------------------------------------------*/
#include "asmoff.h"

        .section .text
        .align  2

        /* mxform_t */
        .equ    M_A00, 0
        .equ    M_A01, 4
        .equ    M_A02, 8
        .equ    M_A10, 12
        .equ    M_A11, 16
        .equ    M_A12, 20
        .equ    M_A20, 24
        .equ    M_A21, 28
        .equ    M_A22, 32
        .equ    M_C0, 36
        .equ    M_C1, 40
        .equ    M_C2, 44
        .equ    M_L0, 48
        .equ    M_L1, 52
        .equ    M_AMB, 60
        .equ    M_SHADE, 64     /* (needs r0-indexed loads) */
        /* mvert_t */
        .equ    V_X, 0
        .equ    V_Y, 4
        .equ    V_DEPTH, 8
        .equ    V_ROW, 10
        .equ    V_NEAR, 11
        .equ    V_SIZE, 12

/* r0 = clamp(c16 + (r1 * r7) >> 20, -(8192 << 16), 8192 << 16) with the
   64-bit arithmetic of clampscreen(); \c16 is a register; clobbers r2, r3 */
        .macro  PROJ c16
        dmuls.l r1,r7
        sts     mach,r2
        sts     macl,r0
        mov     r2,r3
        shll8   r3
        shll2   r3
        shll2   r3              /* hi << 12 */
        shlr16  r0
        shlr2   r0
        shlr2   r0              /* lo >> 20 (logical) */
        or      r3,r0           /* low word of the 64-bit result */
        shar    r2              /* high word: hi >> 20 (arithmetic) */
        shar    r2
        shar    r2
        shar    r2
        shlr16  r2
        exts.w  r2,r2
        clrt
        mov     \c16,r3
        addc    r3,r0           /* + c16 (>= 0): low word, carry */
        mov     #0,r3
        addc    r3,r2           /* high word + carry */
        /* clamp: r2:r0 is a signed 64-bit value */
        cmp/pz  r2
        bf      86f
        tst     r2,r2
        bf      85f             /* >= 2^32: the top */
        mov.l   .Lmaxs,r3
        cmp/hi  r3,r0           /* 0 <= v: above 8192 << 16? */
        bf      89f
85:     mov.l   .Lmaxs,r0
        bra     89f
        nop
86:     mov     #-1,r3
        cmp/eq  r3,r2
        bf      87f             /* < -2^32 */
        mov.l   .Lmins,r3
        cmp/hs  r3,r0           /* -2^32 <= v < 0: v >= -(8192 << 16)? */
        bt      89f
87:     mov.l   .Lmins,r0
89:
        .endm

        .global _xform_verts_asm
_xform_verts_asm:
        mov.l   r8,@-r15
        mov.l   r9,@-r15
        mov.l   r10,@-r15
        mov.l   r11,@-r15
        mov.l   r12,@-r15
        mov.l   r13,@-r15
        mov.l   r14,@-r15
        mov     r4,r14                  /* m */
        mov     r5,r13                  /* fv */
        mov     r6,r12                  /* mv */
        mov     r7,r11                  /* n */
        mov.l   .Lview_cx16,r0
        mov.l   @r0,r9                  /* cx16 */
        mov.l   .Lview_cy16,r0
        mov.l   @r0,r10                 /* cy16 */
        mov.l   .Lanorms_ptr,r8         /* &r_anorms */
        mov.l   @r8,r8                  /* anorms */
        tst     r11,r11
        bf      .Lvert
        bra     .Lend
        nop
.Lvert:
        mov.b   @r13,r4
        extu.b  r4,r4                   /* b0 */
        mov.b   @(1,r13),r0
        extu.b  r0,r5                   /* b1 */
        mov.b   @(2,r13),r0
        extu.b  r0,r6                   /* b2 */
        /* z = a20 b0 + a21 b1 + a22 b2 + c2 */
        mov.l   @(M_A20,r14),r0
        mul.l   r0,r4
        mov.l   @(M_A21,r14),r0
        sts     macl,r1
        mul.l   r0,r5
        mov.l   @(M_A22,r14),r0
        sts     macl,r2
        mul.l   r0,r6
        add     r2,r1
        mov.l   @(M_C2,r14),r0
        add     r0,r1
        sts     macl,r2
        add     r2,r1                   /* z */
        mov.l   .Lnearz,r0
        cmp/ge  r0,r1
        bf      1f
        /* start 2^28 / (z >> 8) */
        mov     r1,r2
        shlr8   r2                      /* z > 0 here: logical = arithmetic */
        mov.l   .Ldvsr,r3
        mov.l   r2,@r3
        mov.l   .Lk28,r2
        mov.l   r2,@(4,r3)              /* DVDNT: divides */
1:      mov     r1,r7                   /* z (kept) */
        /* x */
        mov.l   @(M_A00,r14),r0
        mul.l   r0,r4
        mov.l   @(M_A01,r14),r0
        sts     macl,r1
        mul.l   r0,r5
        mov.l   @(M_A02,r14),r0
        sts     macl,r2
        mul.l   r0,r6
        add     r2,r1
        mov.l   @(M_C0,r14),r0
        add     r0,r1
        sts     macl,r2
        add     r2,r1
        mov.l   r1,@-r15                /* x */
        /* y */
        mov.l   @(M_A10,r14),r0
        mul.l   r0,r4
        mov.l   @(M_A11,r14),r0
        sts     macl,r1
        mul.l   r0,r5
        mov.l   @(M_A12,r14),r0
        sts     macl,r2
        mul.l   r0,r6
        add     r2,r1
        mov.l   @(M_C1,r14),r0
        add     r0,r1
        sts     macl,r2
        add     r2,r1
        mov.l   r1,@-r15                /* y */
        /* light: lc = (n0 l0 + n1 l1) >> 14 */
        mov.b   @(3,r13),r0
        extu.b  r0,r0
        mov     r0,r1
        add     r0,r0
        add     r1,r0
        add     r0,r0                   /* index * 6 */
        add     r8,r0
        mov.w   @r0,r1                  /* n0 */
        mov.w   @(2,r0),r0              /* n1 */
        mov.l   @(M_L1,r14),r2
        mul.l   r2,r0
        mov.l   @(M_L0,r14),r2
        sts     macl,r3
        mul.l   r2,r1
        sts     macl,r1
        add     r3,r1                   /* n0 l0 + n1 l1 */
        mov.l   .Lk2p18,r2
        dmuls.l r2,r1
        sts     mach,r1                 /* >> 14 (arithmetic) */
        mov.l   @(M_AMB,r14),r0
        shll8   r0
        shll2   r0
        shll2   r0
        shll2   r0                      /* val = amb = ambinv << 14 */
        cmp/pz  r1
        bt      2f
        mov     r0,r2
        mov     #M_SHADE,r0
        mov.l   @(r0,r14),r0
        mul.l   r0,r1
        sts     macl,r0
        add     r2,r0                   /* val += shade * lc */
2:      cmp/pz  r0
        bt      3f
        mov     #0,r0
3:      shlr16  r0
        mov.b   r0,@(V_ROW,r12)
        mov.l   @r15+,r6                /* y */
        mov.l   @r15+,r5                /* x */
        mov.l   .Lnearz,r0
        cmp/ge  r0,r7
        bt      4f
        mov     #1,r0
        bra     .Lnext
        mov.b   r0,@(V_NEAR,r12)
4:      mov.l   .Ldvsr,r3
        mov.l   @(4,r3),r4              /* r = 2^28 / (z >> 8) */
        mov.l   .Lview_proj,r0
        mov.l   @r0,r7
        mul.l   r4,r7
        sts     macl,r7                 /* k = r * proj */
        mov     r5,r1
        PROJ    r9
        mov.l   r0,@(V_X,r12)
        mov     r6,r1
        PROJ    r10
        mov.l   r0,@(V_Y,r12)
        shlr2   r4                      /* r >> 2 (r > 0) */
        mov.l   .Lffff,r0
        cmp/hi  r0,r4
        bf      5f
        mov     r0,r4
5:      mov     r4,r0
        mov.w   r0,@(V_DEPTH,r12)
        mov     #0,r0
        mov.b   r0,@(V_NEAR,r12)
.Lnext:
        add     #4,r13
        dt      r11
        bt      .Lend
        bra     .Lvert
        add     #V_SIZE,r12
.Lend:
        mov.l   @r15+,r14
        mov.l   @r15+,r13
        mov.l   @r15+,r12
        mov.l   @r15+,r11
        mov.l   @r15+,r10
        mov.l   @r15+,r9
        rts
        mov.l   @r15+,r8

        .align  2
.Lview_cx16:    .long   _view + VIEW_CX16
.Lview_cy16:    .long   _view + VIEW_CY16
.Lview_proj:    .long   _view + VIEW_PROJ
.Lanorms_ptr:   .long   _r_anorms
.Lnearz:        .long   4 << 16
.Ldvsr:         .long   0xFFFFFF00
.Lk28:          .long   1 << 28
.Lk2p18:        .long   1 << 18
.Lffff:         .long   0xFFFF
.Lmaxs:         .long   8192 << 16
.Lmins:         .long   -(8192 << 16)

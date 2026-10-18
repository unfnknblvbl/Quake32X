/* Structure offsets for the assembly files: compiled with -S, and the
 * "->NAME value" lines become build/asmoff.h (see the Makefile). */
#include "r_local.h"

#define DEF(name, val) __asm__ volatile("\n->" #name " %0" :: "i"(val))
#define OFF(t, f) __builtin_offsetof(t, f)

void asmoff(void)
{
    DEF(RC_ROW0, OFF(rcpu_t, row0));
    DEF(RC_ROW1, OFF(rcpu_t, row1));
    DEF(RC_ROWSLEFT, OFF(rcpu_t, rowsleft));
    DEF(RC_NSPANS, OFF(rcpu_t, nspans));
    DEF(RC_SPANS, OFF(rcpu_t, spans));
    DEF(RC_VXFORMS, OFF(rcpu_t, vxforms));
    DEF(RC_KY0, OFF(rcpu_t, ky0));
    DEF(RC_KY1, OFF(rcpu_t, ky1));
    DEF(RC_VCFRAME, OFF(rcpu_t, vcframe));
    DEF(MAXSPANS_, MAXSPANS);
    DEF(MAXGAPS_, MAXGAPS);
    DEF(GAPROW_SIZE, sizeof(gaprow_t));
    DEF(VIEW_VW, OFF(view_t, vw));
    DEF(VIEW_VW16, OFF(view_t, vw16));
    DEF(VIEW_CX16, OFF(view_t, cx16));
    DEF(VIEW_CY16, OFF(view_t, cy16));
    DEF(VIEW_FAR, OFF(view_t, far));
    DEF(VIEW_PROJ40, OFF(view_t, proj40));
    DEF(VIEW_PROJ, OFF(view_t, proj));
    DEF(VIEW_R16, OFF(view_t, r16));
    DEF(VIEW_CAMR, OFF(view_t, camr));
    DEF(VIEW_MCAMR, OFF(view_t, mcamr));
    DEF(VC_KEY, OFF(vcache_t, key));
    DEF(VC_V, OFF(vcache_t, v));
    DEF(VC_SX, OFF(vcache_t, sx));
    DEF(VC_SY, OFF(vcache_t, sy));
    DEF(VC_OC, OFF(vcache_t, oc));
    DEF(LVL_VERTS_, OFF(level_t, verts));
    DEF(LVL_FGEOM_, OFF(level_t, fgeom));
    DEF(LVL_FVERTS_, OFF(level_t, fverts));
    DEF(RC_VCACHE, OFF(rcpu_t, vcache));
    DEF(RC_FR_N, OFF(rcpu_t, fr_n));
    DEF(RC_FR_D, OFF(rcpu_t, fr_d));
    DEF(RC_FR_LEN, OFF(rcpu_t, fr_len));
    DEF(VC_SIZE, sizeof(vcache_t));
    DEF(VCACHE_SIZE_, VCACHE_SIZE);
}

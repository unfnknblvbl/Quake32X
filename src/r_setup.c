/* Per-face shading setup: 1/z, s/z, t/z screen gradients and the surface */
#include "r_local.h"


/* shift right by the mip level without a variable shift (no barrel shifter) */
#define MIPSHIFT(mip, ...) do { switch (mip) { \
    case 3: __VA_ARGS__; /* fall through */ \
    case 2: __VA_ARGS__; /* fall through */ \
    case 1: __VA_ARGS__; /* fall through */ \
    default: break; } } while (0)

/* only the 1/z gradients (flat shading, for the depth intervals) */
void face_zsetup(frec_t *fr)
{
    const lface_t *f = fr->f;
    const lplane_t *pl = &lvl.planes[f->plane];
    fixed_t n[3], dcam;
    int32_t distinv, A, B, zref;
    if (f->flags & FF_PLANEBACK) {
        n[0] = -pl->n[0]; n[1] = -pl->n[1]; n[2] = -pl->n[2];
        dcam = pl->dist - dot16(pl->n, view.org);
    } else {
        n[0] = pl->n[0]; n[1] = pl->n[1]; n[2] = pl->n[2];
        dcam = dot16(pl->n, view.org) - pl->dist;
    }
    if (dcam < 0x2000) dcam = 0x2000;
    distinv = hw_div64((int64_t)1 << 43, dcam);
    A = dot16(n, view.r80s5);
    B = dot16(n, view.d80s5);
    fr->dzidx = -(int32_t)(((int64_t)A * distinv) >> 31);
    fr->dzidy = -(int32_t)(((int64_t)B * distinv) >> 31);
    zref = hw_div64((int64_t)1 << 48, fr->refv.z);
    fr->zi0 = (int32_t)((uint32_t)zref + (uint32_t)fixmul(fr->dzidx, 0x8000 - fr->refsx)
                        + (uint32_t)fixmul(fr->dzidy, 0x8000 - fr->refsy));
}

/* returns 0 if the surface cache could not make room without evicting a
 * surface still needed by this batch (the caller draws and retries) */
int face_setup(rcpu_t *rc, frec_t *fr)
{
    const lface_t *f = fr->f;
    const ltexinfo_t *ti;
    const lplane_t *pl;
    fixed_t n[3], dcam, sc, tc, sref, tref;
    int32_t distinv, A, B, zref, sr, sd, tr, td, dzidx, dzidy;
    int mip;

    if (!view.textures || (f->flags & FF_SKY))
        return 1;
    ti = &lvl.texinfo[f->texinfo];
    pl = &lvl.planes[f->plane];

    /* mip level from the nearest point, raised until the surface fits */
    {
        fixed_t z = fixmul(fr->zmin, view.mipscale);    /* as seen at full size */
        if (view.mipbias) z >>= 1;
        mip = z < (96 << 16) ? 0 : z < (192 << 16) ? 1 : z < (384 << 16) ? 2 : 3;
        if (f->flags & FF_TURB)
            mip = 0;                    /* liquids: base texture only, as in Quake */
        else {
            /* seen through the see-through water mesh: half its pixels are
               covered, so a coarser surface does (and fits the cache) */
            if ((f->flags & FF_UNDERWATER) && !view.underwater && mip < 3)
                mip++;
            int area = f->ext[0] * f->ext[1], lim = MAX_SURFSIZE;
            MIPSHIFT(mip, lim <<= 2);
            while (mip < 3 && area > lim) {
                mip++;
                lim <<= 2;
            }
            fr->surf = sc_get(rc, fr->fi, mip);
            if (!fr->surf)
                return 0;
        }
        fr->mip = mip;
    }

    if (f->flags & FF_PLANEBACK) {
        n[0] = -pl->n[0]; n[1] = -pl->n[1]; n[2] = -pl->n[2];
        dcam = pl->dist - dot16(pl->n, view.org);
    } else {
        n[0] = pl->n[0]; n[1] = pl->n[1]; n[2] = pl->n[2];
        dcam = dot16(pl->n, view.org) - pl->dist;
    }
    if (dcam < 0x2000) dcam = 0x2000;
    distinv = hw_div64((int64_t)1 << 43, dcam);
    A = dot16(n, view.r80s5);
    B = dot16(n, view.d80s5);
    dzidx = -(int32_t)(((int64_t)A * distinv) >> 31);
    dzidy = -(int32_t)(((int64_t)B * distinv) >> 31);
    fr->dzidx = dzidx;
    fr->dzidy = dzidy;
    zref = hw_div64((int64_t)1 << 48, fr->refv.z);

    {
        fixed_t vr = dot16(ti->s, view.right), vd = dot16(ti->s, view.down), vf = dot16(ti->s, view.fwd);
        sc = dot16(ti->s, view.org) + ti->s[3] - (f->tmins[0] << 16);
        sref = sc + fixmul(vr, fr->refv.x) + fixmul(vd, fr->refv.y) + fixmul(vf, fr->refv.z);
        sr = fixmul(vr, view.k24);
        sd = fixmul(vd, view.k24);
    }
    {
        fixed_t vr = dot16(ti->t, view.right), vd = dot16(ti->t, view.down), vf = dot16(ti->t, view.fwd);
        tc = dot16(ti->t, view.org) + ti->t[3] - (f->tmins[1] << 16);
        tref = tc + fixmul(vr, fr->refv.x) + fixmul(vd, fr->refv.y) + fixmul(vf, fr->refv.z);
        tr = fixmul(vr, view.k24);
        td = fixmul(vd, view.k24);
    }
    sc >>= 8; tc >>= 8; sref >>= 8; tref >>= 8;
    MIPSHIFT(mip, sr >>= 1; sd >>= 1; sc >>= 1; sref >>= 1; tr >>= 1; td >>= 1; tc >>= 1; tref >>= 1);
    fr->dszdx = sr + fixmul(sc, dzidx);
    fr->dszdy = sd + fixmul(sc, dzidy);
    fr->dtzdx = tr + fixmul(tc, dzidx);
    fr->dtzdy = td + fixmul(tc, dzidy);
    {
        fixed_t ox = 0x8000 - fr->refsx, oy = 0x8000 - fr->refsy;
        fr->zi0 = (int32_t)((uint32_t)zref + (uint32_t)fixmul(dzidx, ox) + (uint32_t)fixmul(dzidy, oy));
        fr->sz0 = (int32_t)((uint32_t)fixmul(sref, zref) + (uint32_t)fixmul(fr->dszdx, ox)
                            + (uint32_t)fixmul(fr->dszdy, oy));
        fr->tz0 = (int32_t)((uint32_t)fixmul(tref, zref) + (uint32_t)fixmul(fr->dtzdx, ox)
                            + (uint32_t)fixmul(fr->dtzdy, oy));
    }

    {
        int w = f->ext[0], h = f->ext[1];
        MIPSHIFT(mip, w >>= 1; h >>= 1);
        fr->smax = (w << 16) - 0x101;   /* margins absorb step rounding */
        fr->tmax = (h << 16) - 0x101;
        if (f->flags & FF_TURB) {
            const ltexture_t *tx = &lvl.textures[ti->tex];
            int tw = tx->w, th = tx->h, s0 = f->tmins[0], t0 = f->tmins[1];
            MIPSHIFT(mip, tw >>= 1; th >>= 1; s0 >>= 1; t0 >>= 1);
            fr->soff = s0 & (tw - 1);
            fr->toff = t0 & (th - 1);
            fr->smax = 0x7FFFFFFF;
            fr->tmax = 0x7FFFFFFF;
        }
    }
    return 1;
}

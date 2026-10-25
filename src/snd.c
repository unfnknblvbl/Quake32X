/* Sound: Quake's 11 kHz samples, mixed in stereo on the slave SH-2 and
 * played through the 32X PWM at 22 kHz.
 *
 * The slave's DMA channel 1 feeds the PWM: each PWM request moves one long
 * (left and right pulse widths) from a mix buffer. When a buffer is done,
 * the DMA-end interrupt (vector 72, priority 4) restarts the DMA on the
 * other buffer and mixes the next one. The master starts sounds by putting
 * commands in a small queue (cache-through), so the mixer's channel state
 * belongs to the slave alone. The interrupt never touches the hardware
 * divider, which the interrupted renderer may be in the middle of using.
 *
 * Besides the sounds that play once, two channels loop the static sounds
 * (torches, drips, machinery): each frame the master picks the two loudest
 * near the listener, as Quake mixes its static channels, and sets their
 * volumes. */
#include "g_local.h"
#include "levels.h"

#define MIX_RATE    11025           /* the samples' own rate */
#define OUT_RATE    (MIX_RATE * 2)  /* PWM rate: each mixed sample twice */
#define NDYN        6               /* channels for sounds that play once */
#define NSTATIC     2               /* looping channels for static sounds */
#define NCHAN       (NDYN + NSTATIC)
#define BUFSAMPLES  256             /* output samples per DMA buffer (11.6 ms) */
#define QSIZE       16

#define CMD_STOP_ALL 255
#define CMD_SILENCE  254

#include "snd_dec.h"

typedef struct {
    const uint8_t *data;            /* 8-bit unsigned, in ROM (format 0: the next sample) */
    int32_t  left;                  /* samples remaining */
    int16_t  lvol, rvol;            /* 0..255 */
    const uint8_t *loop;            /* where a looping sound starts again (formats 1-3: non-NULL) */
    int32_t  looplen;
    int16_t  sound;
    /* formats 1-3 */
    uint8_t  fmt;
    int32_t  pos, loopstart;            /* in 11 kHz samples */
    sdec_t   sd;                        /* the decoder */
    uint8_t  dec[BUFSAMPLES / 2];       /* this buffer's samples, decoded */
} chan_t;

typedef struct {
    uint8_t  sound, lvol, rvol, chan;       /* chan 0: any dynamic, 1.. a static one */
} sndcmd_t;

static chan_t chans[NCHAN];         /* the slave's */
static uint32_t mixbuf[2][BUFSAMPLES] __attribute__((aligned(16), unused));
static sndcmd_t cmdq[QSIZE];
static volatile uint16_t q_head, q_tail; /* written by master / slave (cache-through) */
static volatile int32_t snd_running;     /* the slave's mixer is going */
static int pwm_cycle, cur_buf __attribute__((unused));
static const int32_t *sound_table;      /* SND_NUM x { offset, length, loop } */
static const uint8_t *sound_base;
static const uint8_t *bank_base;        /* the shared sound bank (offsets with bit 31) */

/* a sound table offset as an address: in the level, or in the bank */
static inline const uint8_t *snd_addr(const uint8_t *base, const uint8_t *bank, int32_t ofs)
{
    return ofs < 0 ? bank + (ofs & 0x7FFFFFFF) : base + ofs;
}

#ifndef HOST
#define PWM_CTRL    REG16(0x20004030)
#define PWM_CYCLE   REG16(0x20004032)
#define PWM_LEFT    REG16(0x20004034)
#define PWM_MONO    REG16(0x20004038)
#define DMA_SAR1    REG32(0xFFFFFF90)
#define DMA_DAR1    REG32(0xFFFFFF94)
#define DMA_TCR1    REG32(0xFFFFFF98)
#define DMA_CHCR1   REG32(0xFFFFFF9C)
#define DMA_VCR1    REG32(0xFFFFFFA8)
#define DMA_DMAOR   REG32(0xFFFFFFB0)
#define DMA_DRCR1   (*(volatile uint8_t *)0xFFFFFE72)
#define INT_IPRA    REG16(0xFFFFFEE2)
#define CHCR_GO     0x18E5          /* src +4, long, external request (PWM),
                                       edge, interrupt on end, enable */
#endif

#ifndef HOST
#define QV(p) ((volatile void *)UNCACHED(p))
#else
#define QV(p) ((volatile void *)(p))
#endif

/* ------------------------------------------------------------ master side */

/* static sound emitters of the level */
#define MAX_STATIC 64
static struct { int16_t org[3]; uint8_t sound, vol; } statics[MAX_STATIC];   /* whole units */
static int nstatic;
static uint8_t st_sound[NSTATIC], st_lvol[NSTATIC], st_rvol[NSTATIC];

GCODE static void put_cmd(int sound, int lv, int rv, int chan)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    volatile sndcmd_t *q = (volatile sndcmd_t *)QV(cmdq);
    int h = *head;
    if ((uint16_t)(h - *tail) >= QSIZE)
        return;                     /* queue full: drop it */
    q[h & (QSIZE - 1)].sound = sound;
    q[h & (QSIZE - 1)].lvol = lv;
    q[h & (QSIZE - 1)].rvol = rv;
    q[h & (QSIZE - 1)].chan = chan;
    *head = h + 1;
}

/* the level's sounds; everything that plays stops first (the mixer must
 * not read the old level's pages once they are mapped out) */
GCODE void snd_init(void)
{
    int i;
    /* (the slave reads these two cache-through) */
    sound_table = (const int32_t *)(lvl.base + lvl.hdr->lump[LVL_SOUNDS].ofs);
    sound_base = lvl.base;
    bank_base = lvl_bank();
    nstatic = 0;
    for (i = 0; i < NSTATIC; i++)
        st_sound[i] = CMD_SILENCE;
}

GCODE void snd_stop_all(void)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    uint32_t t0 = get_vblank_count();
    put_cmd(CMD_STOP_ALL, 0, 0, 0);
    if (!*(volatile int32_t *)QV(&snd_running))
        return;
    /* the mixer takes commands every 11.6 ms */
    while (*tail != *head && get_vblank_count() - t0 < 4)
        ;
}

GCODE void snd_stop_dynamic(void)
{
    put_cmd(CMD_STOP_ALL, 0, 0, 0);
    for (int k = 0; k < NSTATIC; k++)
        st_sound[k] = CMD_SILENCE;  /* the static channels start again */
}

/* Quake's SND_Spatialize: full volume up close, silent at 1000 / att
 * units; panned by the listener's right vector. 0 if out of hearing */
GCODE static int spatialize(const fixed_t *org, int vol, int att, int *lv, int *rv)
{
    fixed_t d[3], len, dot;
    int a, dist;
    if (!org) {
        *lv = *rv = vol;
        return 1;
    }
    d[0] = org[0] - view.org[0];
    d[1] = org[1] - view.org[1];
    d[2] = org[2] - view.org[2];
    dist = vlen_units(org, view.org);
    a = 256 - hw_div(dist * att * 256, 1000);
    if (a <= 0)
        return 0;
    len = FX(dist);
    dot = len > FX(1) ? hw_fixdiv(dot16(d, view.right), len) : 0;   /* -1 .. 1 */
    *lv = (vol * a >> 8) * (0x10000 - dot) >> 16;
    *rv = (vol * a >> 8) * (0x10000 + dot) >> 16;
    if (*lv > 255) *lv = 255;
    if (*rv > 255) *rv = 255;
    if (*lv < 0) *lv = 0;
    if (*rv < 0) *rv = 0;
    return 1;
}

/* start a sound: org NULL for the player's own (full volume, centred);
   att is Quake's attenuation (1 normal, 2 idle, 3 static, 0 everywhere) */
GCODE void snd_play(int sound, const fixed_t *org, int vol, int att)
{
    int lv, rv;
    if (sound < 0 || sound >= SND_NUM || !sound_table[sound * 3])
        return;
    if (!att)
        org = NULL;
    if (!spatialize(org, vol, att, &lv, &rv))
        return;
    put_cmd(sound, lv, rv, 0);
}

GCODE void snd_start(int sound, const fixed_t *org, int vol)
{
    snd_play(sound, org, vol, 1);
}

/* a static sound (ambientsound in QuakeC): loops where it is */
GCODE void snd_static(int sound, const fixed_t org[3], int vol)
{
    if (nstatic >= MAX_STATIC || sound < 0 || sound >= SND_NUM || !sound_table[sound * 3])
        return;
    statics[nstatic].org[0] = UNITS(org[0]);
    statics[nstatic].org[1] = UNITS(org[1]);
    statics[nstatic].org[2] = UNITS(org[2]);
    statics[nstatic].sound = sound;
    statics[nstatic].vol = vol;
    nstatic++;
}

/* each frame: the two loudest static sounds on the looping channels */
GCODE void snd_update(void)
{
    int best[NSTATIC], bl[NSTATIC], br[NSTATIC], bv[NSTATIC], i, k;
    for (k = 0; k < NSTATIC; k++) {
        best[k] = -1;
        bv[k] = 0;
        bl[k] = br[k] = 0;
    }
    for (i = 0; i < nstatic; i++) {
        int lv, rv, v, j;
        int dx = statics[i].org[0] - UNITS(view.org[0]), dy = statics[i].org[1] - UNITS(view.org[1]);
        fixed_t o[3];
        if (dx < -340 || dx > 340 || dy < -340 || dy > 340)
            continue;
        o[0] = FX(statics[i].org[0]);
        o[1] = FX(statics[i].org[1]);
        o[2] = FX(statics[i].org[2]);
        if (!spatialize(o, statics[i].vol, 3, &lv, &rv))
            continue;
        v = lv + rv;
        /* the same sound twice: the louder one only */
        for (k = 0; k < NSTATIC; k++)
            if (best[k] >= 0 && statics[best[k]].sound == statics[i].sound)
                break;
        if (k < NSTATIC) {
            if (v > bv[k]) { best[k] = i; bv[k] = v; bl[k] = lv; br[k] = rv; }
            continue;
        }
        for (k = 0; k < NSTATIC; k++)
            if (v > bv[k])
                break;
        if (k == NSTATIC)
            continue;
        for (j = NSTATIC - 1; j > k; j--) {
            best[j] = best[j - 1]; bv[j] = bv[j - 1]; bl[j] = bl[j - 1]; br[j] = br[j - 1];
        }
        best[k] = i; bv[k] = v; bl[k] = lv; br[k] = rv;
    }
    /* keep a sound on the channel it is already playing on */
    for (k = 0; k < NSTATIC; k++) {
        int s = best[k] >= 0 ? statics[best[k]].sound : CMD_SILENCE, other = k ^ 1;
        if (NSTATIC == 2 && s != CMD_SILENCE && s != st_sound[k] && s == st_sound[other]) {
            int t;
            t = best[k]; best[k] = best[other]; best[other] = t;
            t = bl[k]; bl[k] = bl[other]; bl[other] = t;
            t = br[k]; br[k] = br[other]; br[other] = t;
        }
    }
    for (k = 0; k < NSTATIC; k++) {
        int s = best[k] >= 0 ? statics[best[k]].sound : CMD_SILENCE;
        int dl = bl[k] - st_lvol[k], dr = br[k] - st_rvol[k];
        if (s == st_sound[k] && dl < 8 && dl > -8 && dr < 8 && dr > -8)
            continue;
        st_sound[k] = s;
        st_lvol[k] = bl[k];
        st_rvol[k] = br[k];
        put_cmd(s, bl[k], br[k], 1 + k);
    }
}

/* ------------------------------------------------------------ slave side */

/* a channel starts a sound: its table entry e; looping: a static channel */
static void chan_start(chan_t *ch, const int32_t *e, const uint8_t *base, const uint8_t *bank, int looping)
{
    int32_t len = SF_LEN(e[1]);
    ch->data = snd_addr(base, bank, e[0]);
    ch->left = len;
    ch->fmt = SF_FMT(e[1]);
    ch->pos = 0;
    sdec_start(&ch->sd, ch->data, ch->fmt, len);
    ch->loopstart = looping && e[2] >= 0 ? e[2] : 0;
    ch->loop = !looping ? NULL : ch->fmt ? ch->data : ch->data + ch->loopstart;
    ch->looplen = len - ch->loopstart;
}

static void take_commands(void)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    volatile sndcmd_t *q = (volatile sndcmd_t *)QV(cmdq);
    const int32_t *tab = *(const int32_t *volatile *)QV(&sound_table);
    const uint8_t *base = *(const uint8_t *volatile *)QV(&sound_base);
    const uint8_t *bank = *(const uint8_t *volatile *)QV(&bank_base);
    uint16_t t = *tail;
    while (t != *head) {
        const volatile sndcmd_t *c = &q[t & (QSIZE - 1)];
        int k, best = 0, s = c->sound;
        t++;
        if (s == CMD_STOP_ALL) {
            for (k = 0; k < NCHAN; k++) {
                chans[k].left = 0;
                chans[k].sound = -1;
            }
            continue;
        }
        if (c->chan) {
            /* a static channel: its sound (from the start if it is a new
               one) and its volume */
            chan_t *ch = &chans[NDYN + c->chan - 1];
            if (s == CMD_SILENCE) {
                ch->left = 0;
                ch->sound = -1;
                continue;
            }
            if (ch->sound != s || ch->left <= 0) {
                chan_start(ch, tab + s * 3, base, bank, 1);
                ch->sound = s;
            }
            ch->lvol = c->lvol;
            ch->rvol = c->rvol;
            continue;
        }
        /* a free channel, else the one closest to finishing */
        for (k = 0; k < NDYN; k++) {
            if (chans[k].left <= 0) { best = k; break; }
            if (chans[k].left < chans[best].left) best = k;
        }
        chan_start(&chans[best], tab + s * 3, base, bank, 0);
        chans[best].lvol = c->lvol;
        chans[best].rvol = c->rvol;
        chans[best].sound = s;
    }
    *tail = t;
}

/* mix one buffer: each 11 kHz sample becomes two output samples. Sample
 * by sample over the playing channels, so the sums stay in registers; a
 * looping channel that wraps in the buffer is two parts */
static int silent_bufs;

#define HALF (BUFSAMPLES / 2)
#define HALF_SAMPLES HALF

/* v >> 7 (arithmetic): GCC calls a library routine for it on the SH-2,
   the multiplier's high word does it in two instructions */
static inline int32_t sar7(int32_t v)
{
#ifdef HOST
    return v >> 7;
#else
    int32_t r;
    __asm__("dmuls.l %1,%2\n\tsts mach,%0" : "=r"(r) : "r"(v), "r"(1 << 25) : "macl", "mach");
    return r;
#endif
}

/* one output word: both pulse widths, clamped */
static inline uint32_t pack(int32_t al, int32_t ar, int mid, int top)
{
    int l = mid + sar7(al), r = mid + sar7(ar);
    if (l < 1) l = 1;
    if (l > top) l = top;
    if (r < 1) r = 1;
    if (r > top) r = top;
    return ((uint32_t)l << 16) | (uint32_t)r;
}

/* up to three channels straight into the output (each mixed sample is
   played twice: the PWM runs at twice the samples' rate, so its carrier is
   out of earshot) */
static void mix_direct(uint32_t *out, int a, int b, const uint8_t *const *q, const int *lv, const int *rv,
                       int m, int mid, int top)
{
    int i;
    out += 2 * a;
    if (m == 1) {
        const uint8_t *p0 = q[0];
        int l0 = lv[0], r0 = rv[0];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128;
            uint32_t w = pack(v0 * l0, v0 * r0, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    } else if (m == 2) {
        const uint8_t *p0 = q[0], *p1 = q[1];
        int l0 = lv[0], r0 = rv[0], l1 = lv[1], r1 = rv[1];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128;
            uint32_t w = pack(v0 * l0 + v1 * l1, v0 * r0 + v1 * r1, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    } else {
        const uint8_t *p0 = q[0], *p1 = q[1], *p2 = q[2];
        int l0 = lv[0], r0 = rv[0], l1 = lv[1], r1 = rv[1], l2 = lv[2], r2 = rv[2];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128, v2 = (int)p2[i] - 128;
            uint32_t w = pack(v0 * l0 + v1 * l1 + v2 * l2, v0 * r0 + v1 * r1 + v2 * r2, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    }
}

/* more than three: two at a time into sums kept in the output words
   themselves (left in the first of each pair, right in the second: no
   stack, which is small inside the interrupt), then the last two or three
   with the sums straight out */
static void mix_many(uint32_t *out, int a, int b, const uint8_t *const *q, const int *lv, const int *rv,
                     int m, int mid, int top)
{
    /* through the cache: the sums are read back, and the cache writes
       through, so the memory the DMA reads stays right */
#ifdef HOST
    int32_t *acc = (int32_t *)out;
#else
    int32_t *acc = (int32_t *)((uint32_t)out & ~0x20000000);
#endif
    int i, k = 0;
    for (i = a; i < b; i++)
        acc[2 * i] = acc[2 * i + 1] = 0;
    while (m - k > 3) {
        const uint8_t *p0 = q[k], *p1 = q[k + 1];
        int l0 = lv[k], r0 = rv[k], l1 = lv[k + 1], r1 = rv[k + 1];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128;
            acc[2 * i] += v0 * l0 + v1 * l1;
            acc[2 * i + 1] += v0 * r0 + v1 * r1;
        }
        k += 2;
    }
    {
        const uint8_t *p0 = q[k], *p1 = q[k + 1];
        const uint8_t *p2 = m - k > 2 ? q[k + 2] : q[k];
        int l0 = lv[k], r0 = rv[k], l1 = lv[k + 1], r1 = rv[k + 1];
        int l2 = m - k > 2 ? lv[k + 2] : 0, r2 = m - k > 2 ? rv[k + 2] : 0;
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128, v2 = (int)p2[i] - 128;
            uint32_t w = pack(acc[2 * i] + v0 * l0 + v1 * l1 + v2 * l2,
                              acc[2 * i + 1] + v0 * r0 + v1 * r1 + v2 * r2, mid, top);
            out[2 * i] = w;
            out[2 * i + 1] = w;
        }
    }
}

/* up to three half-rate channels straight into the output, as mix_direct:
   channel k's sample for output i is p_k[(o_k + i) / 2] */
static void mix_direct_half(uint32_t *out, int a, int b, const uint8_t *const *q, const int *lv, const int *rv,
                            const int *off, int m, int mid, int top)
{
    int i;
    out += 2 * a;
    if (m == 1) {
        const uint8_t *p0 = q[0];
        int l0 = lv[0], r0 = rv[0], o0 = off[0];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[(o0 + i) >> 1] - 128;
            uint32_t w = pack(v0 * l0, v0 * r0, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    } else if (m == 2) {
        const uint8_t *p0 = q[0], *p1 = q[1];
        int l0 = lv[0], r0 = rv[0], l1 = lv[1], r1 = rv[1], o0 = off[0], o1 = off[1];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[(o0 + i) >> 1] - 128, v1 = (int)p1[(o1 + i) >> 1] - 128;
            uint32_t w = pack(v0 * l0 + v1 * l1, v0 * r0 + v1 * r1, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    } else {
        const uint8_t *p0 = q[0], *p1 = q[1], *p2 = q[2];
        int l0 = lv[0], r0 = rv[0], l1 = lv[1], r1 = rv[1], l2 = lv[2], r2 = rv[2];
        int o0 = off[0], o1 = off[1], o2 = off[2];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[(o0 + i) >> 1] - 128, v1 = (int)p1[(o1 + i) >> 1] - 128;
            int v2 = (int)p2[(o2 + i) >> 1] - 128;
            uint32_t w = pack(v0 * l0 + v1 * l1 + v2 * l2, v0 * r0 + v1 * r1 + v2 * r2, mid, top);
            out[0] = w;
            out[1] = w;
            out += 2;
        }
    }
}

/* a stretch with half-rate channels: those summed first into the output
   words (each stored sample read for two output samples: the sample index
   is (its offset + i) / 2), then the others as mix_many does */
static const uint8_t silence[HALF_SAMPLES] = {0};
static void mix_half(uint32_t *out, int a, int b, const uint8_t *const *q, const int *lv, const int *rv,
                     const int *off, const uint8_t *hf, int m, int mid, int top)
{
#ifdef HOST
    int32_t *acc = (int32_t *)out;
#else
    int32_t *acc = (int32_t *)((uint32_t)out & ~0x20000000);
#endif
    const uint8_t *fq[NCHAN + NSTATIC];
    int fl[NCHAN + NSTATIC], fr[NCHAN + NSTATIC], nf = 0, i, k;
    for (i = a; i < b; i++)
        acc[2 * i] = acc[2 * i + 1] = 0;
    for (k = 0; k < m; k++) {
        if (!hf[k]) {
            fq[nf] = q[k];
            fl[nf] = lv[k];
            fr[nf] = rv[k];
            nf++;
            continue;
        }
        {
            const uint8_t *p0 = q[k];
            int l0 = lv[k], r0 = rv[k], o = off[k];
            for (i = a; i < b; i++) {
                int v0 = (int)p0[(o + i) >> 1] - 128;
                acc[2 * i] += v0 * l0;
                acc[2 * i + 1] += v0 * r0;
            }
        }
    }
    k = 0;
    while (nf - k > 3) {
        const uint8_t *p0 = fq[k], *p1 = fq[k + 1];
        int l0 = fl[k], r0 = fr[k], l1 = fl[k + 1], r1 = fr[k + 1];
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128;
            acc[2 * i] += v0 * l0 + v1 * l1;
            acc[2 * i + 1] += v0 * r0 + v1 * r1;
        }
        k += 2;
    }
    {
        /* the last up to three, and the sums out (silence for those not there) */
        const uint8_t *p0 = nf - k > 0 ? fq[k] : silence, *p1 = nf - k > 1 ? fq[k + 1] : silence;
        const uint8_t *p2 = nf - k > 2 ? fq[k + 2] : silence;
        int l0 = nf - k > 0 ? fl[k] : 0, r0 = nf - k > 0 ? fr[k] : 0;
        int l1 = nf - k > 1 ? fl[k + 1] : 0, r1 = nf - k > 1 ? fr[k + 1] : 0;
        int l2 = nf - k > 2 ? fl[k + 2] : 0, r2 = nf - k > 2 ? fr[k + 2] : 0;
        for (i = a; i < b; i++) {
            int v0 = (int)p0[i] - 128, v1 = (int)p1[i] - 128, v2 = (int)p2[i] - 128;
            uint32_t w = pack(acc[2 * i] + v0 * l0 + v1 * l1 + v2 * l2,
                              acc[2 * i + 1] + v0 * r0 + v1 * r1 + v2 * r2, mid, top);
            out[2 * i] = w;
            out[2 * i + 1] = w;
        }
    }
}

/* the buffer cut where channels start or stop, each stretch mixed with
   only the channels playing in it */
static void mix_segments(uint32_t *out, const uint8_t *const *p, const int *lv, const int *rv,
                         const int *lo, const int *hi, const int *off, const uint8_t *hf, int na, int mid)
{
    int cuts[2 * (NCHAN + NSTATIC) + 2], nc = 0, i, j, top = pwm_cycle - 1;
    cuts[nc++] = 0;
    cuts[nc++] = HALF;
    for (i = 0; i < na; i++) {
        cuts[nc++] = lo[i];
        cuts[nc++] = hi[i];
    }
    for (i = 1; i < nc; i++)            /* insertion sort: a dozen at most */
        for (j = i; j > 0 && cuts[j - 1] > cuts[j]; j--) {
            int t = cuts[j];
            cuts[j] = cuts[j - 1];
            cuts[j - 1] = t;
        }
    for (i = 0; i + 1 < nc; i++) {
        int a = cuts[i], b = cuts[i + 1], m = 0, k, nh = 0;
        const uint8_t *q[NCHAN + NSTATIC];
        int ql[NCHAN + NSTATIC], qr[NCHAN + NSTATIC], qo[NCHAN + NSTATIC];
        uint8_t qh[NCHAN + NSTATIC];
        if (a >= b)
            continue;
        for (k = 0; k < na; k++)
            if (lo[k] <= a && hi[k] >= b) {
                q[m] = p[k];
                ql[m] = lv[k];
                qr[m] = rv[k];
                qo[m] = off[k];
                qh[m] = hf[k];
                nh += hf[k];
                m++;
            }
        if (nh == m && m <= 3)
            mix_direct_half(out, a, b, q, ql, qr, qo, m, mid, top);
        else if (nh)
            mix_half(out, a, b, q, ql, qr, qo, qh, m, mid, top);
        else if (!m) {
            uint32_t w = ((uint32_t)mid << 16) | mid, *o = out + 2 * a;
            for (k = a; k < b; k++, o += 2)
                o[0] = o[1] = w;
        } else if (m <= 3)
            mix_direct(out, a, b, q, ql, qr, m, mid, top);
        else
            mix_many(out, a, b, q, ql, qr, m, mid, top);
    }
}

static __attribute__((noinline)) void mix(uint32_t *out)
{
    const uint8_t *p[NCHAN + NSTATIC];
    int lv[NCHAN + NSTATIC], rv[NCHAN + NSTATIC], lo[NCHAN + NSTATIC], hi[NCHAN + NSTATIC], off[NCHAN + NSTATIC];
    uint8_t hf[NCHAN + NSTATIC];
    int i, k, na = 0, mid = pwm_cycle >> 1;
    take_commands();
    for (k = 0; k < NCHAN; k++) {
        chan_t *ch = &chans[k];
        int n;
        if (ch->left <= 0 || (!ch->lvol && !ch->rvol && !ch->loop)) {
            if (!ch->loop)
                ch->left = 0;
            continue;
        }
        n = ch->left < HALF ? ch->left : HALF;
        if (ch->fmt) {
            /* formats 1-3 (snd_dec.h): a piece from pos, and the loop's start
               after it if it wraps in this buffer */
            int part, at = 0, used = 0;
            for (part = 0; part < 2; part++) {
                int len = part ? HALF - n : n;
                if (part) {
                    if (!(ch->left <= 0 && ch->loop && ch->looplen > 0))
                        break;
                    if (len > ch->looplen) len = ch->looplen;
                    ch->pos = ch->loopstart;
                    ch->left = ch->looplen;
                    if (len <= 0)
                        break;
                }
                lv[na] = ch->lvol;
                rv[na] = ch->rvol;
                lo[na] = at;
                hi[na] = at + len;
                if (ch->fmt == SF_PACK) {
                    /* 4 bits: decoded, then mixed as it is */
                    sdec_t *d = &ch->sd;
                    pk_decode(d, ch->pos, len, ch->dec + used);
                    p[na] = ch->dec + used - at;        /* indexed from the buffer start */
                    off[na] = 0;
                    hf[na] = 0;
                    used += len;
                } else {
                    /* half rate: read at half speed, sample (off + i) / 2 */
                    const uint8_t *src = ch->data;
                    if (ch->fmt & SF_PACK) {
                        int32_t k0 = ch->pos >> 1, k1 = (ch->pos + len - 1) >> 1;
                        pk_decode(&ch->sd, k0, k1 - k0 + 1, ch->dec + used);
                        src = ch->dec + used - k0;
                        used += k1 - k0 + 1;
                    }
                    p[na] = src;
                    off[na] = ch->pos - at;
                    hf[na] = 1;
                }
                na++;
                ch->pos += len;
                ch->left -= len;
                at += len;
            }
            continue;
        }
        p[na] = ch->data;
        lv[na] = ch->lvol;
        rv[na] = ch->rvol;
        lo[na] = 0;
        hi[na] = n;
        off[na] = 0;
        hf[na] = 0;
        na++;
        ch->data += n;
        ch->left -= n;
        if (ch->left <= 0 && ch->loop && ch->looplen > 0) {
            /* back to the loop start for the rest of the buffer */
            int rest = HALF - n;
            if (rest > ch->looplen) rest = ch->looplen;
            ch->data = ch->loop;
            ch->left = ch->looplen;
            if (rest > 0) {
                p[na] = ch->data - n;       /* indexed from the buffer start */
                lv[na] = ch->lvol;
                rv[na] = ch->rvol;
                lo[na] = n;
                hi[na] = n + rest;
                off[na] = 0;
                hf[na] = 0;
                na++;
                ch->data += rest;
                ch->left -= rest;
            }
        }
    }
    if (!na) {
        /* nothing playing: both buffers hold silence after two passes */
        uint32_t w = ((uint32_t)mid << 16) | mid;
        if (silent_bufs >= 2)
            return;
        silent_bufs++;
        for (i = 0; i < BUFSAMPLES; i++)
            out[i] = w;
        return;
    }
    silent_bufs = 0;
    mix_segments(out, p, lv, rv, lo, hi, off, hf, na, mid);
}

#ifndef HOST
static void dma_start(int b)
{
    (void)DMA_CHCR1;                /* read TE before clearing it */
    DMA_CHCR1 = 0;
    DMA_SAR1 = (uint32_t)UNCACHED(mixbuf[b]);
    DMA_TCR1 = BUFSAMPLES;
    DMA_CHCR1 = CHCR_GO;
}

/* DMA channel 1 transfer end (vector 72): play the buffer mixed last time,
 * mix the next one into the buffer just played */
void __attribute__((interrupt_handler)) snd_dma_isr(void)
{
    int done = cur_buf;
    cur_buf ^= 1;
    dma_start(cur_buf);
    mix((uint32_t *)UNCACHED(mixbuf[done]));
}

/* the slave sets up the PWM and its own DMA channel, then lets the DMA
 * interrupt in (it renders with interrupts at level 3 and below masked) */
void snd_init_slave(void)
{
    int i;
    for (i = 0; i < NCHAN; i++) {
        chans[i].left = 0;
        chans[i].sound = -1;
    }
    silent_bufs = 2;
    /* PWM: 22 kHz, left and right outputs from their own registers,
       DMA request (RTP) when the FIFO has room */
    pwm_cycle = (((23011360 << 1) / OUT_RATE + 1) >> 1) + 1;
    PWM_CYCLE = pwm_cycle;
    PWM_CTRL = 0x0185;
    PWM_MONO = pwm_cycle >> 1;
    PWM_MONO = pwm_cycle >> 1;
    PWM_MONO = pwm_cycle >> 1;
    for (i = 0; i < BUFSAMPLES; i++)
        mixbuf[0][i] = mixbuf[1][i] = ((uint32_t)(pwm_cycle >> 1) << 16) | (pwm_cycle >> 1);

    DMA_CHCR1 = 0;
    DMA_DRCR1 = 0;                  /* DREQ1: the PWM */
    DMA_DAR1 = 0x20004034;          /* a long here writes left and right */
    DMA_VCR1 = 72;
    DMA_DMAOR = 1;                  /* DMA on */
    INT_IPRA = (INT_IPRA & 0xF0FF) | 0x0400;   /* DMAC: priority 4 */
    cur_buf = 0;
    *(volatile int32_t *)QV(&snd_running) = 1;
    dma_start(0);
    __asm__ volatile("ldc %0, sr" : : "r"(0x30));   /* interrupt mask 3 */
}
#else
void snd_init_slave(void) {}
/* host: mix into a buffer the harness can save */
void snd_host_mix(int16_t *out, int frames)
{
    static uint32_t buf[BUFSAMPLES];
    int i, k = 0;
    pwm_cycle = 1045;
    while (k < frames) {
        mix(buf);
        for (i = 0; i < BUFSAMPLES && k < frames; i += 2, k++) {
            out[2 * k] = (int16_t)(((int)(buf[i] >> 16) - (pwm_cycle >> 1)) * 64);
            out[2 * k + 1] = (int16_t)(((int)(buf[i] & 0xFFFF) - (pwm_cycle >> 1)) * 64);
        }
    }
}
#endif

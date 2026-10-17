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

#define MIX_RATE    11025           /* the samples' own rate */
#define OUT_RATE    (MIX_RATE * 2)  /* PWM rate: each mixed sample twice */
#define NDYN        6               /* channels for sounds that play once */
#define NSTATIC     2               /* looping channels for static sounds */
#define NCHAN       (NDYN + NSTATIC)
#define BUFSAMPLES  256             /* output samples per DMA buffer (11.6 ms) */
#define QSIZE       16

#define CMD_STOP_ALL 255
#define CMD_SILENCE  254

typedef struct {
    const uint8_t *data;            /* 8-bit unsigned, in ROM */
    int32_t  left;                  /* samples remaining */
    int16_t  lvol, rvol;            /* 0..255 */
    const uint8_t *loop;            /* where a looping sound starts again */
    int32_t  looplen;
    int16_t  sound;
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
static struct { fixed_t org[3]; uint8_t sound, vol; } statics[MAX_STATIC];
static int nstatic;
static uint8_t st_sound[NSTATIC], st_lvol[NSTATIC], st_rvol[NSTATIC];

static void put_cmd(int sound, int lv, int rv, int chan)
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
void snd_init(void)
{
    int i;
    /* (the slave reads these two cache-through) */
    sound_table = (const int32_t *)(lvl.base + lvl.hdr->lump[LVL_SOUNDS].ofs);
    sound_base = lvl.base;
    nstatic = 0;
    for (i = 0; i < NSTATIC; i++)
        st_sound[i] = CMD_SILENCE;
}

void snd_stop_all(void)
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
static int spatialize(const fixed_t *org, int vol, int att, int *lv, int *rv)
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
    statics[nstatic].org[0] = org[0];
    statics[nstatic].org[1] = org[1];
    statics[nstatic].org[2] = org[2];
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
        int dx = UNITS(statics[i].org[0] - view.org[0]), dy = UNITS(statics[i].org[1] - view.org[1]);
        if (dx < -340 || dx > 340 || dy < -340 || dy > 340)
            continue;
        if (!spatialize(statics[i].org, statics[i].vol, 3, &lv, &rv))
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

static void take_commands(void)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    volatile sndcmd_t *q = (volatile sndcmd_t *)QV(cmdq);
    const int32_t *tab = *(const int32_t *volatile *)QV(&sound_table);
    const uint8_t *base = *(const uint8_t *volatile *)QV(&sound_base);
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
                const int32_t *e = tab + s * 3;
                ch->data = base + e[0];
                ch->left = e[1];
                ch->loop = e[2] >= 0 ? base + e[0] + e[2] : base + e[0];
                ch->looplen = e[2] >= 0 ? e[1] - e[2] : e[1];
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
        chans[best].data = base + tab[s * 3];
        chans[best].left = tab[s * 3 + 1];
        chans[best].loop = NULL;
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

static void mix(uint32_t *out)
{
    const uint8_t *p[NCHAN + NSTATIC];
    int lv[NCHAN + NSTATIC], rv[NCHAN + NSTATIC], lo[NCHAN + NSTATIC], hi[NCHAN + NSTATIC];
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
        p[na] = ch->data;
        lv[na] = ch->lvol;
        rv[na] = ch->rvol;
        lo[na] = 0;
        hi[na] = n;
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
    for (i = 0; i < HALF; i++) {
        /* +-128 * 255 per channel: >> 7 gives about +-255 for one loud
           sound against a mid point near 520 */
        int32_t al = 0, ar = 0;
        int l, r;
        uint32_t w;
        for (k = 0; k < na; k++)
            if (i >= lo[k] && i < hi[k]) {
                int v = (int)p[k][i] - 128;
                al += v * lv[k];
                ar += v * rv[k];
            }
        l = mid + (al >> 7);
        r = mid + (ar >> 7);
        if (l < 1) l = 1;
        if (l > pwm_cycle - 1) l = pwm_cycle - 1;
        if (r < 1) r = 1;
        if (r > pwm_cycle - 1) r = pwm_cycle - 1;
        w = ((uint32_t)l << 16) | (uint32_t)r;
        out[0] = w;
        out[1] = w;
        out += 2;
    }
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

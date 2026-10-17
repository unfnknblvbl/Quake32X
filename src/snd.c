/* Sound: Quake's 11 kHz samples, mixed in stereo on the slave SH-2 and
 * played through the 32X PWM at 22 kHz.
 *
 * The slave's DMA channel 1 feeds the PWM: each PWM request moves one long
 * (left and right pulse widths) from a mix buffer. When a buffer is done,
 * the DMA-end interrupt (vector 72, priority 4) restarts the DMA on the
 * other buffer and mixes the next one. The master starts sounds by putting
 * commands in a small queue (cache-through), so the mixer's channel state
 * belongs to the slave alone. The interrupt never touches the hardware
 * divider, which the interrupted renderer may be in the middle of using. */
#include "g_local.h"

#define MIX_RATE    11025           /* the samples' own rate */
#define OUT_RATE    (MIX_RATE * 2)  /* PWM rate: each mixed sample twice */
#define NCHAN       6
#define BUFSAMPLES  256             /* output samples per DMA buffer (11.6 ms) */
#define QSIZE       8

typedef struct {
    const uint8_t *data;            /* 8-bit unsigned, in ROM */
    int32_t  left;                  /* samples remaining */
    int16_t  lvol, rvol;            /* 0..255 */
} chan_t;

typedef struct {
    uint8_t  sound, lvol, rvol, pad;
} sndcmd_t;

static chan_t chans[NCHAN];         /* the slave's */
static uint32_t mixbuf[2][BUFSAMPLES] __attribute__((aligned(16), unused));
static sndcmd_t cmdq[QSIZE];
static volatile uint16_t q_head, q_tail; /* written by master / slave (cache-through) */
static int pwm_cycle, cur_buf __attribute__((unused));
static const uint32_t *sound_table;
static int nsounds;

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

/* everything here runs on the master before the slave starts sound */
void snd_init(void)
{
    sound_table = (const uint32_t *)(lvl.base + lvl.hdr->lump[LVL_SOUNDS].ofs);
    nsounds = lvl.hdr->lump[LVL_SOUNDS].count;
    q_head = q_tail = 0;
}

/* start a sound: org NULL for the player's own (full volume, centred) */
GCODE void snd_start(int sound, const fixed_t *org, int vol)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    volatile sndcmd_t *q = (volatile sndcmd_t *)QV(cmdq);
    int lv = vol, rv = vol, h;
    if (sound < 0 || sound >= nsounds)
        return;
    if (org) {
        /* Quake's SND_Spatialize: fades out by 1000 units, panned by the
           listener's right vector */
        fixed_t d[3], len, dot;
        int att;
        d[0] = org[0] - view.org[0];
        d[1] = org[1] - view.org[1];
        d[2] = org[2] - view.org[2];
        len = FX(vlen_units(org, view.org));
        att = 256 - hw_div(UNITS(len) * 256, 1000);
        if (att <= 0)
            return;
        dot = len > FX(1) ? hw_fixdiv(dot16(d, view.right), len) : 0;   /* -1 .. 1 */
        lv = (vol * att >> 8) * (0x10000 - dot) >> 16;
        rv = (vol * att >> 8) * (0x10000 + dot) >> 16;
        if (lv > 255) lv = 255;
        if (rv > 255) rv = 255;
        if (lv < 0) lv = 0;
        if (rv < 0) rv = 0;
    }
    h = *head;
    if ((uint16_t)(h - *tail) >= QSIZE)
        return;                     /* queue full: drop it */
    q[h & (QSIZE - 1)].sound = sound;
    q[h & (QSIZE - 1)].lvol = lv;
    q[h & (QSIZE - 1)].rvol = rv;
    *head = h + 1;
}

void snd_update(void)
{
}

/* ------------------------------------------------------------ slave side */

static void take_commands(void)
{
    volatile uint16_t *head = (volatile uint16_t *)QV(&q_head);
    volatile uint16_t *tail = (volatile uint16_t *)QV(&q_tail);
    volatile sndcmd_t *q = (volatile sndcmd_t *)QV(cmdq);
    uint16_t t = *tail;
    while (t != *head) {
        const volatile sndcmd_t *c = &q[t & (QSIZE - 1)];
        int k, best = 0;
        /* a free channel, else the one closest to finishing */
        for (k = 0; k < NCHAN; k++) {
            if (chans[k].left <= 0) { best = k; break; }
            if (chans[k].left < chans[best].left) best = k;
        }
        chans[best].data = lvl.base + sound_table[c->sound * 2];
        chans[best].left = sound_table[c->sound * 2 + 1];
        chans[best].lvol = c->lvol;
        chans[best].rvol = c->rvol;
        t++;
    }
    *tail = t;
}

/* mix one buffer: each 11 kHz sample becomes two output samples */
/* mix one buffer: each 11 kHz sample becomes two output samples. Sample
 * by sample over the playing channels, so the sums stay in registers */
static int silent_bufs;

static void mix(uint32_t *out)
{
    const uint8_t *p[NCHAN];
    int lv[NCHAN], rv[NCHAN], n[NCHAN];
    int i, k, na = 0, mid = pwm_cycle >> 1;
    take_commands();
    for (k = 0; k < NCHAN; k++) {
        chan_t *ch = &chans[k];
        if (ch->left <= 0)
            continue;
        p[na] = ch->data;
        lv[na] = ch->lvol;
        rv[na] = ch->rvol;
        n[na] = ch->left < BUFSAMPLES / 2 ? ch->left : BUFSAMPLES / 2;
        ch->data += n[na];
        ch->left -= n[na];
        na++;
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
    for (i = 0; i < BUFSAMPLES / 2; i++) {
        /* +-128 * 255 per channel: >> 7 gives about +-255 for one loud
           sound against a mid point near 520 */
        int32_t al = 0, ar = 0;
        int l, r;
        uint32_t w;
        for (k = 0; k < na; k++)
            if (i < n[k]) {
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
    for (i = 0; i < NCHAN; i++)
        chans[i].left = 0;
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

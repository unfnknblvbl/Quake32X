/* sh2sim - cycle-approximate dual SH-2 + 32X bus model, for profiling.
 *
 *   sh2sim rom.32x syms.txt seconds [options]
 *     -pad FROM TO BUTTONS     hold buttons (UDLRABCS) between vblank FROM and TO
 *     -shot VBL file.ppm        dump the displayed frame at vblank VBL
 *     -video file               raw RGB24 320x224 of the displayed frame at every vblank
 *     -vlog file                one line per vblank: vbl COMM0 fps10
 *     -prof                     print per-function cycle profile
 *
 * Models: SH-2 issue timing (1 cycle/op, branches, multiplier latency, load-use
 * stall), unified 4 KB 4-way cache with 16-byte lines, write-through, and the
 * 32X bus wait states from the hardware manual:
 *   SDRAM: 12 clocks per 8-word burst read, 2 clocks per word write
 *   cartridge ROM: 2 + 6..15 wait clocks per 16-bit access (we use 2 + 8)
 *   frame buffer: write 3 clocks (5 when the 4-word FIFO is full), read 2 + 8
 *   system registers 2 + 1, VDP registers / palette 2 + 5
 *   divider: 39 clocks, reading early stalls
 * Both CPUs share one external bus; accesses are serialized.
 * The 68000 and Mega Drive VDP are not simulated (pad is scripted).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SH2_HZ        23011360
#define LINE_CYCLES   1462            /* SH-2 clocks per scanline (NTSC) */
#define FRAME_LINES   262
#define FRAME_CYCLES  (LINE_CYCLES * FRAME_LINES)
#define VBLANK_LINE   224

#define ROM_WORD      10              /* 2 + 8 wait */
#define SDRAM_BURST   12
#define SDRAM_WWORD   2
#define FB_WWORD      3
#define FB_WFULL      5
#define FB_RWORD      10
#define SYS_ACC       3
#define VDP_ACC       7
#define DIV_CYCLES    39

typedef struct {
    uint32_t r[16], pc, pr, sr, gbr, vbr, mach, macl;
    uint64_t cyc;
    /* cache */
    uint32_t tag[64][4];
    uint8_t  valid[64][4];
    uint32_t age[64][4];
    uint32_t agectr;
    uint8_t  ccr;
    /* on-chip */
    uint8_t  onchip[512];
    uint8_t  oram[4096];            /* data array (on-chip RAM in two-way mode) */
    uint32_t minsp;
    uint32_t dvsr, dvdnth, dvdntl;
    uint64_t div_ready;
    uint64_t mul_ready;
    int      load_reg;              /* register written by the previous load, -1 none */
    /* interrupts */
    uint16_t intmask;
    int      vint_pending;
    /* DMA channel 1 (the PWM's) */
    uint32_t sar1, dar1, tcr1, chcr1, vcr1, dmaor;
    uint64_t dma_words;
    int      id;
    int      sleeping;
    /* stats */
    uint64_t stall_sdramw, stall_sdram, stall_rom, stall_fb, stall_reg, stall_div, stall_mul, misses, ifetch_miss;
    uint64_t instrs;
} cpu_t;

static uint8_t rom[4 << 20];
static uint32_t romsize;
static uint8_t sdram[256 << 10];
static uint8_t fbmem[2][128 << 10];
static uint16_t cram[256];
static uint16_t comm[8];
static uint16_t vdp_mode, fbctl_fs, fs_pending = 0xFFFF;
static uint64_t bus_free;            /* shared external bus */
static int fb_fifo; static uint64_t fb_fifo_t;
static cpu_t cpu[2];
static uint64_t now_vbl;              /* vblank count */
/* PWM: 3-deep FIFOs per side, one sample out every (cycle - 1) clocks */
static uint16_t pwm_ctrl, pwm_cycle, pwm_fifo[2][3], pwm_out[2];
static int pwm_n[2];
static uint64_t pwm_next = ~0ULL;
static FILE *audio;
static uint64_t audio_frames;
static uint32_t watch_addr[4];
static int watch_n[4], nwatch;
static uint16_t pad_now;
static int noirq;           /* test: SH-2 VBlank interrupts never arrive */
/* render timing: GO = COMM4 write by master, slave done = COMM6 write, master done = its first COMM6 poll */
static uint64_t t_go, sum_master, sum_slave, nframes_r, t_sdone, sum_serial, t_flipreq, sum_flipwait, nflip, sum_upd, sum_hud;
static int master_done_seen;

/* profile */
typedef struct { uint32_t addr; char name[64]; } sym_t;
static sym_t syms[8192];
static uint32_t oram_base;
static int nsyms;
static uint64_t *prof[2];
#define NCAT 6   /* total, sdram read, sdram write, rom, fb, div+mul */
static uint64_t *profc[2][NCAT];
#define PROF_SHIFT 1
#define PROF_SIZE  (0x40000 >> PROF_SHIFT)
#define ROMPROF_SIZE ((4 << 20) >> PROF_SHIFT)
static uint64_t *romprof[2];

static uint32_t rd_be(const uint8_t *p, int size);
static void die(cpu_t *c, const char *msg, uint32_t v)
{
    int i;
    fprintf(stderr, "cpu%d: %s %08x at pc %08x pr %08x\n", c->id, msg, v, c->pc, c->pr);
    for (i = 0; i < 16; i++) fprintf(stderr, "r%d=%08x%s", i, c->r[i], (i & 3) == 3 ? "\n" : " ");
    if ((c->r[13] & 0x1FFFFFFF) >= 0x06000010 && (c->r[13] & 0x1FFFFFFF) < 0x06040000) {
        uint32_t a = (c->r[13] & 0x3FFFF) - 12;
        fprintf(stderr, "@r13-12:");
        for (i = 0; i < 24; i++) fprintf(stderr, " %02x", sdram[a + i]);
        fprintf(stderr, "\nargs @r14:");
        for (i = 0; i < 84; i += 4) fprintf(stderr, " %08x", rd_be(&c->oram[(c->r[14] + i) & 0xFFF], 4));
        fprintf(stderr, "\n");
    }
    exit(1);
}

/* ---------------------------------------------------------------- bus */

static void bus(cpu_t *c, int cycles, uint64_t *stat)
{
    uint64_t start = c->cyc > bus_free ? c->cyc : bus_free;
    uint64_t end = start + cycles;
    *stat += end - c->cyc;
    bus_free = end;
    c->cyc = end;
}

static inline int region(uint32_t a)
{
    a &= 0x1FFFFFFF;
    if (a >= 0x06000000 && a < 0x06040000) return 1;   /* sdram */
    if (a >= 0x02000000 && a < 0x02400000) return 2;   /* rom */
    if (a >= 0x04000000 && a < 0x04040000) return 3;   /* fb */
    if (a >= 0x00004000 && a < 0x00004400) return 4;   /* regs */
    return 0;
}

static void cache_fill(cpu_t *c, uint32_t a, int ifetch)
{
    int rg = region(a);
    if (rg == 1) bus(c, SDRAM_BURST, &c->stall_sdram);
    else if (rg == 2) bus(c, ROM_WORD * 8, &c->stall_rom);
    else if (rg == 3) bus(c, FB_RWORD * 8, &c->stall_fb);
    else bus(c, 4, &c->stall_reg);
    c->misses++;
    if (ifetch) c->ifetch_miss++;
}

/* returns 1 if access should go through cache */
static inline int cacheable(cpu_t *c, uint32_t a)
{
    return (c->ccr & 1) && (a >> 29) == 0 && region(a) != 4;
}

static void cache_access(cpu_t *c, uint32_t a, int ifetch)
{
    int set = (a >> 4) & 63, w, lru, w0 = (c->ccr & 8) ? 2 : 0;
    uint32_t tag = (a & 0x1FFFFFFF) >> 10;
    for (w = w0; w < 4; w++)
        if (c->valid[set][w] && c->tag[set][w] == tag) {
            c->age[set][w] = ++c->agectr;
            return;
        }
    lru = w0;
    for (w = w0; w < 4; w++) {
        if (!c->valid[set][w]) { lru = w; break; }
        if (c->age[set][w] < c->age[set][lru]) lru = w;
    }
    cache_fill(c, a, ifetch);
    c->tag[set][lru] = tag;
    c->valid[set][lru] = 1;
    c->age[set][lru] = ++c->agectr;
}

static void cache_purge(cpu_t *c)
{
    memset(c->valid, 0, sizeof(c->valid));
}

static uint32_t rd_be(const uint8_t *p, int size)
{
    if (size == 1) return p[0];
    if (size == 2) return (p[0] << 8) | p[1];
    return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}
static void wr_be(uint8_t *p, int size, uint32_t v)
{
    if (size == 1) p[0] = v;
    else if (size == 2) { p[0] = v >> 8; p[1] = v; }
    else { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
}

static int fb_visible(void) { return fbctl_fs & 1; }
static uint8_t *fb_access(void) { return fbmem[fb_visible() ^ 1]; }

static uint16_t fbctl_read(cpu_t *c)
{
    uint32_t line = (uint32_t)((c->cyc % FRAME_CYCLES) / LINE_CYCLES);
    uint16_t v = fbctl_fs & 1;
    if (line >= VBLANK_LINE) v |= 0x8000;
    v |= 0x2000;   /* PEN */
    return v;
}

static void pwm_push(int side, uint16_t v)
{
    if (pwm_n[side] < 3) pwm_fifo[side][pwm_n[side]++] = v & 0xFFF;
}

static uint32_t sdram_or_rom_read32(uint32_t a);

/* the PWM requests DMA while its FIFO has room (RTP); channel 1 of either
   CPU, if running on external requests, moves longs to the pulse registers */
static void dma_dreq(uint64_t t)
{
    int i;
    if (!(pwm_ctrl & 0x80)) return;
    for (i = 0; i < 2; i++) {
        cpu_t *c = &cpu[i];
        while ((c->dmaor & 1) && (c->chcr1 & 3) == 1 && !(c->chcr1 & 0x200) && c->tcr1 && pwm_n[0] < 3) {
            uint32_t v = sdram_or_rom_read32(c->sar1);
            uint32_t d = c->dar1 & 0x3F;
            if ((c->chcr1 & 0xC00) == 0x800) {            /* long */
                if (d == 0x34) { pwm_push(0, v >> 16); pwm_push(1, v); }
                else if (d == 0x38) { pwm_push(0, v); pwm_push(1, v); }
                c->sar1 += ((c->chcr1 >> 12) & 3) == 1 ? 4 : 0;
            } else {                                      /* word */
                uint16_t w = (c->sar1 & 2) ? v : v >> 16;
                if (d == 0x34) pwm_push(0, w); else if (d == 0x36) pwm_push(1, w);
                else { pwm_push(0, w); pwm_push(1, w); }
                c->sar1 += ((c->chcr1 >> 12) & 3) == 1 ? 2 : 0;
            }
            bus_free = (bus_free > t ? bus_free : t) + 4;
            c->dma_words++;
            if (--c->tcr1 == 0) c->chcr1 |= 2;           /* TE */
        }
    }
}

static void pwm_tick(void)
{
    int k;
    for (k = 0; k < 2; k++)
        if (pwm_n[k]) {
            pwm_out[k] = pwm_fifo[k][0];
            pwm_fifo[k][0] = pwm_fifo[k][1];
            pwm_fifo[k][1] = pwm_fifo[k][2];
            pwm_n[k]--;
        }
    if (audio) {
        int16_t smp[2];
        for (k = 0; k < 2; k++) {
            int mid = pwm_cycle / 2;
            int v = mid ? ((int)pwm_out[k] - mid) * 32767 / mid : 0;
            smp[k] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;
        }
        fwrite(smp, 2, 2, audio);
        audio_frames++;
    }
    dma_dreq(pwm_next);
    pwm_next += pwm_cycle > 1 ? pwm_cycle - 1 : 1000;
}

static uint32_t onchip_read(cpu_t *c, uint32_t a, int size)
{
    uint32_t off = a & 0x1FF;
    if (off >= 0x100 && off < 0x120) {
        if (c->cyc < c->div_ready) { c->stall_div += c->div_ready - c->cyc; c->cyc = c->div_ready; }
        switch (off & ~3) {
        case 0x100: return c->dvsr;
        case 0x104: case 0x114: case 0x11c: return c->dvdntl;
        case 0x110: case 0x118: return c->dvdnth;
        default: return 0;
        }
    }
    if (off == 0x92) return c->ccr;
    switch (off) {
    case 0x190: return c->sar1;
    case 0x194: return c->dar1;
    case 0x198: return c->tcr1;
    case 0x19C: return c->chcr1;
    case 0x1A8: return c->vcr1;
    case 0x1B0: return c->dmaor;
    }
    return rd_be(&c->onchip[off], size);
}

static void onchip_write(cpu_t *c, uint32_t a, int size, uint32_t v)
{
    uint32_t off = a & 0x1FF;
    if (off >= 0x100 && off < 0x120) {
        switch (off & ~3) {
        case 0x100: c->dvsr = v; break;
        case 0x104: {
            int32_t d = (int32_t)c->dvsr, n = (int32_t)v;
            if (d == 0) { c->dvdntl = n < 0 ? 0x80000000 : 0x7FFFFFFF; c->dvdnth = 0; }
            else { c->dvdntl = (uint32_t)(n / d); c->dvdnth = (uint32_t)(n % d); }
            c->div_ready = c->cyc + DIV_CYCLES;
            break;
        }
        case 0x110: c->dvdnth = v; break;
        case 0x114: {
            int64_t n = (int64_t)(((uint64_t)c->dvdnth << 32) | v);
            int32_t d = (int32_t)c->dvsr;
            int64_t q;
            if (d == 0) q = n < 0 ? -0x80000000LL : 0x7FFFFFFF;
            else {
                q = n / d;
                c->dvdnth = (uint32_t)(n % d);
            }
            if (q > 0x7FFFFFFF) q = 0x7FFFFFFF;
            if (q < -0x80000000LL) q = -0x80000000LL;
            c->dvdntl = (uint32_t)q;
            c->div_ready = c->cyc + DIV_CYCLES;
            break;
        }
        }
        return;
    }
    if (off == 0x92) {
        if (v & 0x10) cache_purge(c);
        c->ccr = v & 0xEF;
        return;
    }
    switch (off) {
    case 0x190: c->sar1 = v; return;
    case 0x194: c->dar1 = v; return;
    case 0x198: c->tcr1 = v & 0xFFFFFF; return;
    case 0x19C:
        /* TE is cleared by writing 0 after reading it as 1 */
        c->chcr1 = (v & ~2u) | (c->chcr1 & v & 2);
        dma_dreq(c->cyc);
        return;
    case 0x1A8: c->vcr1 = v & 0x7F; return;
    case 0x1B0: c->dmaor = v & 0xF; return;
    }
    wr_be(&c->onchip[off], size, v);
}

static uint32_t reg_read(cpu_t *c, uint32_t a, int size)
{
    uint32_t off = a & 0x3FF;
    if (off < 0x100) bus(c, SYS_ACC, &c->stall_reg);
    else bus(c, VDP_ACC, &c->stall_reg);
    if (off >= 0x20 && off < 0x30) {
        int i = (off - 0x20) >> 1;
        if (c->id == 0 && off == 0x26 && !master_done_seen && t_go) {
            master_done_seen = 1;
            sum_master += c->cyc - t_go;
        }
        if (size == 4) return ((uint32_t)comm[i] << 16) | comm[i + 1];
        if (size == 2) return comm[i];
        return (off & 1) ? (comm[i] & 0xFF) : (comm[i] >> 8);
    }
    if (off == 0x00 || off == 0x01) return size == 1 ? (off ? c->intmask & 0xFF : 0x80) : (0x8000 | c->intmask);
    if (off == 0x30) return pwm_ctrl;
    if (off == 0x32) return pwm_cycle;
    if (off >= 0x34 && off < 0x3A) {
        int sd = off == 0x36 ? 1 : 0;
        return (pwm_n[sd] >= 3 ? 0x8000 : 0) | (pwm_n[sd] == 0 ? 0x4000 : 0);
    }
    if (off == 0x100) return vdp_mode;
    if (off == 0x10A || off == 0x10B) return size == 1 ? (off & 1 ? fbctl_read(c) & 0xFF : fbctl_read(c) >> 8) : fbctl_read(c);
    if (off >= 0x200 && off < 0x400) return cram[(off - 0x200) >> 1];
    return 0;
}

static void reg_write(cpu_t *c, uint32_t a, int size, uint32_t v)
{
    uint32_t off = a & 0x3FF;
    if (off < 0x100) bus(c, SYS_ACC, &c->stall_reg);
    else bus(c, VDP_ACC, &c->stall_reg);
    if (off >= 0x20 && off < 0x30) {
        int i = (off - 0x20) >> 1;
        if (size == 2 && off == 0x24 && c->id == 0) {
            if (t_sdone) sum_serial += c->cyc - t_sdone;
            if (t_flipreq) sum_upd += c->cyc - t_flipreq;
            if (t_flipreq && t_sdone) sum_hud += t_flipreq - t_sdone;
            t_go = c->cyc; master_done_seen = 0;
        }
        if (size == 2 && off == 0x26 && c->id == 1 && t_go) { sum_slave += c->cyc - t_go; nframes_r++; t_sdone = c->cyc; }
        if (size == 4) { comm[i] = v >> 16; comm[i + 1] = v; }
        else if (size == 2) comm[i] = v;
        else if (off & 1) comm[i] = (comm[i] & 0xFF00) | (v & 0xFF);
        else comm[i] = (comm[i] & 0xFF) | (v << 8);
        return;
    }
    if (off == 0x30) { pwm_ctrl = v; return; }
    if (off == 0x32) {
        pwm_cycle = v & 0xFFF;
        if (pwm_next == ~0ULL) {
            pwm_next = c->cyc + pwm_cycle;
            /* silence from power-on, so the audio lines up with the video */
            if (audio && pwm_cycle > 1) {
                uint64_t n = c->cyc / (pwm_cycle - 1);
                static const int16_t z[2] = { 0, 0 };
                while (n--) { fwrite(z, 2, 2, audio); audio_frames++; }
            }
        }
        return;
    }
    if (off == 0x34) {
        if (size == 4) { pwm_push(0, v >> 16); pwm_push(1, v); }
        else pwm_push(0, v);
        return;
    }
    if (off == 0x36) { pwm_push(1, v); return; }
    if (off == 0x38) { pwm_push(0, v); pwm_push(1, v); return; }
    if (off == 0x01 && size == 1) { c->intmask = v & 0x0F; return; }
    if (off == 0x00 && size == 2) { c->intmask = v & 0x0F; return; }
    if (off == 0x16) { c->vint_pending = 0; return; }
    if (off == 0x100) { vdp_mode = v; return; }
    if (off == 0x10A || off == 0x10B) { fs_pending = v & 1; t_flipreq = c->cyc; return; }
    if (off >= 0x200 && off < 0x400) { cram[(off - 0x200) >> 1] = v; return; }
}

static uint32_t sdram_or_rom_read32(uint32_t a)
{
    uint32_t pa = a & 0x1FFFFFFF;
    if (pa >= 0x06000000 && pa < 0x06040000) return rd_be(&sdram[pa - 0x06000000], 4);
    if (pa >= 0x02000000 && pa - 0x02000000 < romsize) return rd_be(&rom[pa - 0x02000000], 4);
    return 0;
}

static uint32_t mem_read(cpu_t *c, uint32_t a, int size)
{
    uint32_t pa;
    int rg;
    if ((a >> 24) == 0xFF) return onchip_read(c, a, size);
    if ((a >> 29) == 6) return rd_be(&c->oram[a & 0xFFF], size);
    if (a & (size - 1)) die(c, "unaligned read", a);
    pa = a & 0x1FFFFFFF;
    rg = region(a);
    if (rg == 4) return reg_read(c, a, size);
    if (cacheable(c, a)) cache_access(c, a, 0);
    else {
        if (rg == 1) bus(c, SDRAM_BURST, &c->stall_sdram);
        else if (rg == 2) bus(c, ROM_WORD * (size == 4 ? 2 : 1), &c->stall_rom);
        else if (rg == 3) bus(c, FB_RWORD * (size == 4 ? 2 : 1), &c->stall_fb);
    }
    switch (rg) {
    case 1: return rd_be(&sdram[pa - 0x06000000], size);
    case 2: return pa - 0x02000000 < romsize ? rd_be(&rom[pa - 0x02000000], size) : 0xFFFFFFFF;
    case 3: return rd_be(&fb_access()[(pa - 0x04000000) & 0x1FFFF], size);
    }
    if (pa < 0x4000) return 0;
    die(c, "bad read", a);
    return 0;
}

static void mem_write(cpu_t *c, uint32_t a, int size, uint32_t v)
{
    uint32_t pa;
    int rg;
    if ((a >> 24) == 0xFF) { onchip_write(c, a, size, v); return; }
    if ((a >> 29) == 6) { wr_be(&c->oram[a & 0xFFF], size, v); return; }
    if (a & (size - 1)) die(c, "unaligned write", a);
    pa = a & 0x1FFFFFFF;
    rg = region(a);
    if (rg == 4) { reg_write(c, a, size, v); return; }
    switch (rg) {
    case 1:
        bus(c, SDRAM_WWORD * (size == 4 ? 2 : 1), &c->stall_sdramw);
        wr_be(&sdram[pa - 0x06000000], size, v);
        return;
    case 3: {
        int words = size == 4 ? 2 : 1, k;
        uint8_t *fb = fb_access();
        for (k = 0; k < words; k++) {
            /* FIFO drains one word every FB_WFULL clocks */
            uint64_t drained = (c->cyc - fb_fifo_t) / FB_WFULL;
            if (drained) { fb_fifo -= (int)(drained > (uint64_t)fb_fifo ? fb_fifo : drained); fb_fifo_t = c->cyc; }
            if (fb_fifo >= 4) bus(c, FB_WFULL, &c->stall_fb);
            else { bus(c, FB_WWORD, &c->stall_fb); fb_fifo++; }
        }
        pa = (pa - 0x04000000) & 0x3FFFF;
        if (pa >= 0x20000) {   /* overwrite image: zero bytes are skipped */
            pa &= 0x1FFFF;
            if (size == 1) { if (v & 0xFF) fb[pa] = v; }
            else if (size == 2) { if (v >> 8 & 0xFF) fb[pa] = v >> 8; if (v & 0xFF) fb[pa + 1] = v; }
            return;
        }
        wr_be(&fb[pa], size, v);
        return;
    }
    case 2: return;
    }
    if (pa < 0x4000) return;
    die(c, "bad write", a);
}

static inline uint16_t fetch(cpu_t *c, uint32_t a)
{
    uint32_t pa = a & 0x1FFFFFFF;
    if ((a >> 29) == 6) return rd_be(&c->oram[a & 0xFFF], 2);
    if (cacheable(c, a)) cache_access(c, a, 1);
    else if (region(a) == 1) bus(c, SDRAM_BURST, &c->stall_sdram);
    else if (region(a) == 2) bus(c, ROM_WORD, &c->stall_rom);
    if (pa >= 0x06000000 && pa < 0x06040000) return rd_be(&sdram[pa - 0x06000000], 2);
    if (pa >= 0x02000000 && pa < 0x02400000) return rd_be(&rom[pa - 0x02000000], 2);
    die(c, "bad fetch", a);
    return 0;
}

/* ---------------------------------------------------------------- cpu */

#define T (c->sr & 1)
#define SETT(x) (c->sr = (c->sr & ~1u) | ((x) ? 1 : 0))
#define Q ((c->sr >> 8) & 1)
#define M ((c->sr >> 9) & 1)
#define SETQ(x) (c->sr = (c->sr & ~0x100u) | ((x) ? 0x100 : 0))
#define SETM(x) (c->sr = (c->sr & ~0x200u) | ((x) ? 0x200 : 0))
#define R c->r

static void mul_wait(cpu_t *c)
{
    if (c->cyc < c->mul_ready) { c->stall_mul += c->mul_ready - c->cyc; c->cyc = c->mul_ready; }
}

static int exec(cpu_t *c, uint32_t pc, int in_delay);

static void delay_slot(cpu_t *c, uint32_t pc)
{
    exec(c, pc, 1);
}

/* executes one instruction at pc; returns 1 if a branch set c->pc */
static int exec(cpu_t *c, uint32_t pc, int in_delay)
{
    uint16_t op = fetch(c, pc);
    int n = (op >> 8) & 15, m = (op >> 4) & 15;
    int32_t disp;
    uint32_t tmp, tmp2;
    int load = -1, reads = -1, reads2 = -1;
    c->instrs++;
    c->cyc += 1;

#define BRANCH(target, cycles) do { uint32_t _t = (target); c->cyc += (cycles) - 1; \
        if (in_delay) die(c, "branch in delay slot", op); delay_slot(c, pc + 2); c->pc = _t; goto done_branch; } while (0)

    switch (op >> 12) {
    case 0x0:
        switch (op & 15) {
        case 2:
            if (m == 0) R[n] = c->sr;
            else if (m == 1) R[n] = c->gbr;
            else if (m == 2) R[n] = c->vbr;
            else goto bad;
            break;
        case 3:
            if (m == 0) { c->pr = pc + 4; BRANCH(pc + 4 + R[n], 2); }
            if (m == 2) BRANCH(pc + 4 + R[n], 2);
            goto bad;
        case 4: reads = n; mem_write(c, R[0] + R[n], 1, R[m]); break;
        case 5: reads = n; mem_write(c, R[0] + R[n], 2, R[m]); break;
        case 6: reads = n; mem_write(c, R[0] + R[n], 4, R[m]); break;
        case 7: mul_wait(c); c->macl = R[n] * R[m]; c->cyc += 1; c->mul_ready = c->cyc + 2; break;
        case 8:
            if (op == 0x0008) SETT(0);
            else if (op == 0x0018) SETT(1);
            else if (op == 0x0028) c->mach = c->macl = 0;
            else goto bad;
            break;
        case 9:
            if (op == 0x0009) break;
            if (op == 0x0019) { SETQ(0); SETM(0); SETT(0); break; }
            if (m == 2) { R[n] = T; break; }
            goto bad;
        case 10:
            if (m == 0) { mul_wait(c); R[n] = c->mach; }
            else if (m == 1) { mul_wait(c); R[n] = c->macl; }
            else if (m == 2) R[n] = c->pr;
            else goto bad;
            break;
        case 11:
            if (op == 0x000B) BRANCH(c->pr, 2);
            if (op == 0x001B) { c->sleeping = 1; break; }
            if (op == 0x002B) {
                uint32_t npc = mem_read(c, R[15], 4);
                R[15] += 4;
                c->sr = mem_read(c, R[15], 4) & 0x3F3;
                R[15] += 4;
                BRANCH(npc, 4);
            }
            goto bad;
        case 12: reads = 0; R[n] = (int32_t)(int8_t)mem_read(c, R[0] + R[m], 1); load = n; break;
        case 13: reads = 0; R[n] = (int32_t)(int16_t)mem_read(c, R[0] + R[m], 2); load = n; break;
        case 14: reads = 0; R[n] = mem_read(c, R[0] + R[m], 4); load = n; break;
        case 15: {  /* mac.l */
            int64_t a = (int32_t)mem_read(c, R[n], 4), b;
            R[n] += 4;
            b = (int32_t)mem_read(c, R[m], 4);
            R[m] += 4;
            mul_wait(c);
            {
                int64_t acc = (int64_t)(((uint64_t)c->mach << 32) | c->macl) + a * b;
                c->mach = (uint32_t)(acc >> 32); c->macl = (uint32_t)acc;
            }
            c->cyc += 1; c->mul_ready = c->cyc + 2;
            break;
        }
        default: goto bad;
        }
        break;
    case 0x1:
        reads = n;
        mem_write(c, R[n] + ((op & 15) << 2), 4, R[m]);
        break;
    case 0x2:
        reads = n;
        switch (op & 15) {
        case 0: mem_write(c, R[n], 1, R[m]); break;
        case 1: mem_write(c, R[n], 2, R[m]); break;
        case 2: mem_write(c, R[n], 4, R[m]); break;
        case 4: mem_write(c, R[n] - 1, 1, R[m]); R[n] -= 1; break;
        case 5: mem_write(c, R[n] - 2, 2, R[m]); R[n] -= 2; break;
        case 6: mem_write(c, R[n] - 4, 4, R[m]); R[n] -= 4; break;
        case 7: SETQ(R[n] >> 31); SETM(R[m] >> 31); SETT(Q != M); break;
        case 8: SETT((R[n] & R[m]) == 0); break;
        case 9: R[n] &= R[m]; break;
        case 10: R[n] ^= R[m]; break;
        case 11: R[n] |= R[m]; break;
        case 12: tmp = R[n] ^ R[m];
            SETT(!(tmp & 0xFF000000) || !(tmp & 0xFF0000) || !(tmp & 0xFF00) || !(tmp & 0xFF)); break;
        case 13: R[n] = (R[n] >> 16) | (R[m] << 16); break;
        case 14: mul_wait(c); c->macl = (uint32_t)((uint16_t)R[n] * (uint16_t)R[m]); c->mul_ready = c->cyc + 2; break;
        case 15: mul_wait(c); c->macl = (uint32_t)((int16_t)R[n] * (int16_t)R[m]); c->mul_ready = c->cyc + 2; break;
        default: goto bad;
        }
        reads2 = m;
        break;
    case 0x3:
        reads = n; reads2 = m;
        switch (op & 15) {
        case 0: SETT(R[n] == R[m]); break;
        case 2: SETT(R[n] >= R[m]); break;
        case 3: SETT((int32_t)R[n] >= (int32_t)R[m]); break;
        case 4: {   /* div1 */
            uint32_t tmp0, t2 = R[m];
            int oldq = Q, tmp1;
            SETQ(R[n] >> 31);
            R[n] = (R[n] << 1) | T;
            if (!oldq) {
                if (!M) { tmp0 = R[n]; R[n] -= t2; tmp1 = R[n] > tmp0; SETQ(Q ? !tmp1 : tmp1); }
                else    { tmp0 = R[n]; R[n] += t2; tmp1 = R[n] < tmp0; SETQ(Q ? tmp1 : !tmp1); }
            } else {
                if (!M) { tmp0 = R[n]; R[n] += t2; tmp1 = R[n] < tmp0; SETQ(Q ? !tmp1 : tmp1); }
                else    { tmp0 = R[n]; R[n] -= t2; tmp1 = R[n] > tmp0; SETQ(Q ? tmp1 : !tmp1); }
            }
            SETT(Q == M);
            break;
        }
        case 5: { uint64_t p = (uint64_t)R[n] * R[m]; mul_wait(c);
            c->mach = p >> 32; c->macl = (uint32_t)p; c->cyc += 1; c->mul_ready = c->cyc + 2; break; }
        case 6: SETT(R[n] > R[m]); break;
        case 7: SETT((int32_t)R[n] > (int32_t)R[m]); break;
        case 8: R[n] -= R[m]; break;
        case 10: { uint32_t a = R[n], b = R[m], r2 = a - b - T; SETT(a < b || (a - b) < T); R[n] = r2; break; }
        case 11: { int32_t a = R[n], b = R[m]; int64_t r2 = (int64_t)a - b; R[n] = (uint32_t)r2; SETT(r2 != (int32_t)r2); break; }
        case 12: R[n] += R[m]; break;
        case 13: { int64_t p = (int64_t)(int32_t)R[n] * (int32_t)R[m]; mul_wait(c);
            c->mach = (uint64_t)p >> 32; c->macl = (uint32_t)p; c->cyc += 1; c->mul_ready = c->cyc + 2; break; }
        case 14: { uint64_t s = (uint64_t)R[n] + R[m] + T; R[n] = (uint32_t)s; SETT(s >> 32); break; }
        case 15: { int32_t a = R[n], b = R[m]; int64_t r2 = (int64_t)a + b; R[n] = (uint32_t)r2; SETT(r2 != (int32_t)r2); break; }
        default: goto bad;
        }
        break;
    case 0x4:
        reads = n;
        switch (op & 0xFF) {
        case 0x00: SETT(R[n] >> 31); R[n] <<= 1; break;
        case 0x10: R[n]--; SETT(R[n] == 0); break;
        case 0x20: SETT(R[n] >> 31); R[n] <<= 1; break;
        case 0x01: SETT(R[n] & 1); R[n] >>= 1; break;
        case 0x11: SETT((int32_t)R[n] >= 0); break;
        case 0x21: SETT(R[n] & 1); R[n] = (int32_t)R[n] >> 1; break;
        case 0x02: R[n] -= 4; mem_write(c, R[n], 4, c->mach); break;
        case 0x12: R[n] -= 4; mul_wait(c); mem_write(c, R[n], 4, c->macl); break;
        case 0x22: R[n] -= 4; mem_write(c, R[n], 4, c->pr); break;
        case 0x03: R[n] -= 4; mem_write(c, R[n], 4, c->sr); c->cyc += 1; break;
        case 0x13: R[n] -= 4; mem_write(c, R[n], 4, c->gbr); c->cyc += 1; break;
        case 0x23: R[n] -= 4; mem_write(c, R[n], 4, c->vbr); c->cyc += 1; break;
        case 0x04: SETT(R[n] >> 31); R[n] = (R[n] << 1) | T; break;
        case 0x24: tmp = T; SETT(R[n] >> 31); R[n] = (R[n] << 1) | tmp; break;
        case 0x05: SETT(R[n] & 1); R[n] = (R[n] >> 1) | (T << 31); break;
        case 0x15: SETT((int32_t)R[n] > 0); break;
        case 0x25: tmp = T; SETT(R[n] & 1); R[n] = (R[n] >> 1) | (tmp << 31); break;
        case 0x06: c->mach = mem_read(c, R[n], 4); R[n] += 4; break;
        case 0x16: c->macl = mem_read(c, R[n], 4); R[n] += 4; break;
        case 0x26: c->pr = mem_read(c, R[n], 4); R[n] += 4; break;
        case 0x07: c->sr = mem_read(c, R[n], 4) & 0x3F3; R[n] += 4; c->cyc += 2; break;
        case 0x17: c->gbr = mem_read(c, R[n], 4); R[n] += 4; c->cyc += 2; break;
        case 0x27: c->vbr = mem_read(c, R[n], 4); R[n] += 4; c->cyc += 2; break;
        case 0x08: R[n] <<= 2; break;
        case 0x18: R[n] <<= 8; break;
        case 0x28: R[n] <<= 16; break;
        case 0x09: R[n] >>= 2; break;
        case 0x19: R[n] >>= 8; break;
        case 0x29: R[n] >>= 16; break;
        case 0x0A: c->mach = R[n]; break;
        case 0x1A: c->macl = R[n]; break;
        case 0x2A: c->pr = R[n]; break;
        case 0x0B: c->pr = pc + 4; BRANCH(R[n], 2);
        case 0x1B: tmp = mem_read(c, R[n], 1); SETT(tmp == 0); mem_write(c, R[n], 1, tmp | 0x80); c->cyc += 3; break;
        case 0x2B: BRANCH(R[n], 2);
        case 0x0E: c->sr = R[n] & 0x3F3; break;
        case 0x1E: c->gbr = R[n]; break;
        case 0x2E: c->vbr = R[n]; break;
        default:
            if ((op & 15) == 15) {  /* mac.w */
                int32_t a = (int16_t)mem_read(c, R[n], 2), b;
                R[n] += 2;
                b = (int16_t)mem_read(c, R[m], 2);
                R[m] += 2;
                mul_wait(c);
                {
                    int64_t acc = (int64_t)(((uint64_t)c->mach << 32) | c->macl) + (int64_t)a * b;
                    c->mach = (uint32_t)(acc >> 32); c->macl = (uint32_t)acc;
                }
                c->mul_ready = c->cyc + 2;
                break;
            }
            goto bad;
        }
        break;
    case 0x5:
        reads = m;
        R[n] = mem_read(c, R[m] + ((op & 15) << 2), 4); load = n;
        break;
    case 0x6:
        reads = m;
        switch (op & 15) {
        case 0: R[n] = (int32_t)(int8_t)mem_read(c, R[m], 1); load = n; break;
        case 1: R[n] = (int32_t)(int16_t)mem_read(c, R[m], 2); load = n; break;
        case 2: R[n] = mem_read(c, R[m], 4); load = n; break;
        case 3: R[n] = R[m]; break;
        case 4: tmp = (int32_t)(int8_t)mem_read(c, R[m], 1); if (n != m) R[m] += 1; R[n] = tmp; load = n; break;
        case 5: tmp = (int32_t)(int16_t)mem_read(c, R[m], 2); if (n != m) R[m] += 2; R[n] = tmp; load = n; break;
        case 6: tmp = mem_read(c, R[m], 4); if (n != m) R[m] += 4; R[n] = tmp; load = n; break;
        case 7: R[n] = ~R[m]; break;
        case 8: R[n] = (R[m] & 0xFFFF0000) | ((R[m] & 0xFF) << 8) | ((R[m] >> 8) & 0xFF); break;
        case 9: R[n] = (R[m] >> 16) | (R[m] << 16); break;
        case 10: tmp = 0 - R[m]; tmp2 = tmp - T; SETT(0 < tmp || tmp < tmp2); R[n] = tmp2; break;
        case 11: R[n] = 0 - R[m]; break;
        case 12: R[n] = R[m] & 0xFF; break;
        case 13: R[n] = R[m] & 0xFFFF; break;
        case 14: R[n] = (int32_t)(int8_t)R[m]; break;
        case 15: R[n] = (int32_t)(int16_t)R[m]; break;
        }
        break;
    case 0x7:
        reads = n;
        R[n] += (int32_t)(int8_t)op;
        break;
    case 0x8:
        switch (n) {
        case 0: reads = m; mem_write(c, R[m] + (op & 15), 1, R[0]); break;
        case 1: reads = m; mem_write(c, R[m] + ((op & 15) << 1), 2, R[0]); break;
        case 4: reads = m; R[0] = (int32_t)(int8_t)mem_read(c, R[m] + (op & 15), 1); load = 0; break;
        case 5: reads = m; R[0] = (int32_t)(int16_t)mem_read(c, R[m] + ((op & 15) << 1), 2); load = 0; break;
        case 8: reads = 0; SETT(R[0] == (uint32_t)(int32_t)(int8_t)op); break;
        case 9: if (T) { c->pc = pc + 4 + ((int32_t)(int8_t)op << 1); c->cyc += 2; goto done_branch; } break;
        case 11: if (!T) { c->pc = pc + 4 + ((int32_t)(int8_t)op << 1); c->cyc += 2; goto done_branch; } break;
        case 13: if (T) BRANCH(pc + 4 + ((int32_t)(int8_t)op << 1), 2); break;
        case 15: if (!T) BRANCH(pc + 4 + ((int32_t)(int8_t)op << 1), 2); break;
        default: goto bad;
        }
        break;
    case 0x9:
        R[n] = (int32_t)(int16_t)mem_read(c, pc + 4 + ((op & 0xFF) << 1), 2); load = n;
        break;
    case 0xA:
        disp = (int32_t)(op << 20) >> 19;
        BRANCH(pc + 4 + disp, 2);
    case 0xB:
        disp = (int32_t)(op << 20) >> 19;
        c->pr = pc + 4;
        BRANCH(pc + 4 + disp, 2);
    case 0xC:
        switch (n) {
        case 0: mem_write(c, c->gbr + (op & 0xFF), 1, R[0]); break;
        case 1: mem_write(c, c->gbr + ((op & 0xFF) << 1), 2, R[0]); break;
        case 2: mem_write(c, c->gbr + ((op & 0xFF) << 2), 4, R[0]); break;
        case 3: die(c, "trapa", op); break;
        case 4: R[0] = (int32_t)(int8_t)mem_read(c, c->gbr + (op & 0xFF), 1); load = 0; break;
        case 5: R[0] = (int32_t)(int16_t)mem_read(c, c->gbr + ((op & 0xFF) << 1), 2); load = 0; break;
        case 6: R[0] = mem_read(c, c->gbr + ((op & 0xFF) << 2), 4); load = 0; break;
        case 7: R[0] = ((pc + 4) & ~3u) + ((op & 0xFF) << 2); break;
        case 8: reads = 0; SETT((R[0] & (op & 0xFF)) == 0); break;
        case 9: reads = 0; R[0] &= op & 0xFF; break;
        case 10: reads = 0; R[0] ^= op & 0xFF; break;
        case 11: reads = 0; R[0] |= op & 0xFF; break;
        case 12: tmp = mem_read(c, c->gbr + R[0], 1); SETT((tmp & (op & 0xFF)) == 0); c->cyc += 2; break;
        case 13: tmp = mem_read(c, c->gbr + R[0], 1); mem_write(c, c->gbr + R[0], 1, tmp & op); c->cyc += 2; break;
        case 14: tmp = mem_read(c, c->gbr + R[0], 1); mem_write(c, c->gbr + R[0], 1, tmp ^ (op & 0xFF)); c->cyc += 2; break;
        case 15: tmp = mem_read(c, c->gbr + R[0], 1); mem_write(c, c->gbr + R[0], 1, tmp | (op & 0xFF)); c->cyc += 2; break;
        }
        break;
    case 0xD:
        R[n] = mem_read(c, ((pc + 4) & ~3u) + ((op & 0xFF) << 2), 4); load = n;
        break;
    case 0xE:
        R[n] = (int32_t)(int8_t)op;
        break;
    case 0xF:
        goto bad;
    }
    /* load-use stall: previous instruction loaded a register this one reads */
    if (c->load_reg >= 0 && (c->load_reg == reads || c->load_reg == reads2)) c->cyc += 1;
    c->load_reg = load;
    if (!in_delay) c->pc = pc + 2;
    return 0;
done_branch:
    c->load_reg = -1;
    return 1;
bad:
    die(c, "illegal opcode", op);
    return 0;
}

static void take_irq(cpu_t *c, int level, int vec)
{
    R[15] -= 4; mem_write(c, R[15], 4, c->sr);
    R[15] -= 4; mem_write(c, R[15], 4, c->pc);
    c->sr = (c->sr & ~0xF0u) | (level << 4);
    c->pc = mem_read(c, c->vbr + vec * 4, 4);
    c->cyc += 13;
    c->sleeping = 0;
}

static void step(cpu_t *c)
{
    uint32_t pc = c->pc;
    uint64_t c0 = c->cyc;
    if (c->vint_pending && (c->intmask & 8) && ((c->sr >> 4) & 15) < 12) {
        take_irq(c, 12, 70);
        return;
    }
    if ((c->chcr1 & 6) == 6) {                  /* DMA 1 end, interrupt enabled */
        int lvl = (rd_be(&c->onchip[0xE2], 2) >> 8) & 15;
        if (lvl > ((c->sr >> 4) & 15)) {
            take_irq(c, lvl, c->vcr1);
            return;
        }
    }
    uint64_t sr0 = c->stall_sdram, sw0 = c->stall_sdramw, ro0 = c->stall_rom, fb0 = c->stall_fb, dv0 = c->stall_div + c->stall_mul;
    exec(c, pc, 0);
    if (c->r[15] < c->minsp && c->r[15] >= 0xC0000000) c->minsp = c->r[15];
    if ((pc >> 29) == 6 && oram_base)
        pc = oram_base + (pc & 0xFFF);   /* on-chip copy: attribute to the SDRAM original */
    if (romprof[c->id] && (pc & 0x1FFFFFFF) >= 0x02000000 && (pc & 0x1FFFFFFF) < 0x02400000)
        romprof[c->id][(pc & 0x3FFFFF) >> PROF_SHIFT] += c->cyc - c0;
    if (prof[c->id] && (pc & 0x1FFFFFFF) >= 0x06000000 && (pc & 0x1FFFFFFF) < 0x06040000) {
        int k = (pc & 0x3FFFF) >> PROF_SHIFT;
        prof[c->id][k] += c->cyc - c0;
        profc[c->id][1][k] += c->stall_sdram - sr0;
        profc[c->id][2][k] += c->stall_sdramw - sw0;
        profc[c->id][3][k] += c->stall_rom - ro0;
        profc[c->id][4][k] += c->stall_fb - fb0;
        profc[c->id][5][k] += c->stall_div + c->stall_mul - dv0;
    }
}

/* ---------------------------------------------------------------- main */

static const char *sym_for(uint32_t a)
{
    int lo = 0, hi = nsyms - 1, best = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (syms[mid].addr <= a) { best = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return best >= 0 ? syms[best].name : "?";
}

struct padcmd { int from, to; uint16_t b; };
static struct padcmd pads[256];
static int npads;
struct shot { int vbl; const char *file; };
static struct shot shots[64];
static int nshots;

static uint16_t parse_buttons(const char *s)
{
    uint16_t b = 0;
    for (; *s; s++) switch (*s) {
        case 'U': b |= 1; break; case 'D': b |= 2; break; case 'L': b |= 4; break; case 'R': b |= 8; break;
        case 'B': b |= 0x10; break; case 'C': b |= 0x20; break; case 'A': b |= 0x40; break; case 'S': b |= 0x80; break;
    }
    return b;
}

static void write_frame(FILE *f)
{
    static uint8_t buf[320 * 224 * 3];
    const uint8_t *fb = fbmem[fb_visible()];
    uint8_t *o = buf;
    int x, y;
    for (y = 0; y < 224; y++) {
        uint32_t lo = rd_be(&fb[y * 2], 2) * 2;
        for (x = 0; x < 320; x++) {
            uint16_t p = cram[fb[(lo + x) & 0x1FFFF]];
            int r = p & 31, g = (p >> 5) & 31, b = (p >> 10) & 31;
            *o++ = (r << 3) | (r >> 2); *o++ = (g << 3) | (g >> 2); *o++ = (b << 3) | (b >> 2);
        }
    }
    fwrite(buf, 1, sizeof(buf), f);
}

static void save_shot(const char *file)
{
    FILE *f = fopen(file, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 224\n255\n");
    write_frame(f);
    fclose(f);
}

int main(int argc, char **argv)
{
    FILE *f;
    double seconds;
    int i, doprof = 0, hotn = 0, hotcat = 0;
    FILE *video = NULL, *vlog = NULL;
    uint32_t src, dst, size;
    uint64_t end, next_vbl;
    char line[256];

    if (argc < 4) {
        fprintf(stderr, "usage: sh2sim rom syms seconds [-pad from to btns] [-shot vbl file] [-prof]\n");
        return 1;
    }
    f = fopen(argv[1], "rb");
    romsize = fread(rom, 1, sizeof(rom), f);
    fclose(f);
    f = fopen(argv[2], "r");
    while (f && fgets(line, sizeof(line), f) && nsyms < 8192) {
        unsigned a; char t, name[128];
        if (sscanf(line, "%x %c %127s", &a, &t, name) == 3 && (t == 'T' || t == 't')) {
            syms[nsyms].addr = a;
            snprintf(syms[nsyms].name, sizeof(syms[nsyms].name), "%s", name);
            if (!strcmp(name, "_oram_code_start")) oram_base = a;
            nsyms++;
        }
    }
    if (f) fclose(f);
    seconds = atof(argv[3]);
    for (i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "-pad") && i + 3 < argc) {
            pads[npads].from = atoi(argv[i + 1]); pads[npads].to = atoi(argv[i + 2]);
            pads[npads].b = parse_buttons(argv[i + 3]); npads++; i += 3;
        } else if (!strcmp(argv[i], "-shot") && i + 2 < argc) {
            shots[nshots].vbl = atoi(argv[i + 1]); shots[nshots].file = argv[i + 2]; nshots++; i += 2;
        } else if (!strcmp(argv[i], "-prof")) doprof = 1;
        else if (!strcmp(argv[i], "-noirq")) noirq = 1;
        else if (!strcmp(argv[i], "-video") && i + 1 < argc) video = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "-vlog") && i + 1 < argc) vlog = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "-audio") && i + 1 < argc) audio = fopen(argv[++i], "wb");
        else if (!strcmp(argv[i], "-watch") && i + 2 < argc && nwatch < 4) {
            /* -watch symbol|address nlongs: appended to each -vlog line */
            const char *w = argv[++i];
            int k;
            FILE *sf = fopen(argv[2], "r");
            watch_addr[nwatch] = strtoul(w, NULL, 16);
            while (sf && fgets(line, sizeof(line), sf)) {
                unsigned a; char t, name[128];
                if (sscanf(line, "%x %c %127s", &a, &t, name) == 3 && !strcmp(name, w)) watch_addr[nwatch] = a;
            }
            if (sf) fclose(sf);
            (void)k;
            watch_n[nwatch++] = atoi(argv[++i]);
        }
        else if (!strcmp(argv[i], "-hot") && i + 1 < argc) { doprof = 1; hotn = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-hotcat") && i + 1 < argc) { hotcat = atoi(argv[++i]); }
    }

    /* boot: what the 32X boot ROM does with the Mars header */
    src = rd_be(&rom[0x3D4], 4);
    dst = rd_be(&rom[0x3D8], 4);
    size = rd_be(&rom[0x3DC], 4);
    memcpy(&sdram[dst], &rom[src], size);
    for (i = 0; i < 2; i++) {
        cpu_t *c = &cpu[i];
        memset(c, 0, sizeof(*c));
        c->id = i;
        c->pc = rd_be(&rom[0x3E0 + i * 4], 4);
        c->vbr = rd_be(&rom[0x3E8 + i * 4], 4);
        c->r[15] = 0x06040000;
        c->sr = 0xF0;
        c->load_reg = -1;
        c->ccr = 0;
        c->minsp = 0xFFFFFFFF;
        if (doprof) {
            int k;
            prof[i] = calloc(PROF_SIZE, sizeof(uint64_t));
            for (k = 1; k < NCAT; k++) profc[i][k] = calloc(PROF_SIZE, sizeof(uint64_t));
            romprof[i] = calloc(ROMPROF_SIZE, sizeof(uint64_t));
        }
    }
    memset(comm, 0, sizeof(comm));

    end = (uint64_t)(seconds * SH2_HZ);
    next_vbl = (uint64_t)VBLANK_LINE * LINE_CYCLES;
    while (cpu[0].cyc < end || cpu[1].cyc < end) {
        cpu_t *c = cpu[0].cyc <= cpu[1].cyc ? &cpu[0] : &cpu[1];
        uint64_t t = c->cyc;
        while (pwm_next <= t) pwm_tick();
        if (t >= next_vbl) {
            /* vblank: frame swap, IRQ, pad */
            if (fs_pending != 0xFFFF) {
                fbctl_fs = fs_pending; fs_pending = 0xFFFF;
                sum_flipwait += next_vbl - t_flipreq; nflip++;
            }
            for (i = 0; i < nshots; i++)
                if (shots[i].vbl == (int)now_vbl) save_shot(shots[i].file);
            if (video) write_frame(video);
            if (vlog) {
                int w, k;
                fprintf(vlog, "%llu %u %u", (unsigned long long)now_vbl, comm[0], comm[1]);
                for (w = 0; w < nwatch; w++)
                    for (k = 0; k < watch_n[w]; k++)
                        fprintf(vlog, " %d", (int32_t)rd_be(&sdram[(watch_addr[w] & 0x3FFFF) + 4 * k], 4));
                fprintf(vlog, "\n");
            }
            now_vbl++;
            pad_now = 0;
            for (i = 0; i < npads; i++)
                if ((int)now_vbl >= pads[i].from && (int)now_vbl <= pads[i].to) pad_now |= pads[i].b;
            comm[6] = pad_now;
            comm[7]++;
            if (now_vbl % 60 == 0) printf("t=%3llus fps %u.%u\n", (unsigned long long)now_vbl / 60, comm[1] / 10, comm[1] % 10);
            if (!noirq) cpu[0].vint_pending = cpu[1].vint_pending = 1;
            next_vbl += FRAME_CYCLES;
        }
        if (c->sleeping && !(c->vint_pending && (c->intmask & 8))) { c->cyc = next_vbl; continue; }
        step(c);
    }

    if (video) fclose(video);
    if (vlog) fclose(vlog);
    if (audio) {
        fclose(audio);
        printf("audio: %llu stereo frames at %.1f Hz (PWM cycle %u), DMA words %llu/%llu\n",
               (unsigned long long)audio_frames, pwm_cycle > 1 ? (double)SH2_HZ / (pwm_cycle - 1) : 0.0, pwm_cycle,
               (unsigned long long)cpu[0].dma_words, (unsigned long long)cpu[1].dma_words);
    }
    printf("simulated %.2f s, %llu vblanks, fps10 (COMM2) = %u\n", seconds, (unsigned long long)now_vbl, comm[1]);
    if (nframes_r)
        printf("frames %llu: avg render master %.2f ms, slave %.2f ms; avg frame %.2f ms\n", (unsigned long long)nframes_r,
               sum_master * 1000.0 / SH2_HZ / nframes_r, sum_slave * 1000.0 / SH2_HZ / nframes_r,
               seconds * 1000.0 / nframes_r);
    if (nframes_r > 1 && nflip)
        printf("serial (slave done -> next go) %.2f ms: hud etc %.2f ms, flip request -> go %.2f ms; flip latency %.2f ms\n",
               sum_serial * 1000.0 / SH2_HZ / (nframes_r - 1), sum_hud * 1000.0 / SH2_HZ / (nframes_r - 1),
               sum_upd * 1000.0 / SH2_HZ / (nframes_r - 1), sum_flipwait * 1000.0 / SH2_HZ / nflip);
    for (i = 0; i < 2; i++) {
        cpu_t *c = &cpu[i];
        printf("cpu%d: %llu instr, stalls: sdram-w %llu sdram-r %llu rom %llu fb %llu regs %llu div %llu mul %llu; misses %llu (ifetch %llu)\n",
               i, (unsigned long long)c->instrs, (unsigned long long)c->stall_sdramw, (unsigned long long)c->stall_sdram, (unsigned long long)c->stall_rom,
               (unsigned long long)c->stall_fb, (unsigned long long)c->stall_reg, (unsigned long long)c->stall_div,
               (unsigned long long)c->stall_mul, (unsigned long long)c->misses, (unsigned long long)c->ifetch_miss);
        printf("cpu%d: lowest stack pointer %08x\n", i, c->minsp);
    }
    if (doprof) {
        for (i = 0; i < 2; i++) {
            static uint64_t per[8192][NCAT];
            uint64_t tot = 0;
            int k, order[8192], no = 0, cat;
            memset(per, 0, sizeof(per));
            for (k = 0; k < PROF_SIZE; k++) {
                uint32_t a = 0x06000000 + (k << PROF_SHIFT);
                int lo = 0, hi = nsyms - 1, best = -1;
                if (!prof[i][k]) continue;
                while (lo <= hi) { int mid = (lo + hi) / 2; if (syms[mid].addr <= a) { best = mid; lo = mid + 1; } else hi = mid - 1; }
                if (best >= 0) {
                    per[best][0] += prof[i][k];
                    for (cat = 1; cat < NCAT; cat++) per[best][cat] += profc[i][cat][k];
                }
                tot += prof[i][k];
            }
            for (k = 0; k < nsyms; k++) if (per[k][0]) order[no++] = k;
            for (k = 0; k < no; k++) {
                int j;
                for (j = k + 1; j < no; j++) if (per[order[j]][0] > per[order[k]][0]) { int t2 = order[k]; order[k] = order[j]; order[j] = t2; }
            }
            printf("cpu%d profile (%llu cycles)   %%total  sdram-r sdram-w   rom    fb  div/mul (%% of function)\n", i, (unsigned long long)tot);
            for (k = 0; k < no && k < 32; k++) {
                uint64_t *pp = per[order[k]];
                printf("  %-22s %5.1f%%   %5.1f   %5.1f  %5.1f %5.1f  %5.1f\n", syms[order[k]].name, 100.0 * pp[0] / tot,
                       100.0 * pp[1] / pp[0], 100.0 * pp[2] / pp[0], 100.0 * pp[3] / pp[0], 100.0 * pp[4] / pp[0], 100.0 * pp[5] / pp[0]);
            }
        }
    }
    if (doprof) {
        for (i = 0; i < 2; i++) {
            static uint64_t per[8192];
            uint64_t tot = 0;
            int k, order[8192], no = 0;
            memset(per, 0, sizeof(per));
            for (k = 0; k < ROMPROF_SIZE; k++) {
                uint32_t a = 0x02000000 + (k << PROF_SHIFT);
                int lo = 0, hi = nsyms - 1, best = -1;
                if (!romprof[i][k]) continue;
                while (lo <= hi) { int mid = (lo + hi) / 2; if (syms[mid].addr <= a) { best = mid; lo = mid + 1; } else hi = mid - 1; }
                if (best >= 0) per[best] += romprof[i][k];
                tot += romprof[i][k];
            }
            for (k = 0; k < nsyms; k++) if (per[k]) order[no++] = k;
            for (k = 0; k < no; k++) {
                int j;
                for (j = k + 1; j < no; j++) if (per[order[j]] > per[order[k]]) { int t2 = order[k]; order[k] = order[j]; order[j] = t2; }
            }
            if (tot) printf("cpu%d ROM code profile (%llu cycles, %.1f%% of the run)\n", i, (unsigned long long)tot, 100.0 * tot / (seconds * SH2_HZ));
            for (k = 0; k < no && k < 16; k++)
                printf("  %-22s %5.1f%%\n", syms[order[k]].name, 100.0 * per[order[k]] / tot);
        }
    }
    if (hotn) {
        /* hottest individual instructions on cpu1 */
        static int idx[PROF_SIZE];
        int k, no = 0;
        uint64_t tot = 0;
        uint64_t *pa = hotcat ? profc[1][hotcat] : prof[1];
        for (k = 0; k < PROF_SIZE; k++) if (pa[k]) { idx[no++] = k; }
        for (k = 0; k < PROF_SIZE; k++) tot += prof[1][k];
        for (k = 0; k < hotn && k < no; k++) {
            int j, best = k;
            for (j = k + 1; j < no; j++) if (pa[idx[j]] > pa[idx[best]]) best = j;
            { int t2 = idx[k]; idx[k] = idx[best]; idx[best] = t2; }
            printf("  %08x %5.2f%% %s\n", 0x06000000 + (idx[k] << PROF_SHIFT), 100.0 * pa[idx[k]] / tot,
                   sym_for(0x06000000 + (idx[k] << PROF_SHIFT)));
        }
    }
    return 0;
}

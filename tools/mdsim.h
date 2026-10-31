/* mdsim.h - the Mega Drive side for sh2sim: a 68000 (Musashi) running the
 * ROM's 68000 program, its work RAM, the joypad port, the 32X registers it
 * shares with the SH-2s (COMM), and enough of the VDP for this game: VRAM,
 * CRAM and VSRAM writes through the ports, planes A and B with full-screen
 * scroll, sprites, priorities, the vertical blank interrupt. The picture is
 * mixed with the 32X one the way the 32X does it: the 32X shows where the
 * Mega Drive shows its backdrop, and elsewhere when the priority bit or the
 * colour's through bit says so.
 *
 * Included by sh2sim.c when built with -DMD68K (tools/build_sim.sh). It
 * uses sh2sim's rom[], romsize and comm[]. No DMA, no window plane, no
 * interlace, no per-line sprite limits, no bus contention with the SH-2s.
 */
#include "m68k.h"

#define MD_CLOCK_DIV 3              /* 68000 at 7.67 MHz: one clock per three SH-2 clocks */

static uint8_t md_ram[64 << 10];
static uint8_t vram[64 << 10];
static uint16_t md_cram[64], vsram[40];
static uint8_t vreg[32];
static int vdp_pending, vdp_code, vdp_vint, vdp_invbl;
static uint32_t vdp_addr;
static int md_bank;
static uint16_t md_adapter = 0x0083;
static int pad_th = 1, pad_count;
static uint16_t md_pad;             /* SH-2 button bits, as sh2sim's -pad */
static uint64_t md_clk;             /* 68000 clocks run */

/* -xe1ap: port 1 holds an XE-1 AP analogue pad (with Polyshroom's extension
 * unless -xe1ap-noext), as described in Polyshroom's ANALOGUE-PAD document:
 * a falling edge of TH latches a report, sent 50 us later one nibble per
 * 20 us (10 us /ACK high with the new L/H, then 10 us /ACK low with the
 * nibble), 12 nibbles, or 20 when TH is still low after the twelfth. The
 * d-pad letters of -pad push the left stick all the way. */
static int xe_on, xe_ext = 1;
static uint8_t xe_axes[7] = { 128, 128, 128, 128, 128, 0, 0 };   /* X Y RZ Z RY LT RT */
static uint8_t xe_nib[20];
static int xe_active, xe_short;
static uint64_t xe_t0;

static uint64_t md_now(void) { return md_clk + m68k_cycles_run(); }

static void xe_latch(void)
{
    uint16_t p = md_pad;
    int x = xe_axes[0], y = xe_axes[1], z = xe_axes[3], rz = xe_axes[2], ry = xe_axes[4];
    int lt = xe_axes[5], rt = xe_axes[6];
    if (p & 4) x = 0;
    if (p & 8) x = 255;
    if (p & 1) y = 0;
    if (p & 2) y = 255;
    if (!xe_ext)
        z = 128 + (rt - lt) / 2;
    /* buttons 0 = pressed: E1 E2 START SELECT / A B C D */
    xe_nib[0] = (p & 0x200 ? 0 : 8) | (p & 0x100 ? 0 : 4) | (p & 0x80 ? 0 : 2) | (p & 0x800 ? 0 : 1);
    xe_nib[1] = (p & 0x40 ? 0 : 8) | (p & 0x10 ? 0 : 4) | (p & 0x20 ? 0 : 2) | (p & 0x400 ? 0 : 1);
    xe_nib[2] = x >> 4;  xe_nib[6] = x & 15;
    xe_nib[3] = y >> 4;  xe_nib[7] = y & 15;
    xe_nib[4] = rz >> 4; xe_nib[8] = rz & 15;
    xe_nib[5] = z >> 4;  xe_nib[9] = z & 15;
    xe_nib[10] = 15;
    xe_nib[11] = (p & 0x40 ? 0 : 8) | (p & 0x10 ? 0 : 4) | 3;
    xe_nib[12] = 3; xe_nib[13] = 2;
    xe_nib[14] = ry >> 4; xe_nib[15] = ry & 15;
    xe_nib[16] = lt >> 4; xe_nib[17] = lt & 15;
    xe_nib[18] = rt >> 4; xe_nib[19] = rt & 15;
    xe_t0 = md_now();
    xe_active = 1;
    xe_short = !xe_ext;
}

/* the lines now: D0-D3, TL = L/H, TR = /ACK */
static uint8_t xe_read(void)
{
    uint64_t us10 = (md_now() - xe_t0) * 1000 / 767;  /* tenths of a microsecond */
    int k, n = xe_short ? 12 : 20, lo;
    if (!xe_active || us10 < 500)
        return 0x2F;
    k = (int)((us10 - 500) / 200);
    lo = (us10 - 500) % 200 >= 100;
    if (k >= n) {
        xe_active = 0;
        return 0x2F;
    }
    if (!lo)
        return (k ? xe_nib[k - 1] : 15) | ((k & 1) << 4) | 0x20;
    return xe_nib[k] | ((k & 1) << 4);
}
static int md_on;
#ifdef PAD_ADAPTER
#include "adapter_sim.h"     /* the pad adapter's tests (its own project): its firmware and PIO */
#endif
static uint8_t md_ctx[16384] __attribute__((aligned(16)));

static uint32_t md_vector(uint32_t a)
{
    int i = a >> 2;
    if (i == 0) return 0x00FFFFE0;
    return 0x880200 + (i - 1) * 6;  /* the 32X's vector ROM: the cartridge's jump table */
}

static uint8_t io_read(uint32_t a)
{
    if (a == 0xA10001) return 0xA0;
#ifdef PAD_ADAPTER
    if (a == 0xA10003 && adp_mode)
        return adp_read() | (pad_th << 6);
#endif
    if (a == 0xA10003 && xe_on)
        return xe_read() | (pad_th << 6);
    if (a == 0xA10003) {
        uint16_t p = md_pad;
        int v;
        if (pad_th) {
            if (pad_count == 3)
                v = ((p >> 8) & 1 ? 0 : 1) | ((p >> 9) & 1 ? 0 : 2) | ((p >> 10) & 1 ? 0 : 4) | ((p >> 11) & 1 ? 0 : 8);
            else
                v = ~p & 0x0F;
            v |= (p & 0x10 ? 0 : 0x10) | (p & 0x20 ? 0 : 0x20) | 0x40;
        } else {
            if (pad_count == 3) v = 0;
            else if (pad_count == 4) v = 0x0F;
            else v = ~p & 3;
            v |= (p & 0x40 ? 0 : 0x10) | (p & 0x80 ? 0 : 0x20);
        }
        return v;
    }
    if (a == 0xA10005) return 0x7F;
    return 0;
}

static void io_write(uint32_t a, uint8_t v)
{
    if (a == 0xA10003) {
        int th = (v >> 6) & 1;
#ifdef PAD_ADAPTER
        if (adp_mode)
            adp_th(th);
#endif
        if (pad_th && !th) {
            pad_count++;
            if (xe_on)
                xe_latch();
        }
        /* REQ raised before the twelfth nibble: no extension this time */
        if (xe_on && !pad_th && th && xe_active && md_now() - xe_t0 < (uint64_t)(50 + 12 * 20) * 767 / 100)
            xe_short = 1;
        pad_th = th;
    }
}

static uint16_t sys_read16(uint32_t a)
{
    uint32_t off = a & 0x3F;
    if (off >= 0x20 && off < 0x30) return comm[(off - 0x20) >> 1];
    if (off == 0x00) return md_adapter;
    if (off == 0x04) return md_bank;
    return 0;
}

static void sys_write16(uint32_t a, uint16_t v)
{
    uint32_t off = a & 0x3E;
    if (off >= 0x20 && off < 0x30) { comm[(off - 0x20) >> 1] = v; return; }
    if (off == 0x00) { md_adapter = (v & 0x8000) | (md_adapter & 0x7FFF); return; }
    if (off == 0x04) { md_bank = v & 3; return; }
}

static void sys_write8(uint32_t a, uint8_t v)
{
    uint32_t off = a & 0x3F;
    if (off >= 0x20 && off < 0x30) {
        uint16_t *c = &comm[(off - 0x20) >> 1];
        *c = (off & 1) ? (*c & 0xFF00) | v : (*c & 0x00FF) | (v << 8);
        return;
    }
    if (off == 0x05) { md_bank = v & 3; return; }
    if (off == 0x00 || off == 0x01) {
        uint16_t w = off ? (md_adapter & 0xFF00) | v : (md_adapter & 0x00FF) | (v << 8);
        sys_write16(a & ~1, w);
    }
}

static void vdp_ctrl(uint16_t v)
{
    if (vdp_pending) {
        vdp_addr = (vdp_addr & 0x3FFF) | ((v & 3) << 14);
        vdp_code = (vdp_code & 3) | ((v >> 2) & 0x3C);
        vdp_pending = 0;
    } else if ((v & 0xC000) == 0x8000) {
        int r = (v >> 8) & 0x1F;
        if (r < 24) vreg[r] = v & 0xFF;
    } else {
        vdp_addr = (vdp_addr & 0xC000) | (v & 0x3FFF);
        vdp_code = (vdp_code & 0x3C) | (v >> 14);
        vdp_pending = 1;
    }
}

static void vdp_data(uint16_t v)
{
    vdp_pending = 0;
    switch (vdp_code & 0xF) {
    case 1: vram[vdp_addr & 0xFFFE] = v >> 8; vram[(vdp_addr & 0xFFFE) + 1] = v; break;
    case 3: md_cram[(vdp_addr >> 1) & 63] = v & 0xEEE; break;
    case 5: if (((vdp_addr >> 1) & 63) < 40) vsram[(vdp_addr >> 1) & 63] = v & 0x7FF; break;
    }
    vdp_addr = (vdp_addr + vreg[15]) & 0xFFFF;
}

static uint16_t vdp_status(void)
{
    vdp_pending = 0;
    return 0x3600 | (vdp_invbl ? 8 : 0) | (vdp_vint ? 0x80 : 0) | 0x200;
}

static uint16_t md_read16(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x100) return (a & 2) ? md_vector(a) & 0xFFFF : md_vector(a) >> 16;
    if (a >= 0x880000 && a < 0x900000) {
        uint32_t o = a - 0x880000;
        return o + 1 < romsize ? (rom[o] << 8) | rom[o + 1] : 0xFFFF;
    }
    if (a >= 0x900000 && a < 0xA00000) {
        uint32_t o = ((uint32_t)md_bank << 20) + (a - 0x900000);
        return o + 1 < romsize ? (rom[o] << 8) | rom[o + 1] : 0xFFFF;
    }
    if (a >= 0xE00000) return (md_ram[a & 0xFFFF] << 8) | md_ram[(a + 1) & 0xFFFF];
    if (a >= 0xA15100 && a < 0xA15140) return sys_read16(a);
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1C) == 4) return vdp_status();
        return 0;
    }
    if (a >= 0xA10000 && a < 0xA10020) return (io_read(a | 1) << 8) | io_read(a | 1);
    return 0;
}

static uint8_t md_read8(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a >= 0xA10000 && a < 0xA10020) return io_read(a);
    if (a >= 0xE00000) return md_ram[a & 0xFFFF];
    {
        uint16_t w = md_read16(a & ~1);
        return (a & 1) ? w & 0xFF : w >> 8;
    }
}

static void md_write16(uint32_t a, uint16_t v)
{
    a &= 0xFFFFFF;
    if (a >= 0xE00000) { md_ram[a & 0xFFFF] = v >> 8; md_ram[(a + 1) & 0xFFFF] = v; return; }
    if (a >= 0xC00000 && a < 0xC00020) {
        if ((a & 0x1C) == 0) vdp_data(v);
        else if ((a & 0x1C) == 4) vdp_ctrl(v);
        return;
    }
    if (a >= 0xA15100 && a < 0xA15140) { sys_write16(a, v); return; }
    if (a >= 0xA10000 && a < 0xA10020) { io_write(a | 1, v); return; }
}

static void md_write8(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    if (a >= 0xE00000) { md_ram[a & 0xFFFF] = v; return; }
    if (a >= 0xA130F3 && a <= 0xA130FF && (a & 1)) {     /* Sega's mapper */
        ssf2_bank[(a & 0xF) >> 1] = v & 0x3F;
        return;
    }
    if (a >= 0xA10000 && a < 0xA10020) { io_write(a, v); return; }
    if (a >= 0xA15100 && a < 0xA15140) { sys_write8(a, v); return; }
    if (a >= 0xC00000 && a < 0xC00020) { md_write16(a & ~1, (v << 8) | v); return; }
}

unsigned int m68k_read_memory_8(unsigned int a) { return md_read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return md_read16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return ((uint32_t)md_read16(a) << 16) | md_read16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { md_write8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { md_write16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { md_write16(a, v >> 16); md_write16(a + 2, v); }

static int md_int_ack(int level)
{
    (void)level;
    vdp_vint = 0;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

static int md_tas(void) { return 1; }

static void md_init(void)
{
    md_on = 1;
    m68k_set_context(md_ctx);
    m68k_init();
    m68k_set_int_ack_callback(md_int_ack);
    m68k_set_tas_instr_callback(md_tas);
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    /* what the 32X boot ROM leaves for the program */
    comm[0] = 0x4D5F; comm[1] = 0x4F4B;       /* "M_OK" */
    comm[2] = 0x535F; comm[3] = 0x4F4B;       /* "S_OK" */
}

/* run the 68000 up to SH-2 clock t */
static void md_run(uint64_t t)
{
    uint64_t target = t / MD_CLOCK_DIV;
    while (md_clk < target)
        md_clk += m68k_execute(64);
}

static void md_vblank_start(uint16_t pad)
{
    md_pad = pad;
    pad_count = 0;
#ifdef PAD_ADAPTER
    if (adp_mode)
        adp_set(pad, xe_axes);
#endif
    vdp_invbl = 1;
    if (vreg[1] & 0x20) {
        vdp_vint = 1;
        m68k_set_irq(6);
    }
}

static void md_vblank_end(void) { vdp_invbl = 0; }

static inline int vram_pixel(int tile, int row, int col)
{
    uint8_t b = vram[((tile * 32) + row * 4 + (col >> 1)) & 0xFFFF];
    return (col & 1) ? b & 15 : b >> 4;
}

/* one line of the Mega Drive picture: colour index + 1 (0 = nothing),
 * bit 7 set for high priority */
static void md_plane_line(int base, int hs, int vs, int hscroll, int vscroll, int y, uint8_t *out)
{
    int w = hs * 8, h = vs * 8, x;
    int py = (y + vscroll) & (h - 1);
    for (x = 0; x < 320; x++) {
        int px = (x - hscroll) & (w - 1);
        int a = (base + ((py >> 3) * hs + (px >> 3)) * 2) & 0xFFFF;
        uint16_t cell = (vram[a] << 8) | vram[a + 1];
        int row = py & 7, col = px & 7, p;
        if (cell & 0x1000) row = 7 - row;
        if (cell & 0x0800) col = 7 - col;
        p = vram_pixel(cell & 0x7FF, row, col);
        out[x] = p ? (uint8_t)((((cell >> 13) & 3) * 16 + p + 1) | ((cell >> 8) & 0x80)) : 0;
    }
}

static void md_sprite_line(int y, uint8_t *out)
{
    int sat = (vreg[5] & 0x7E) << 9, link = 0, n = 0;
    memset(out, 0, 320);
    do {
        const uint8_t *s = &vram[(sat + link * 8) & 0xFFFF];
        int sy = (((s[0] << 8) | s[1]) & 0x3FF) - 128;
        int size = s[2], hsz = ((size >> 2) & 3) + 1, vsz = (size & 3) + 1;
        uint16_t tw = (s[4] << 8) | s[5];
        int sx = (((s[6] << 8) | s[7]) & 0x1FF) - 128, x;
        link = s[3] & 0x7F;
        if (y >= sy && y < sy + vsz * 8) {
            int ry = y - sy;
            if (tw & 0x1000) ry = vsz * 8 - 1 - ry;
            for (x = 0; x < hsz * 8; x++) {
                int X = sx + x, rx = (tw & 0x0800) ? hsz * 8 - 1 - x : x, p;
                if (X < 0 || X >= 320 || out[X])
                    continue;
                p = vram_pixel((tw & 0x7FF) + (rx >> 3) * vsz + (ry >> 3), ry & 7, rx & 7);
                if (p)
                    out[X] = (uint8_t)((((tw >> 13) & 3) * 16 + p + 1) | ((tw >> 8) & 0x80));
            }
        }
    } while (link && ++n < 80);
}

/* mix the Mega Drive picture into a 320x224 RGB24 frame of the 32X one;
 * p32x: the 32X colour of each pixel (CRAM words, through bit 15) */
static void md_mix(uint8_t *rgb, const uint16_t *p32x, int pri)
{
    static uint8_t la[320], lb[320], ls[320];
    static const int sz[4] = { 32, 64, 0, 128 };
    int hs = sz[vreg[16] & 3], vs = sz[(vreg[16] >> 4) & 3];
    int abase = (vreg[2] & 0x38) << 10, bbase = (vreg[4] & 7) << 13, hsb = (vreg[13] & 0x3F) << 10;
    int bg = vreg[7] & 0x3F, y, x;
    if (!(vreg[1] & 0x40) || !hs || !vs)
        return;
    for (y = 0; y < 224; y++) {
        int ha = (vram[hsb] << 8) | vram[hsb + 1], hb = (vram[hsb + 2] << 8) | vram[hsb + 3];
        md_plane_line(abase, hs, vs, ha & 0x3FF, vsram[0] & 0x3FF, y, la);
        md_plane_line(bbase, hs, vs, hb & 0x3FF, vsram[1] & 0x3FF, y, lb);
        md_sprite_line(y, ls);
        for (x = 0; x < 320; x++) {
            int c;
            uint16_t p = p32x[y * 320 + x];
            uint8_t *d = rgb + (y * 320 + x) * 3;
            if (ls[x] & 0x80) c = ls[x];
            else if (la[x] & 0x80) c = la[x];
            else if (lb[x] & 0x80) c = lb[x];
            else if (ls[x]) c = ls[x];
            else if (la[x]) c = la[x];
            else c = lb[x];
            c = (c & 0x7F) - 1;
            if (c < 0 || c == bg)
                continue;                       /* backdrop: the 32X shows */
            if (((p ^ (pri ? 0x8000 : 0)) & 0x8000))
                continue;                       /* the 32X pixel wins */
            {
                uint16_t m = md_cram[c];
                d[0] = ((m >> 1) & 7) * 255 / 7;
                d[1] = ((m >> 5) & 7) * 255 / 7;
                d[2] = ((m >> 9) & 7) * 255 / 7;
            }
        }
    }
}

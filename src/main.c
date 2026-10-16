/* Quake 32X - main loops for the two SH-2s */
#include "game.h"

extern const uint8_t level_data[];
extern uint8_t _bss_end[];  /* linker symbol __bss_end */
volatile uint32_t vblank_count;
volatile uint16_t pad_latch;     /* OR of every pad sample, set by the VBlank IRQ */
volatile uint16_t *r_fb;

#define SLAVE_GO     MARS_SYS_COMM4
#define SLAVE_DONE   MARS_SYS_COMM6
#define MASTER_DONE  MARS_SYS_COMM8
#define PAD          MARS_SYS_COMM12

#define FREE_TOP     0x06040000      /* stacks are in on-chip RAM */

static rcpu_t rcpu[2];
static surfcache_t *schash[2][HASHSIZE];
static volatile int32_t slave_spin;

void *memcpy(void *dst, const void *src, unsigned n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if ((((uint32_t)d | (uint32_t)s) & 3) == 0) {
        while (n >= 4) {
            *(uint32_t *)d = *(const uint32_t *)s;
            d += 4; s += 4; n -= 4;
        }
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int c, unsigned n)
{
    uint8_t *d = dst;
    if (((uint32_t)d & 3) == 0) {
        uint32_t v = (uint8_t)c * 0x01010101u;
        while (n >= 4) { *(uint32_t *)d = v; d += 4; n -= 4; }
    }
    while (n--) *d++ = c;
    return dst;
}

static void init_video(void)
{
    int i, k;
    while ((MARS_VDP_FBCTL & MARS_VDP_PEN) == 0) ;
    for (i = 0; i < 256; i++)
        MARS_CRAM[i] = lvl.palette[i];
    MARS_VDP_DISPMODE = MARS_VDP_MODE_PACKED;
    for (k = 0; k < 2; k++) {
        volatile uint16_t *fb = MARS_FRAMEBUFFER;
        uint16_t fs = MARS_VDP_FBCTL & MARS_VDP_FS;
        for (i = 0; i < 256; i++)
            fb[i] = 0x100 + ((i < 224 ? i : 223) >> 1) * SCREENW;
        for (i = 0x100; i < 0x100 + SCREENW * SCREENH; i++)
            fb[i] = 0;
        MARS_VDP_FBCTL = fs ^ 1;
        while ((MARS_VDP_FBCTL & MARS_VDP_FS) == fs) ;
    }
}

static void apply_settings(void)
{
    if (view.lighting != cfg.lighting)
        view.cachegen++;
    view.textures = cfg.textures;
    view.lighting = cfg.lighting;
    view.far = far_levels[cfg.far] << 16;
}

/* input, game ticks, view and PVS for the next frame */
static void update_frame(uint32_t *last_tick, int *viewleaf, uint16_t *oldpad)
{
    uint32_t now = get_vblank_count();
    int ticks = now - *last_tick;
    volatile uint16_t *latch = UNCACHED(&pad_latch);
    uint16_t pad = PAD, seen, pressed;
    fixed_t eye[3];
    int leaf;

    seen = *latch;
    *latch = 0;
    if (pad & BTN_NOPAD) pad = 0;
    if (seen & BTN_NOPAD) seen = 0;
    pressed = (seen | pad) & ~*oldpad;
    *oldpad = pad;
    *last_tick = now;
    if (ticks > 6) ticks = 6;

    if (pressed & BTN_START) {
        menu_active ^= 1;
        pressed &= ~BTN_START;
    }
    if (menu_active)
        menu_input(pressed);
    else
        while (ticks-- > 0)
            game_tick(pad);
    apply_settings();

    game_eye(eye);
    view.time = now;
    r_setup_view(eye, player.yaw, player.pitch);
    leaf = r_pointleaf(eye);
    if (leaf != *viewleaf) {
        r_markleaves(leaf);
        *viewleaf = leaf;
    }
}

void pri_main(void)
{
    uint32_t last_tick, fps_tick;
    int frames = 0, fps10 = 0, viewleaf = -1;
    uint16_t oldpad = 0, tag = 0;
    int split = SCREENH / 2;

    level_init(level_data);
    r_init();
    r_span_init();
    r_oram_init();
    r_fb = (volatile uint16_t *)0x24000200;
    init_video();

    {
        uint32_t base = ((uint32_t)_bss_end + 15) & ~15;
        int each = ((FREE_TOP - base) / 2) & ~15;
        rcpu[0].cpu = 0;
        rcpu[1].cpu = 1;
        sc_init(&rcpu[0], (uint8_t *)base, each, schash[0]);
        sc_init(&rcpu[1], (uint8_t *)(base + each), each, schash[1]);
    }

    game_init();
    view.cachegen = 1;
    view.mipbias = 0;
    apply_settings();
    last_tick = fps_tick = get_vblank_count();
    view.flip_fs = MARS_VDP_FBCTL & MARS_VDP_FS;
    update_frame(&last_tick, &viewleaf, &oldpad);

    for (;;) {
        int32_t mspin = 0, sspin;
        uint16_t fs;
        uint32_t now;

        /* render: master does rows [0,split), slave [split,SCREENH). Both
           start with geometry and wait for the previous flip only when they
           first write to the framebuffer. */
        view.split = split;
        if (++tag == 0) tag = 1;
        SLAVE_GO = tag;
        r_render(&rcpu[0], 0, split);
        while (SLAVE_DONE != tag)
            mspin++;
        MARS_SYS_COMM8 = tag;
        while (MARS_SYS_COMM10 != tag) ;        /* slave publishes its wait */
        sspin = *(volatile int32_t *)UNCACHED(&slave_spin);

        /* balance the two halves using who waited for whom: one spin is a
           few cycles, a row costs roughly 10-20k cycles */
        {
            int32_t d = mspin - sspin, step;
            step = d / 4096;
            if (step == 0 && (d > 64 || d < -64)) step = d > 0 ? 1 : -1;
            if (step > 3) step = 3;
            if (step < -3) step = -3;
            split += step;
            if (split < 16) split = 16;
            if (split > SCREENH - 16) split = SCREENH - 16;
        }

        if (menu_active)
            menu_draw();
        if (cfg.showfps)
            hud_draw(fps10, split);

        now = get_vblank_count();
        frames++;
        if (now - fps_tick >= 60) {
            fps10 = hw_div(frames * 600, now - fps_tick);
            frames = 0;
            fps_tick = now;
        }
        MARS_SYS_COMM2 = fps10;

        /* request the flip; it happens at the next vertical blank while the
           game logic and the next frame's geometry run */
        fs = MARS_VDP_FBCTL & MARS_VDP_FS;
        MARS_VDP_FBCTL = fs ^ 1;
        view.flip_fs = fs ^ 1;
        update_frame(&last_tick, &viewleaf, &oldpad);
    }
}

void sec_main(void)
{
    uint16_t last = 0;
    r_oram_init();
#ifdef PROFILE
    *(volatile uint8_t *)0xFFFFFE10 = 0;    /* TIER: no FRT interrupts */
    *(volatile uint8_t *)0xFFFFFE11 = 0;    /* FTCSR: free running */
    *(volatile uint8_t *)0xFFFFFE16 = 0;    /* TCR: clock / 8 */
#endif
    for (;;) {
        uint16_t tag;
        int32_t spin = 0;
        while ((tag = SLAVE_GO) == last) ;
        last = tag;
        SH2_CCR = 0x19;     /* purge (keep two-way mode): see the master's fresh view/PVS data */
        r_render(&rcpu[1], view.split, SCREENH);
        SLAVE_DONE = tag;
        while (MASTER_DONE != tag)
            spin++;
        *(volatile int32_t *)UNCACHED(&slave_spin) = spin;
        MARS_SYS_COMM10 = tag;
    }
}

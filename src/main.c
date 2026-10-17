/* Quake 32X - main loops for the two SH-2s */
#include "game.h"

extern const uint8_t level_data[];
extern uint8_t _bss_end[];  /* linker symbol __bss_end */
volatile uint32_t vblank_count;
volatile uint16_t pad_latch;     /* OR of every pad sample, set by the VBlank IRQ */
volatile uint16_t *r_fb;
volatile uint16_t *r_screen = (volatile uint16_t *)0x24000200;
static int border_dirty;            /* framebuffers whose border needs redrawing */
static uint8_t *arena_base;         /* per-CPU memory: liquid buffers + surface cache */
static int arena_each, arena_mesh = -1;
static int win_x, win_y;            /* view window origin (rendered pixels) */

#define SLAVE_GO     MARS_SYS_COMM4
#define SLAVE_DONE   MARS_SYS_COMM6
#define MASTER_DONE  MARS_SYS_COMM8
#define PAD          MARS_SYS_COMM12

#ifdef ONCHIP
#define FREE_TOP     0x06040000      /* stacks are in on-chip RAM */
#define CCR_PURGE    0x19            /* purge, keep two-way mode */
#else
#define FREE_TOP     0x0603E000      /* two 4 KB stacks above this */
#define CCR_PURGE    0x11            /* purge, 4-way cache */
#endif

static rcpu_t rcpu[2];
static surfcache_t *schash[2][HASHSIZE];
static volatile int32_t slave_spin;
static uint16_t pad_poll;           /* pad samples taken by the master while rendering */
#ifdef DEMO
static int hint_time = 0;
#else
static int hint_time = 8 * 60;      /* show the controls for the first 8 seconds */
#endif

/* 60 Hz time from whichever vblank counter is running: the 68000's (COMM14)
   or the master SH-2's interrupt. Either one alone keeps the game going. */
static uint32_t time_now(void)
{
    static uint16_t last68;
    static uint32_t lastirq, t;
    uint16_t c = MARS_SYS_COMM14;
    uint32_t v = get_vblank_count();
    uint32_t d68 = (uint16_t)(c - last68), dirq = v - lastirq;
    last68 = c;
    lastirq = v;
    t += d68 > dirq ? d68 : dirq;
    return t;
}

static void poll_pad(void)
{
    pad_poll |= PAD;
}

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
    const viewsize_t *vs = &view_sizes[cfg.size];
    if (view.lighting != cfg.lighting)
        view.cachegen++;
    view.textures = cfg.textures;
    view.lighting = cfg.lighting;
    view.far = far_levels[cfg.far] << 16;
    view.mesh = cfg.mesh;
    if (arena_mesh != cfg.mesh) {
        /* both CPUs are idle here; the slave purges its cache before the next frame */
        arena_mesh = cfg.mesh;
        r_arena(&rcpu[0], arena_base, arena_each, schash[0], cfg.mesh);
        r_arena(&rcpu[1], arena_base + arena_each, arena_each, schash[1], cfg.mesh);
    }
    if (view.vw != vs->w || view.vh != vs->h) {
        view.vw = vs->w;
        view.vh = vs->h;
        win_x = (SCREENW - vs->w) >> 1;
        win_y = (SCREENH - vs->h) >> 1;
        r_fb = r_screen + win_y * SCREENW + win_x;
        border_dirty = 2;
    }
}

/* input, game ticks, view and PVS for the next frame */
static uint32_t frame_now;

#ifdef DEMO
#include "demo.h"
/* demo playback: the camera follows a planned route, and pause-menu macros
   fire at given camera ticks (the camera holds still while a macro runs) */
static int demo_tick, demo_macro, demo_step;
static uint32_t demo_t0;

static uint16_t demo_update(uint32_t now, int ticks, uint16_t pad_pressed)
{
#ifdef DEMO_BENCH
    /* benchmark: the route without the menu macros; the pad works the menu */
    static const demo_macro_t none = { -1, 0 };
    const demo_macro_t *m = &none;
    uint16_t pressed = pad_pressed;
#else
    const demo_macro_t *m = &demo_macros[demo_macro];
    uint16_t pressed = 0;
    (void)pad_pressed;
#endif
    if (m->tick >= 0 && demo_tick >= m->tick) {
        if (!demo_t0) demo_t0 = now ? now : 1;
        while (m->steps[demo_step].dt >= 0 && (int)(now - demo_t0) >= m->steps[demo_step].dt)
            pressed |= m->steps[demo_step++].buttons;
        if (m->steps[demo_step].dt < 0) {
            demo_macro++;
            demo_step = 0;
            demo_t0 = 0;
        }
    } else if (!menu_active) {
        demo_tick += ticks;
        if (m->tick >= 0 && demo_tick > m->tick) demo_tick = m->tick;
        if (demo_tick >= demo_ncam) demo_tick = demo_ncam - 1;
    }
    {
        const demo_cam_t *c = &demo_cam[demo_tick];
        const demo_phase_t *ph = demo_phases;
        player.pos[0] = c->x;
        player.pos[1] = c->y;
        player.pos[2] = c->z;
        player.yaw = c->yaw;
        player.pitch = c->pitch;
        while (ph[1].tick >= 0 && ph[1].tick <= demo_tick) ph++;
        MARS_SYS_COMM0 = ph->phase | (menu_active ? 0x80 : 0);
    }
    return pressed;
}
#endif

static void update_frame(uint32_t *last_tick, int *viewleaf, uint16_t *oldpad)
{
    uint32_t now = time_now();
    int ticks = now - *last_tick;
    volatile uint16_t *latch = UNCACHED(&pad_latch);
    uint16_t pad = PAD, seen, pressed;
    fixed_t eye[3];
    int leaf;

    seen = *latch | pad_poll;
    *latch = 0;
    pad_poll = 0;
    if (pad & BTN_NOPAD) pad = 0;
    if (seen & BTN_NOPAD) seen = 0;
    pressed = (seen | pad) & ~*oldpad;
    *oldpad = pad;
    *last_tick = now;
    frame_now = now;
#ifdef DEMO
    pressed = demo_update(now, ticks, pressed);    /* real time: the camera keeps pace */
#endif
    if (ticks > 6) ticks = 6;
    if (hint_time > 0) {
        hint_time -= ticks;
        if (pressed) hint_time = 0;
    }

    if (pressed & BTN_START) {
        menu_active ^= 1;
        pressed &= ~BTN_START;
    }
    if (menu_active)
        menu_input(pressed);
#ifndef DEMO
    else
        while (ticks-- > 0)
            game_tick(pad);
#endif
    apply_settings();

    game_eye(eye);
    view.time = now;
    r_setup_view(eye, player.yaw, player.pitch);
    leaf = r_pointleaf(eye);
    view.underwater = lvl.leafs[leaf].contents <= CONTENTS_WATER
                      && lvl.leafs[leaf].contents >= CONTENTS_WATER - 2;     /* water, slime, lava */
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
    int split = SCREENH / 2, splith = SCREENH;

    level_init(level_data);
    r_init();
    r_span_init();
    r_oram_init();
    r_fb = r_screen;
    init_video();

    {
        uint32_t base = ((uint32_t)_bss_end + 15) & ~15;
        int each = ((FREE_TOP - base) / 2) & ~15;
        rcpu[0].cpu = 0;
        rcpu[1].cpu = 1;
        arena_base = (uint8_t *)base;
        arena_each = each;
    }

    game_init();
    view.cachegen = 1;
    view.mipbias = 0;
    apply_settings();
    r_poll_hook = poll_pad;
    last_tick = fps_tick = time_now();
    view.flip_fs = MARS_VDP_FBCTL & MARS_VDP_FS;
    update_frame(&last_tick, &viewleaf, &oldpad);

    for (;;) {
        int32_t mspin = 0, sspin;
        uint16_t fs;
        uint32_t now;

        /* render: master does rows [0,split), slave [split,SCREENH). Both
           start with geometry and wait for the previous flip only when they
           first write to the framebuffer. */
        if (splith != view.vh) {            /* screen size changed: keep the ratio */
            split = hw_div(split * view.vh, splith);
            splith = view.vh;
        }
        view.split = split;
        if (++tag == 0) tag = 1;
        SLAVE_GO = tag;
        r_render(&rcpu[0], 0, split);
        /* the border is static: redraw it only into framebuffers that had a
           menu or hint drawn over it (or after a size change), so after the
           overlay goes away both framebuffers get one clean redraw */
        if (border_dirty) {
            if (view.vw < SCREENW)
                border_draw(win_x, win_y, view.vw, view.vh);
            border_dirty--;
        }
        if (menu_active || hint_time > 0)
            border_dirty = 2;
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
            {
                int lim = view.vh >> 3;
                if (split < lim) split = lim;
                if (split > view.vh - lim) split = view.vh - lim;
            }
        }

        if (menu_active)
            menu_draw();
        if (cfg.showfps)
            hud_draw(menu_active ? 0 : win_x * 2, menu_active ? 0 : win_y, fps10, split);  /* clear of the menu */
        if (hint_time > 0 && !menu_active)
            hint_draw();

        now = frame_now;
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
        SH2_CCR = CCR_PURGE;    /* purge: see the master's fresh view/PVS data */
        r_render(&rcpu[1], view.split, view.vh);
        SLAVE_DONE = tag;
        while (MASTER_DONE != tag)
            spin++;
        *(volatile int32_t *)UNCACHED(&slave_spin) = spin;
        MARS_SYS_COMM10 = tag;
    }
}

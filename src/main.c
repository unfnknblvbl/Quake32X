/* Quake 32X - main loops for the two SH-2s */
#include "g_local.h"
#include "bot.h"
#include "mdlink.h"
#include "levels.h"
#include "playdemo.h"

extern uint8_t _bss_end[];  /* linker symbol __bss_end */
volatile uint32_t vblank_count;
volatile uint16_t pad_latch;     /* OR of every pad sample, set by the VBlank IRQ */
volatile uint16_t *r_fb;
volatile uint16_t *r_screen = (volatile uint16_t *)0x24000200;
static uint8_t *arena_base;         /* per-CPU memory: liquid buffers + surface cache */
static int arena_each, arena_mesh = -1;
static int win_x, win_y;            /* view window origin (32X pixels and rows) */
volatile uint8_t dbg_phase;         /* demo phase, for the simulator's log */
volatile int32_t dbg_fps10;
volatile int32_t dbg_split, dbg_mspin, dbg_sspin;
#ifndef BAL_DIV
#define BAL_DIV 20000
#endif
#ifndef BAL_MAX
#define BAL_MAX 16
#endif         /* frames a second x 10, for the logs and harnesses */

/* the view weapon of the frame being drawn (the 68000 shows it at the flip),
   and of the frame being prepared */
typedef struct { int weapon, frame; } gun_t;
static gun_t gun, gun_next;
static int viewleaf = -1;
static int md_flush;                /* send all of the next frame's Mega Drive layer */
int g_mode = GM_PLAY;
int cur_level = -1;

#define SLAVE_GO     MARS_SYS_COMM4
#define SLAVE_DONE   MARS_SYS_COMM6
#define MASTER_DONE  MARS_SYS_COMM8
#define PAD          MARS_SYS_COMM12

#ifdef ONCHIP
#define FREE_TOP     0x06040000      /* stacks are in on-chip RAM */
#define CCR_PURGE    0x19            /* purge, keep two-way mode */
#else
#define FREE_TOP     0x0603E800      /* slave 2 KB and master 4 KB stacks above */
#define CCR_PURGE    0x11            /* purge, 4-way cache */
#endif

static rcpu_t rcpu[2];
static surfcache_t *schash[2][HASHSIZE];
static volatile int32_t slave_spin, slave_tag;
static uint16_t pad_poll;           /* pad samples taken by the master while rendering */
static int hint_time = 0;           /* the controls, shown when a game starts */
#ifndef DEMO
#define HINT_TICKS (8 * 60)         /* ... for 8 seconds */
#else
#define HINT_TICKS 0
#endif

/* 60 Hz time from whichever vblank counter is running: the 68000's (COMM14
   low byte) or the master SH-2's interrupt. Either one alone keeps the game
   going. */
static uint32_t time_now(void)
{
    static uint8_t last68;
    static uint32_t lastirq, t;
    uint8_t c = MARS_SYS_COMM14 & 0xFF;
    uint32_t v = get_vblank_count();
    uint32_t d68 = (uint8_t)(c - last68), dirq = v - lastirq;
    last68 = c;
    lastirq = v;
    t += d68 > dirq ? d68 : dirq;
    return t;
}

/* the XE-1 AP's axes, 7 bits each (centre 64), as the 68000 cycles them
   through COMM2: left X, Y, right X, Y, the throttle; and the extension */
static uint8_t an_axis[5] = { 64, 64, 64, 64, 64 };
static int an_ext;
int pad_kind;

static void an_sample(void)
{
    uint16_t w = MARS_SYS_COMM2;
    switch (w >> 14) {
    case 0: an_axis[0] = (w >> 7) & 127; an_axis[1] = w & 127; break;
    case 1: an_axis[2] = (w >> 7) & 127; an_axis[3] = w & 127; break;
    case 2: an_axis[4] = (w >> 7) & 127; an_ext = (w >> 6) & 1; break;
    }
}

/* an axis past the dead zone, -1 .. 1 (16.16); squared for turning and
   looking, so small movements are fine */
static fixed_t an_value(int v, int curve)
{
    static const uint8_t dead[NUM_AN_DEAD] = { 2, 5, 8, 12 };
    int d = dead[cfg.an_dead], s = v - 64, a = s < 0 ? -s : s;
    fixed_t f;
    if (a <= d)
        return 0;
    f = hw_div((a - d) << 16, 63 - d);
    if (f > 0x10000) f = 0x10000;
    if (curve) f = fixmul(f, f);
    return s < 0 ? -f : f;
}

static void an_update(void)
{
    fixed_t lx = an_value(an_axis[0], cfg.an_left), ly = an_value(an_axis[1], 0);
    fixed_t rx = an_value(an_axis[2], 1), ry;
    if (an_ext)
        ry = an_value(an_axis[3], 1);
    else if (cfg.an_throttle)
        ry = -an_value(an_axis[4], 1);      /* the throttle as the right stick's Y: up = 255 */
    else
        ry = 0;
    an_in.fwd = -ly;
    an_in.side = cfg.an_left ? 0 : lx;
    an_in.turn = rx + (cfg.an_left ? lx : 0);
    an_in.look = cfg.an_right ? 0 : (cfg.an_invert ? -ry : ry);
}

/* called by the master between batches while it renders */
static void poll_pad(void)
{
    pad_poll |= PAD;
    an_sample();
    md_pump();
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

GCODE static void init_video(void)
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

/* Quake's colour shifts (V_UpdatePalette): the view in a liquid, damage,
 * pickups and powerups tint the 32X palette, each in turn toward its
 * colour by its share of 255. The CRAM is rewritten only when it changes */
uint16_t pal_buf[256] __attribute__((aligned(16)));
volatile int32_t pal_dirty;         /* the V blank interrupt copies pal_buf to CRAM (crt0.s) */

GCODE static void update_palette(int contents)
{
    static uint32_t last_key = 0xFFFFFFFF;
    struct { uint8_t r, g, b, pct; } sh[4];
    int n = 0, i, k;
    uint32_t key;
    if (contents == CONTENTS_WATER) { sh[n].r = 130; sh[n].g = 80; sh[n].b = 50; sh[n++].pct = 128; }
    else if (contents == CONTENTS_SLIME) { sh[n].r = 0; sh[n].g = 25; sh[n].b = 5; sh[n++].pct = 150; }
    else if (contents == CONTENTS_LAVA) { sh[n].r = 255; sh[n].g = 80; sh[n].b = 0; sh[n++].pct = 150; }
    if (ps.dmg_flash) { sh[n].r = 255; sh[n].g = 0; sh[n].b = 0; sh[n++].pct = ps.dmg_flash; }
    if (ps.bonus_flash) { sh[n].r = 215; sh[n].g = 186; sh[n].b = 69; sh[n++].pct = ps.bonus_flash; }
    if (ps.quad_finished > level_time) { sh[n].r = 0; sh[n].g = 0; sh[n].b = 255; sh[n++].pct = 30; }
    else if (ps.suit_finished > level_time) { sh[n].r = 0; sh[n].g = 255; sh[n].b = 0; sh[n++].pct = 20; }
    else if (ps.invis_finished > level_time) { sh[n].r = 100; sh[n].g = 100; sh[n].b = 100; sh[n++].pct = 100; }
    key = n ^ (uint32_t)r_palette;
    for (k = 0; k < n; k++)
        key = key * 2654435761u + ((uint32_t)sh[k].r << 24 | sh[k].g << 16 | sh[k].b << 8 | (sh[k].pct & ~3));
    if (key == last_key)
        return;
    last_key = key;
    for (i = 0; i < 256; i++) {
        uint16_t c = r_palette[i];
        int r = (c & 31) << 3, g = ((c >> 5) & 31) << 3, b = ((c >> 10) & 31) << 3;
        for (k = 0; k < n; k++) {
            r += (sh[k].pct * (sh[k].r - r)) >> 8;
            g += (sh[k].pct * (sh[k].g - g)) >> 8;
            b += (sh[k].pct * (sh[k].b - b)) >> 8;
        }
        pal_buf[i] = (c & 0x8000) | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);
    }
    /* the palette can only be written in a blank: at the next V blank */
    *(volatile int32_t *)UNCACHED(&pal_dirty) = 1;
}

/* the menu settings into the next frame's view; the surface caches and the
   window move only at the commit (the slave may still be drawing) */
GCODE static void apply_settings(void)
{
    const viewsize_t *vs = &view_sizes[cfg.size];
    int vh = vs->h < SCREENH - SBAR_H ? vs->h : SCREENH - SBAR_H;   /* above the status bar */
    if (view_next.lighting != cfg.lighting)
        view_next.cachegen++;
    view_next.textures = cfg.textures && !(lvl.flags & LF_FLAT_ONLY);
    md_gun_flat = !view_next.textures;
    view_next.lighting = cfg.lighting;
    view_next.far = far_levels[cfg.far] << 16;
    view_next.mesh = cfg.mesh;
    view_next.vw = vs->w;
    view_next.vh = vh;
}

/* the prepared frame becomes the one drawn: both CPUs are idle */
static void commit_frame(void)
{
    int fs = view.flip_fs;
    r_commit();
    view.flip_fs = fs;
    g_commit_scene();
    gun = gun_next;
    if (arena_mesh != view.mesh) {
        /* the slave purges its cache before its next frame */
        arena_mesh = view.mesh;
        r_arena(&rcpu[0], arena_base, arena_each, schash[0], view.mesh);
        r_arena(&rcpu[1], arena_base + arena_each, arena_each, schash[1], view.mesh);
    }
    win_x = (SCREENW - view.vw) >> 1;
    win_y = (SCREENH - SBAR_H - view.vh) >> 1;
    r_fb = r_screen + win_y * SCREENW + win_x;
}

/* input, game ticks, view and PVS for the next frame */
static uint32_t frame_now;

#ifdef DEMO_CAMERA
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
        dbg_phase = ph->phase | (menu_active ? 0x80 : 0);
    }
    return pressed;
}
#endif

/* input and game ticks. The master runs this right after drawing its own
 * rows, while the slave is still drawing: nothing here touches what the
 * slave reads (view, the scene, the PVS), only game state, and the row
 * split rebalances to give the slave more of the picture. */
static void update_input(uint32_t *last_tick, uint16_t *oldpad)
{
#ifdef FIXED_TICKS
    uint32_t now = *last_tick + FIXED_TICKS;    /* test builds: the same game at any speed */
#else
    uint32_t now = time_now();
#endif
    int ticks = now - *last_tick, demo_ticks;
    volatile uint16_t *latch = UNCACHED(&pad_latch);
    uint16_t pad = PAD, seen, pressed;
    int analog;

    seen = *latch | pad_poll;
    *latch = 0;
    pad_poll = 0;
    an_sample();
    /* 0xF000 = no pad; bit 12 alone marks a 6-button pad; bit 13 the XE-1
       AP, with its triggers in bits 14 and 15 */
    if ((pad & BTN_NOPAD) == BTN_NOPAD) pad = 0;
    if ((seen & BTN_NOPAD) == BTN_NOPAD) seen = 0;
    analog = (pad & AN_PAD) != 0;
    pad_kind = analog ? 2 + an_ext : pad ? 1 : 0;
    pad &= analog ? 0xCFFF : 0x0FFF;
    seen &= analog ? 0xCFFF : 0x0FFF;
    pressed = (seen | pad) & ~*oldpad;
    *oldpad = pad;
    *last_tick = now;
    frame_now = now;
#ifdef DEMO_CAMERA
    pressed = demo_update(now, ticks, pressed);    /* real time: the camera keeps pace */
#endif
    /* the game takes at most 6 ticks a frame (below 10 fps it slows down
       rather than taking big steps); the attract demo keeps real time, as
       Quake's does, since it only replays where things were */
    demo_ticks = ticks > 60 ? 60 : ticks;
    if (ticks > 6) ticks = 6;
    if (hint_time > 0) {
        hint_time -= ticks;
        if (pressed) hint_time = 0;
    }

    if (pressed & BTN_START) {
        menu_active ^= 1;
        pressed &= ~BTN_START;
    }
    if (menu_active) {
        menu_input(pressed);                /* the menu reads the pad itself */
    } else if (g_mode == GM_DEMO) {
        /* the attract demo plays itself; START opens the menu */
        while (demo_ticks-- > 0)
            if (!pd_tick()) {
                g_request = REQ_DEMO;       /* over: again */
                break;
            }
    } else if (g_mode == GM_FINALE) {
        /* the closing screen: a button after two seconds, then the demo */
        static uint32_t since;
        if (!since) since = now;
        if (now - since > TICKS(2) && (map_pad(pressed) & (ACT_JUMP | ACT_FIRE))) {
            since = 0;
            g_request = REQ_DEMO;
        }
    } else {
        /* the player's buttons as actions (Controls page, or the analogue
           pad's page), or the bot's */
        uint16_t held = analog ? map_analog(pad) : map_pad(pad);
        pressed = analog ? map_analog(pressed) : map_pad(pressed);
        an_in.active = analog && !bot_active;
        if (an_in.active)
            an_update();
        if (bot_active && g_level_done > TICKS(6))
            g_request = REQ_DEMO;           /* the bot is through: the attract demo again */
        while (ticks-- > 0) {
            if (bot_active)
                bot_tick(&held, &pressed);
#ifndef DEMO_CAMERA
            if (!g_level_done)
                game_tick(held);
#endif
            g_tick(held, pressed);
            pressed = 0;
        }
    }
}

/* the next frame's view, PVS, BSP walk and scene, into view_next, vis, cands, scene_next
   and gun_next: the master does this while the slave still draws */
static void update_view(int *viewleaf)
{
    fixed_t eye[3];
    int leaf, yaw = player.yaw, pitch = player.pitch;
    apply_settings();
    if (g_mode == GM_DEMO) {
        pd_update();                /* the demo's things, movers and view */
        pd_eye(eye);
        yaw = player.yaw;
        pitch = player.pitch;
    } else
        game_eye(eye);
    if (g_level_done || g_mode == GM_FINALE)
        g_intermission_view(eye, &yaw, &pitch);
    view_next.time = frame_now;
    r_setup_view(eye, yaw, pitch);
    leaf = r_pointleaf(eye);
    view_next.underwater = lvl.leafs[leaf].contents <= CONTENTS_WATER
                      && lvl.leafs[leaf].contents >= CONTENTS_WATER - 2;     /* water, slime, lava */
    update_palette(view_next.underwater ? lvl.leafs[leaf].contents : 0);
    if (lvl.leaffar && lvl.leaffar[leaf] && ((fixed_t)lvl.leaffar[leaf] << 16) < view_next.far)
        view_next.far = (fixed_t)lvl.leaffar[leaf] << 16;  /* this area's draw distance */
    if (leaf != *viewleaf || r_doors_changed()) {
        r_markleaves(leaf);
        *viewleaf = leaf;
    }
    g_build_scene();
    r_walk();                       /* after the movers' state (g_build_scene) */
    snd_update();

    /* the view weapon: the Mega Drive draws it, the 32X skips what it hides */
    if (g_mode == GM_DEMO) {
        gun_next.weapon = pd_gun(&gun_next.frame);
        if (gun_next.weapon < 0)
            gun_next.frame = -1;
    } else if (!ps.dead && !cfg.fly && !g_level_done && g_mode == GM_PLAY && ps.invis_finished <= level_time) {
        switch (ps.weapon) {
        case IT_AXE: gun_next.weapon = MDW_AXE; break;
        case IT_SUPER_SHOTGUN: gun_next.weapon = MDW_SUPER_SHOTGUN; break;
        case IT_NAILGUN: gun_next.weapon = MDW_NAILGUN; break;
        case IT_GRENADE_LAUNCHER: gun_next.weapon = MDW_GRENADE_LAUNCHER; break;
        default: gun_next.weapon = MDW_SHOTGUN;
        }
        gun_next.frame = ps.weaponframe;
    } else
        gun_next.frame = -1;     /* (invisible: no weapon, as in Quake) */
    {
        md_cut_t cut;
#ifdef NO_CUTOUT
        if (0) {
#else
        if (md_gun_cut(gun_next.weapon, cfg.size, gun_next.frame, &cut)) {
#endif
            view_next.cut_row0 = cut.row0;
            view_next.cut_n = cut.nrows;
            view_next.cut_x = cut.x;
        } else
            view_next.cut_n = 0;
    }
}

/* a level into the window and everything that depends on it; both CPUs
 * are idle (the slave waits for its next frame) */
GCODE static void load_level(int n)
{
    snd_stop_all();                 /* the mixer must let go of the old pages */
    level_init(lvl_map(n));
    cur_level = n;
    r_init();
    r_level_init();
    arena_mesh = -1;                /* empty surface caches (commit_frame) */
    view_next.cachegen++;
    viewleaf = -1;
    snd_init();
    g_init();
    game_init();
}

/* the attract mode: the demo's level and the demo from the start (or, if
 * the cartridge has none, a game) */
GCODE static void start_demo(void)
{
    int n = lvl_demo();
    bot_stop();
    if (n < 0) {
        g_mode = GM_PLAY;
        g_new_game();
        load_level(0);
        return;
    }
    g_mode = GM_DEMO;
    g_new_game();
    load_level(n);
    if (!pd_start()) {
        g_mode = GM_PLAY;
        load_level(0);
    }
}

/* between frames: what the game asked for */
GCODE static void handle_request(void)
{
    int req = g_request;
    g_request = REQ_NONE;
    switch (req) {
    case REQ_NEXT_LEVEL:
        if (cur_level + 1 < lvl_count()) {
            g_change_parms();
            load_level(cur_level + 1);
        } else {
            /* the end of the cartridge's levels: the closing screen over
               the last level's intermission view */
            g_mode = GM_FINALE;
            g_level_done = 0;
            bot_stop();
        }
        break;
    case REQ_RESTART:
        g_init();
        game_respawn();
        break;
    case REQ_NEW_GAME:
    case REQ_BOT:
        g_mode = GM_PLAY;
        bot_stop();
        menu_active = 0;
        g_new_game();
        load_level(0);
        hint_time = req == REQ_BOT ? 0 : HINT_TICKS;
        if (req == REQ_BOT)
            bot_start();
        break;
    case REQ_DEMO:
        start_demo();
        break;
    }
}

void pri_main(void)
{
    uint32_t last_tick, fps_tick;
    int frames = 0, fps10 = 0;
    uint16_t oldpad = 0, tag = 0;
    int split = SCREENH / 2, splith = SCREENH;

    md_init();
    g_new_game();
#ifdef START_LEVEL
    load_level(START_LEVEL);            /* test builds: another level first */
#elif defined(DEMO_CAMERA) || defined(START_POS)
    load_level(0);
#else
    start_demo();                       /* like Quake: the demo first */
#endif
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

    scene.zbuf = zbuf_mem;
#if defined(DEMO_CAMERA) || defined(START_POS)
    cfg.monsters = 0;                   /* camera routes and benchmark views: no monsters */
#endif
    view_next.cachegen = 1;
    view_next.mipbias = 0;
#ifdef TEST_CFG
    TEST_CFG;                           /* test builds: settings from the start */
#endif
    apply_settings();
    r_poll_hook = poll_pad;
    last_tick = fps_tick = time_now();
    view.flip_fs = MARS_VDP_FBCTL & MARS_VDP_FS;
    update_input(&last_tick, &oldpad);
    update_view(&viewleaf);

    for (;;) {
#ifdef TEST_NEXT
        {   /* test builds: through the levels every TEST_NEXT ticks */
            static uint32_t next_at = TEST_NEXT;
            if (frame_now >= next_at) {
                next_at = frame_now + TEST_NEXT;
                g_request = REQ_NEXT_LEVEL;
            }
        }
#endif
        if (g_request) {
            /* both CPUs are idle here: a new level, or this one again */
            handle_request();
            last_tick = time_now();
            update_view(&viewleaf);
            md_flush = 1;
        }
        commit_frame();
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
#ifndef SERIAL_FRAME
        update_input(&last_tick, &oldpad);  /* game ticks while the slave draws */
#endif
        /* the text, status bar and menu go into the Mega Drive's queue
           while the slave still draws, and the master feeds the 68000
           while it waits: nothing of this keeps the slave waiting */
        overlays_draw(win_x, win_y, fps10, hint_time > 0);
        md_pump();
        /* the next frame's view, PVS and scene while the slave still draws:
           the balance hands the slave more rows for it */
#ifndef SERIAL_FRAME
        update_view(&viewleaf);
#endif
        while (SLAVE_DONE != tag) {
            if (!(mspin & 31))
                md_pump();          /* (now and then: spins are what balances the split) */
            mspin++;
        }
#ifdef SERIAL_FRAME
        /* test builds: the game and the next frame only once the slave is
           done, so that particles (which the slave draws from the game's
           array) are the same at any speed */
        update_input(&last_tick, &oldpad);
        update_view(&viewleaf);
#endif
        MARS_SYS_COMM8 = tag;
        while (*(volatile int32_t *)UNCACHED(&slave_tag) != tag) ;     /* slave publishes its wait */
        sspin = *(volatile int32_t *)UNCACHED(&slave_spin);
        dbg_split = split | (view.vh << 8);
        dbg_mspin = mspin;
        dbg_sspin = sspin;

        /* balance the two halves using who waited for whom: one spin is a
           few cycles, a row costs roughly 10-20k cycles */
        {
            int32_t d = mspin - sspin, step;
            /* a row is worth about 20000 spins: move about half the gap */
            step = d / BAL_DIV;
            if (step == 0 && (d > 64 || d < -64)) step = d > 0 ? 1 : -1;
            if (step > BAL_MAX) step = BAL_MAX;
            if (step < -BAL_MAX) step = -BAL_MAX;
#ifndef FIXED_SPLIT
            split += step;
#endif
            {
                int lim = view.vh >> 3;
                if (split < lim) split = lim;
                if (split > view.vh - lim) split = view.vh - lim;
            }
        }

        if (menu_active || md_flush)
            md_wait();              /* paused, or a new level: show it all at once */
        md_flush = 0;

        now = frame_now;
        frames++;
        if (now - fps_tick >= 60) {
            fps10 = hw_div(frames * 600, now - fps_tick);
            frames = 0;
            fps_tick = now;
        }
        dbg_fps10 = fps10;

        /* request the flip; it happens at the next vertical blank while the
           next frame's geometry runs */
        fs = MARS_VDP_FBCTL & MARS_VDP_FS;
        MARS_VDP_FBCTL = fs ^ 1;
        view.flip_fs = fs ^ 1;
        md_gun(gun.weapon, cfg.size, gun.frame);    /* with the flip */
        md_pump();
    }
}

void sec_main(void)
{
    uint16_t last = 0;
    int sound_on = 0;
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
        if (!sound_on) {
            snd_init_slave();   /* the master has loaded everything by now */
            sound_on = 1;
        }
        r_render(&rcpu[1], view.split, view.vh);
        SLAVE_DONE = tag;
        while (MASTER_DONE != tag)
            spin++;
        *(volatile int32_t *)UNCACHED(&slave_spin) = spin;
        *(volatile int32_t *)UNCACHED(&slave_tag) = tag;
    }
}

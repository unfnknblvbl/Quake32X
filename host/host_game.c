/* Host build of the whole game (renderer, game logic, demo bot) for fast
 * iteration: runs the bot through the level at a fixed tick rate and
 * writes frames as raw RGB24 320x224 (pipe into ffmpeg) or PPM stills.
 *
 *   q32x_game <level_le.lvl> <frames> [ticks/frame] [out.rgb] [shotdir every]
 * env: FLAT=1 textures off, CAP=n monster cap index, LOG=1 per-frame log
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/g_local.h"
#include "../src/bot.h"

int32_t host_div_result;
uint16_t host_comm0;
volatile uint16_t *r_fb, *r_screen;
static uint16_t fbmem[SCREENW * SCREENH];

#define ARENA (44 * 1024)

void host_bad_texel(const frec_t *fr, int s, int t, int x, int y)
{
    fprintf(stderr, "bad texel face %d mip %d s %x t %x smax %x tmax %x surf %dx%d at %d,%d zi0 %x\n", fr->fi, fr->mip, s, t,
            fr->smax, fr->tmax, fr->surf->width, fr->surf->height, x, y, fr->zi0);
    abort();
}


/* PADSCRIPT=file: lines "from to buttons" (frames; U D L R A B C S) instead of the bot */
static struct { int a, b; uint16_t btn; } pads[256];
static int npads;
static void load_pads(const char *fn)
{
    FILE *f = fopen(fn, "r");
    char bs[32];
    while (f && npads < 256 && fscanf(f, "%d %d %31s", &pads[npads].a, &pads[npads].b, bs) == 3) {
        uint16_t b = 0;
        for (char *c = bs; *c; c++)
            b |= *c == 'U' ? BTN_UP : *c == 'D' ? BTN_DOWN : *c == 'L' ? BTN_LEFT : *c == 'R' ? BTN_RIGHT
               : *c == 'A' ? BTN_A : *c == 'B' ? BTN_B : *c == 'C' ? BTN_C : *c == 'S' ? BTN_START : 0;
        pads[npads++].btn = b;
    }
    if (f) fclose(f);
}
static uint16_t pad_at(int fr)
{
    uint16_t b = 0;
    for (int i = 0; i < npads; i++)
        if (fr >= pads[i].a && fr <= pads[i].b) b |= pads[i].btn;
    return b;
}

static void put_rgb(FILE *f)
{
    static uint8_t row[320 * 3];
    for (int y = 0; y < 224; y++) {
        for (int x = 0; x < 320; x++) {
            int c = ((const uint8_t *)fbmem)[(y >> 1) * SCREENW * 2 + x];
            uint16_t p = lvl.palette[c];
            row[x * 3] = (p & 31) << 3;
            row[x * 3 + 1] = ((p >> 5) & 31) << 3;
            row[x * 3 + 2] = ((p >> 10) & 31) << 3;
        }
        fwrite(row, 1, sizeof(row), f);
    }
}

int main(int argc, char **argv)
{
    FILE *f;
    long sz;
    uint8_t *blob;
    static rcpu_t rc[2];
    static surfcache_t *hash[2][HASHSIZE];
    int frames, tpf, k, leaf, viewleaf = -1;
    FILE *vid = NULL;
    const char *shotdir = NULL;
    int every = 0;

    if (argc < 3) {
        fprintf(stderr, "usage: %s level frames [ticks/frame] [out.rgb] [shotdir every]\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    blob = malloc(sz);
    if (fread(blob, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);
    frames = atoi(argv[2]);
    tpf = argc > 3 ? atoi(argv[3]) : 4;
    if (argc > 4 && argv[4][0] != '-') vid = fopen(argv[4], "wb");
    if (argc > 6) { shotdir = argv[5]; every = atoi(argv[6]); }

    level_init(blob);
    r_init();
    r_span_init();
    r_screen = fbmem;
    for (k = 0; k < 2; k++) {
        rc[k].cpu = k;
        r_arena(&rc[k], malloc(ARENA), ARENA, hash[k], 0);
    }
    view.cachegen = 1;
    view.vw = SCREENW;
    view.vh = SCREENH - SBAR_H;
    r_fb = r_screen;
    game_init();
    scene.zbuf = zbuf_mem;
    if (getenv("CAP")) cfg.monsters = atoi(getenv("CAP"));
    g_init();
    if (getenv("MODELTEST")) {
        /* a monster (CL_ARMY 1 / CL_DOG 2) DIST units ahead of the start,
           posed on frame FRAME, facing YAW; no AI */
        ent_t *e = g_spawn();
        int dist = getenv("DIST") ? atoi(getenv("DIST")) : 150;
        e->org[0] = player.pos[0] + fixmul(fcos(player.yaw), FX(dist));
        e->org[1] = player.pos[1] + fixmul(fsin(player.yaw), FX(dist));
        e->org[2] = player.pos[2];
        m_spawn(e, atoi(getenv("MODELTEST")), 0);
        e->yaw = (getenv("YAW") ? atoi(getenv("YAW")) : 270) * 65536 / 360;
        e->frame = getenv("FRAME") ? atoi(getenv("FRAME")) : 0;
        e->nextthink = 0xFFFFFFF;
        cfg.monsters = 0;
    }

    if (getenv("PADSCRIPT")) load_pads(getenv("PADSCRIPT"));
    else if (!getenv("MODELTEST")) bot_start();     /* the demo bot plays */
    if (getenv("START")) {          /* START="x y z yaw": where the player begins */
        float x, y, z, a;
        sscanf(getenv("START"), "%f %f %f %f", &x, &y, &z, &a);
        player.pos[0] = (fixed_t)(x * 65536); player.pos[1] = (fixed_t)(y * 65536);
        player.pos[2] = (fixed_t)(z * 65536); player.yaw = (int)(a * 65536 / 360) & 0xFFFF;
    }
    for (int fr = 0; fr < frames; fr++) {
        uint16_t pressed = 0, held = 0, ppad = 0;
        fixed_t eye[3];
        if (npads) {
            ppad = pad_at(fr);
            pressed = ppad & ~pad_at(fr - 1);
        }
        if (pressed & BTN_START) { menu_active ^= 1; pressed &= ~BTN_START; }
        if (menu_active)
            menu_input(pressed);
        else
            for (k = 0; k < tpf && !getenv("MODELTEST"); k++) {
                if (bot_active) bot_tick(&held, &pressed);
                else { held = map_pad(ppad); if (k == 0) pressed = map_pad(pressed); }
                if (!g_level_done)
                    game_tick(held);
                g_tick(held, pressed);
                pressed = 0;
            }
        if (getenv("FLAT")) cfg.textures = 0;
        view.textures = cfg.textures;
        view.lighting = cfg.lighting;
        view.far = far_levels[cfg.far] << 16;
        game_eye(eye);
        view.time = fr * tpf;
        r_setup_view(eye, player.yaw, player.pitch);
        leaf = r_pointleaf(eye);
        view.underwater = lvl.leafs[leaf].contents <= CONTENTS_WATER && lvl.leafs[leaf].contents >= CONTENTS_WATER - 2;
        if (leaf != viewleaf) { r_markleaves(leaf); viewleaf = leaf; }
        g_build_scene();
        r_render(&rc[0], 0, view.vh / 2);
        r_render(&rc[1], view.vh / 2, view.vh);
        {
            char buf[48];
            int hi = g_hud_text(buf);
            memset(fbmem + (SCREENH - SBAR_H) * SCREENW, 0, SBAR_H * SCREENW * 2);
            draw_text(4, SCREENH - SBAR_H + 1, buf, hi);
        }
        if (g_level_done)
            intermission_draw(ps.kills, ps.total_monsters, g_level_ticks / 60, ps.secrets, ps.total_secrets);
        if (menu_active)
            menu_draw();
        if (getenv("LOG")) {
            int nm = 0;
            for (k = 0; k < MAX_ENTS; k++) nm += ents[k].type == ET_MONSTER;
            extern int bot_dbg_wp(void);
            printf("wp %d det %d ", bot_dbg_wp() & 255, (bot_dbg_wp() >> 8) - 1);
            printf("f %d t %u pos %d %d %d yaw %d hp %d sh %d kills %d alive %d draw %d zused %d parts %d phase %d\n",
                   fr, level_time, UNITS(player.pos[0]), UNITS(player.pos[1]), UNITS(player.pos[2]),
                   player.yaw * 360 / 65536, ps.health, ps.shells, ps.kills, nm, scene.ndraw, scene.zused,
                   scene.nparts, host_comm0);
        }
        if (getenv("DOORLOG")) {
            static int last[64];
            for (k = 0; k < 48; k++) {
                int st = mv_state(k);
                if (fr && st != last[k])
                    printf("   mover %d state %d -> %d at f %d pos %d %d %d\n", k, last[k], st, fr,
                           UNITS(player.pos[0]), UNITS(player.pos[1]), UNITS(player.pos[2]));
                last[k] = st;
            }
        }
        if (getenv("MLOG") && fr % atoi(getenv("MLOG")) == 0)
            for (k = 0; k < MAX_ENTS; k++) {
                const ent_t *e = &ents[k];
                if (e->type != ET_MONSTER && e->type != ET_CORPSE) continue;
                printf("   ent %d type %d cls %d q %d pos %d %d %d hp %d frame %d hunt %d ground %d yaw %d ideal %d\n", k, e->type, e->cls,
                       e->queue, UNITS(e->org[0]), UNITS(e->org[1]), UNITS(e->org[2]), e->health, e->frame, e->hunting,
                       e->onground, e->yaw * 360 / 65536, e->ideal_yaw * 360 / 65536);
            }
        if (vid) put_rgb(vid);
        if (shotdir && every && fr % every == 0) {
            char name[256];
            snprintf(name, sizeof(name), "%s/f%05d.ppm", shotdir, fr);
            FILE *o = fopen(name, "wb");
            fprintf(o, "P6\n320 224\n255\n");
            put_rgb(o);
            fclose(o);
        }
    }
    if (vid) fclose(vid);
    return 0;
}

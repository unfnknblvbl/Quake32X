/* Playing back one of Quake's demos (the attract mode), from the stream
 * tools/mkdemo.py made of it, in its level's blob (LVL_DEMO).
 *
 * As in Quake's client, a demo frame is what the server sent at one
 * moment: the things in view (each as its baseline with some fields
 * changed), the movers' positions, the player's, sounds, effects and the
 * status bar. Frames are read as the demo's clock passes their times, and
 * positions are drawn between the last two (CL_RelinkEntities' lerp). The
 * things go into ents[] for g_build_scene, the movers into mover_off, the
 * player's view into player. Nothing of the game itself runs. */
#include "g_local.h"
#include "playdemo.h"
#include "mdlink.h"

#define PD_MAX 48                   /* things drawn in one frame */
#define PD_NUMS 512                 /* entity numbers */

typedef struct {
    uint16_t num;
    uint8_t  model, frame, skin, effects;
    uint8_t  yaw, pitch;
    int16_t  org[3];                /* 1/8 units */
} pdent_t;

typedef struct {
    uint32_t nframes, nbase, base_ofs, frames_ofs, strings_ofs, max_ents;
} pdhdr_t;

static const pdhdr_t *hdr;
static const uint8_t *demo, *fp;    /* the stream; the next frame */
static int frame_no;
static pdent_t ent_a[PD_MAX], ent_b[PD_MAX], *prev = ent_a, *cur = ent_b;
static int nprev, ncur;
static uint8_t prev_slot[PD_NUMS];  /* entity number -> 1 + index in prev */
static int16_t view_a[3], view_b[3];
static uint16_t ang_a[2], ang_b[2];
static int16_t mov_a[MAX_MOVERS][3], mov_b[MAX_MOVERS][3];
static uint8_t mov_seen[MAX_MOVERS];
static int32_t t_a, t_b, clock600;  /* frame times (1/100 s); the clock (1/600 s) */
static int viewheight = 22;
static int pd_weapon = -1, pd_wframe;
int pd_active;

static inline int rd16(const uint8_t *p) { return (int16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static void to_fixed(const int16_t c[3], fixed_t f[3])
{
    f[0] = (fixed_t)c[0] << 13;
    f[1] = (fixed_t)c[1] << 13;
    f[2] = (fixed_t)c[2] << 13;
}

static void rd_org(const uint8_t *p, int16_t o[3])
{
    o[0] = rd16(p);
    o[1] = rd16(p + 2);
    o[2] = rd16(p + 4);
}

/* ------------------------------------------------------------ records */

static void temp_entity(int kind, const fixed_t org[3])
{
    switch (kind) {
    case 0: case 1:                 /* spike, super spike */
        g_particles(org, NULL, 0, kind ? 10 : 5);
        if (g_rand() % 5)
            snd_start(SND_TINK1, org, 255);
        else
            snd_start(SND_RIC1 + (g_rand() % 3), org, 255);
        break;
    case 2: g_particles(org, NULL, 0, 10); break;           /* gunshot */
    case 3: g_explosion(org); break;
    case 4: g_burst(org, 0xD0, 40, 256, TICKS(0.8)); snd_start(SND_R_EXP3, org, 255); break;
    case 7: g_particles(org, NULL, 20, 15); snd_start(SND_WIZARD_HIT, org, 255); break;
    case 8: g_particles(org, NULL, 226, 10); break;
    case 10: g_burst(org, 224, 40, 64, TICKS(1)); break;
    case 11: g_teleport_fx(org); break;
    }
}

/* the stream's client data: the status bar and the view weapon */
static void client_data(const uint8_t *p)
{
    static const uint16_t weapons[5] = { IT_AXE, IT_SHOTGUN, IT_SUPER_SHOTGUN, IT_NAILGUN, IT_GRENADE_LAUNCHER };
    int ammo = p[3], w = p[4], items = p[6];
    ps.health = rd16(p);
    ps.armor = p[2];
    pd_weapon = w < 5 ? w : -1;
    pd_wframe = p[5];
    ps.weapon = w < 5 ? weapons[w] : IT_AXE;
    switch (ps.weapon) {
    case IT_SHOTGUN: case IT_SUPER_SHOTGUN: ps.shells = ammo; break;
    case IT_NAILGUN: ps.nails = ammo; break;
    case IT_GRENADE_LAUNCHER: ps.rockets = ammo; break;
    }
    ps.items = items & 3;
    ps.invis_finished = items & 4 ? level_time + TICKS(1) : 0;
    ps.quad_finished = items & 8 ? level_time + TICKS(1) : 0;
    viewheight = (int8_t)p[7];
    ps.dead = ps.health <= 0;
}

/* read the next frame: the current one becomes the previous */
static int read_frame(void)
{
    const uint8_t *p = fp;
    pdent_t *t;
    int i;
    if (frame_no >= (int)hdr->nframes)
        return 0;
    t = prev; prev = cur; cur = t;
    nprev = ncur;
    ncur = 0;
    memset(prev_slot, 0, sizeof(prev_slot));
    for (i = 0; i < nprev; i++)
        if (prev[i].num < PD_NUMS)
            prev_slot[prev[i].num] = i + 1;
    view_a[0] = view_b[0]; view_a[1] = view_b[1]; view_a[2] = view_b[2];
    ang_a[0] = ang_b[0]; ang_a[1] = ang_b[1];
    memcpy(mov_a, mov_b, sizeof(mov_a));
    t_a = t_b;
    t_b = (uint16_t)rd16(p);
    ang_b[0] = rd16(p + 2);
    ang_b[1] = rd16(p + 4);
    p += 6;
    for (;;) {
        int op = *p++;
        fixed_t org[3];
        int16_t o[3];
        if (op == 0)
            break;
        switch (op) {
        case 1: {                   /* a thing: its baseline, then the fields that differ */
            int num = (p[0] << 8) | p[1], bits = p[2];
            const uint8_t *b = demo + hdr->base_ofs + 12 * (num < (int)hdr->nbase ? num : 0);
            pdent_t *e = ncur < PD_MAX ? &cur[ncur] : NULL;
            p += 3;
            if (e) {
                e->num = num;
                e->model = b[0];
                e->frame = b[1];
                e->skin = b[2];
                e->org[0] = rd16(b + 4);
                e->org[1] = rd16(b + 6);
                e->org[2] = rd16(b + 8);
                e->pitch = b[3];
                e->yaw = b[10];
                e->effects = 0;
            }
            if (bits & 1) { if (e) e->model = p[0]; p++; }
            if (bits & 2) { if (e) e->frame = p[0]; p++; }
            if (bits & 4) { if (e) e->skin = p[0]; p++; }
            if (bits & 8) { if (e) rd_org(p, e->org); p += 6; }
            if (bits & 16) { if (e) e->yaw = p[0]; p++; }
            if (bits & 32) { if (e) e->pitch = p[0]; p++; }
            if (bits & 64) { if (e) e->effects = p[0]; p++; }
            if (e && e->model != 255 && g_model(e->model))
                ncur++;
            break;
        }
        case 2:                     /* a mover's offset */
            if (p[0] < MAX_MOVERS) {
                rd_org(p + 1, mov_b[p[0]]);
                if (!mov_seen[p[0]]) {
                    memcpy(mov_a[p[0]], mov_b[p[0]], sizeof(mov_a[0]));
                    mov_seen[p[0]] = 1;
                }
            }
            p += 7;
            break;
        case 3:                     /* the player */
            rd_org(p, view_b);
            p += 6;
            break;
        case 4:                     /* a sound */
            rd_org(p + 3, o);
            to_fixed(o, org);
            snd_play(p[0], org, p[1], p[2] >> 6);
            p += 9;
            break;
        case 5: {                   /* particles */
            fixed_t dir[3];
            int n = p[9];
            rd_org(p, o);
            to_fixed(o, org);
            dir[0] = (int8_t)p[6] << 12;
            dir[1] = (int8_t)p[7] << 12;
            dir[2] = (int8_t)p[8] << 12;
            if (n == 255)
                g_explosion(org);
            else
                g_particles(org, dir, p[10], n > 48 ? 24 : (n + 1) >> 1);
            p += 11;
            break;
        }
        case 6:                     /* a temporary entity */
            rd_org(p + 1, o);
            to_fixed(o, org);
            temp_entity(p[0], org);
            p += 7;
            break;
        case 7:
            client_data(p);
            p += 8;
            break;
        case 8: {
            int n = p[0] >> 1;
            g_flash(FLASH_DAMAGE, 3 * (n < 10 ? 10 : n));
            p += 1;
            break;
        }
        case 9:
            g_flash(FLASH_BONUS, 50);
            break;
        case 10:
            ps.msg = (const char *)demo + hdr->strings_ofs + (((p[0] << 8) | p[1]));
            ps.msg_until = level_time + TICKS(2);
            p += 2;
            break;
        case 11: {
            int v = (p[1] << 8) | p[2];
            switch (p[0]) {
            case 0: ps.kills = v; break;
            case 1: ps.total_monsters = v; break;
            case 2: ps.secrets = v; break;
            case 3: ps.total_secrets = v; break;
            case 4: ps.kills++; break;
            case 5: ps.secrets++; break;
            }
            p += 3;
            break;
        }
        default:
            return 0;               /* a record we do not know: stop */
        }
    }
    fp = p;
    frame_no++;
    return 1;
}

/* ------------------------------------------------------------ playback */

GCODE int pd_start(void)
{
    const lheader_t *h = lvl.hdr;
    if (!h->lump[LVL_DEMO].count)
        return 0;
    demo = lvl.base + h->lump[LVL_DEMO].ofs;
    hdr = (const pdhdr_t *)demo;
    fp = demo + hdr->frames_ofs;
    frame_no = 0;
    nprev = ncur = 0;
    memset(mov_seen, 0, sizeof(mov_seen));
    memset(view_b, 0, sizeof(view_b));
    t_a = t_b = 0;
    /* the first frame twice: nothing is drawn between nowhere and it */
    if (!read_frame())
        return 0;
    read_frame();
    clock600 = t_a * 6;
    pd_active = 1;
    return 1;
}

/* a 60 Hz tick of the demo's clock; returns 0 when the demo is over */
GCODE int pd_tick(void)
{
    if (!pd_active)
        return 0;
    level_time++;
    clock600 += 10;
    if (ps.flash)
        ps.flash = ps.flash > 3 ? ps.flash - 3 : 0;
    /* CL_GetMessage: the next frame once the clock is past the last one's time */
    while (clock600 > t_b * 6) {
        if (!read_frame()) {
            pd_active = 0;
            return 0;
        }
    }
    return 1;
}

static int lerp16(int a, int b, int f)      /* f: 0 .. 256 */
{
    return a + (((b - a) * f) >> 8);
}

/* everything at this moment between the last two frames: into ents[],
 * mover_off and the player, before g_build_scene */
GCODE void pd_update(void)
{
    int f, i, k;
    fixed_t o[3];
    int32_t span = t_b - t_a;
    f = span > 0 ? hw_div((clock600 - t_a * 6) * 256, span * 6) : 256;
    if (f < 0) f = 0;
    if (f > 256) f = 256;
    /* the things in view */
    for (i = ncur; i < MAX_ENTS; i++)
        ents[i].type = ET_FREE;
    for (i = 0; i < ncur; i++) {
        const pdent_t *c = &cur[i];
        int s = c->num < PD_NUMS ? prev_slot[c->num] : 0;
        ent_t *e = &ents[i];
        int16_t p[3];
        const lmodel_t *m;
        if (i >= MAX_ENTS)
            break;
        for (k = 0; k < 3; k++) {
            p[k] = c->org[k];
            if (s) {
                int d = c->org[k] - prev[s - 1].org[k];
                if (d > -800 && d < 800)        /* not across a teleport */
                    p[k] = lerp16(prev[s - 1].org[k], c->org[k], f);
            }
        }
        to_fixed(p, o);
        if (e->type != ET_ITEM || e->org[0] != o[0] || e->org[1] != o[1] || e->org[2] != o[2]
            || e->model != c->model) {
            memset(e, 0, sizeof(*e));
            e->type = ET_ITEM;
            e->org[0] = o[0]; e->org[1] = o[1]; e->org[2] = o[2];
            g_relink(e);
        }
        e->model = c->model;
        e->frame = c->frame;
        e->skin = c->skin;
        e->yaw = c->yaw << 8;
        m = g_model(c->model);
        e->type = ET_ITEM;
        if (m && (m->flags & (MF_GRENADE | MF_GIB | MF_ZOMGIB | MF_TRACER)) && s) {
            /* a trail behind what flies */
            particle_t *pt = (level_time & 1) ? NULL : g_new_particle(o);
            if (pt) {
                pt->color = (m->flags & MF_GRENADE) ? 0x0E - (g_rand() & 3)
                            : (m->flags & MF_TRACER) ? 52 + (g_rand() & 7) : 67 + (g_rand() & 3);
                pt->type = (m->flags & MF_GRENADE) ? 1 : (m->flags & MF_TRACER) ? 0 : 2;
                pt->vel[0] = pt->vel[1] = pt->vel[2] = 0;
                g_particle_life(pt, TICKS(0.4));
            }
        }
    }
    /* the movers */
    for (i = 0; i < nmovers; i++)
        if (mov_seen[i]) {
            int16_t p[3];
            for (k = 0; k < 3; k++)
                p[k] = lerp16(mov_a[i][k], mov_b[i][k], f);
            to_fixed(p, mover_off[i]);
        }
    /* the player */
    {
        int16_t p[3];
        for (k = 0; k < 3; k++) {
            int d = view_b[k] - view_a[k];
            p[k] = d > -800 && d < 800 ? lerp16(view_a[k], view_b[k], f) : view_b[k];
        }
        to_fixed(p, player.pos);
        player.pitch = (int16_t)(ang_a[0] + (((int16_t)(ang_b[0] - ang_a[0]) * f) >> 8));
        player.yaw = (uint16_t)(ang_a[1] + (((int16_t)(ang_b[1] - ang_a[1]) * f) >> 8));
    }
}

GCODE void pd_eye(fixed_t eye[3])
{
    eye[0] = player.pos[0];
    eye[1] = player.pos[1];
    eye[2] = player.pos[2] + FX(viewheight);
}

/* the view weapon (MDW_) and its frame, -1: none */
GCODE int pd_gun(int *frame)
{
    *frame = pd_wframe;
    if (pd_weapon == MDW_AXE && pd_wframe > 4)
        *frame = pd_wframe - 4;     /* the second swing: the first one's frames */
    return ps.invis_finished > level_time ? -1 : pd_weapon;
}

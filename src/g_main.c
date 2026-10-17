/* Game logic: entities, the monster queue and spawn cap, items, the
 * player's weapons and damage, particles, and the per-frame scene list.
 * Runs on the master SH-2, from cartridge ROM (GCODE). Rules follow id's
 * QuakeC (weapons.qc, items.qc, combat.qc) for the parts used here. */
#include "g_local.h"
#include "r_local.h"
#include "levels.h"

ent_t ents[MAX_ENTS];
pstate_t ps;
particle_t particles[MAX_PARTICLES];
scene_t scene;
uint32_t level_time;
const lent_t *lents;
int nlents;
int g_level_done;                    /* the exit was touched: intermission */
static uint32_t part_until;          /* no particle lives past this tick */
uint32_t g_teleports;                /* monsters teleported in so far (for demo captions) */
uint32_t g_last_tele_cls;
static int exit_rec = -1;            /* the trigger_changelevel record */
uint32_t g_level_ticks;

static const lmodel_t *models[MDL_NUM];
static uint8_t qstate[MAX_LENTS];    /* per level record: 0 queued, 1 spawned, 2 dead */
static int16_t qleaf[MAX_LENTS];     /* ... and the leaf of its spot */
static int inter_rec = -1;           /* the info_intermission the camera goes to */
int g_request;                       /* for the main loop: REQ_* (levels.h) */
static pstate_t ps_start;            /* the player as the level began (a death restarts it) */

/* static models (torches, flames): drawn, never moved */
#define MAX_STATICS 64
static struct { uint8_t model, frame, light, pad; int16_t leaf; uint16_t yaw; fixed_t org[3]; } statics[MAX_STATICS];
static int nstatics;
static uint32_t rnd32 = 0x12345678;

enum { Q_QUEUED, Q_SPAWNED, Q_DEAD };

static const uint8_t cap_values[] = { 0, 1, 2, 3, 4, 6, 8, 255 };

GCODE int g_monster_cap(void)
{
    return cap_values[cfg.monsters];
}

/* ------------------------------------------------------------ helpers */

GCODE int g_rand(void)
{
    rnd32 = rnd32 * 1103515245u + 12345u;
    return (rnd32 >> 16) & 0x7FFF;
}

GCODE fixed_t g_frand(void)
{
    return g_rand() << 1;
}

GCODE fixed_t g_crand(void)
{
    return (g_rand() << 2) - 0x10000;
}

const lmodel_t *g_model(int i)
{
    return models[i];               /* NULL: not in this level */
}

static int is_monster_class(int c)
{
    return c >= CL_ARMY && c <= CL_ZOMBIE;
}

/* the classes the spawner can bring in so far */
static int can_spawn(int c)
{
    return (c == CL_ARMY || c == CL_DOG) && models[c == CL_ARMY ? MDL_SOLDIER : MDL_DOG];
}

static uint32_t isqrt_u(uint32_t x)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* distance in whole units */
GCODE int vlen_units(const fixed_t a[3], const fixed_t b[3])
{
    int dx = UNITS(a[0] - b[0]), dy = UNITS(a[1] - b[1]), dz = UNITS(a[2] - b[2]);
    return isqrt_u((uint32_t)(dx * dx + dy * dy + dz * dz));
}

/* atan2 as a 16-bit binary angle (0 = +x, 16384 = +y) */
GCODE uint16_t vectoyaw(fixed_t dx, fixed_t dy)
{
    fixed_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    fixed_t z;
    int a;
    if (ax == 0 && ay == 0)
        return 0;
    if (ax >= ay) {
        z = hw_fixdiv(ay, ax);                       /* 0..1 */
        a = (fixmul(z, 8192 << 16) + fixmul(fixmul(z, 0x10000 - z), 2847 << 16)) >> 16;
    } else {
        z = hw_fixdiv(ax, ay);
        a = 16384 - ((fixmul(z, 8192 << 16) + fixmul(fixmul(z, 0x10000 - z), 2847 << 16)) >> 16);
    }
    if (dx < 0) a = 32768 - a;
    if (dy < 0) a = -a;
    return (uint16_t)a;
}

/* normalize a vector (16.16) */
static void normalize(fixed_t v[3])
{
    int32_t a[3];
    int64_t s;
    uint32_t len;
    int k, m = 0;
    /* scale so the largest component is below 2^15 before squaring */
    for (k = 0; k < 3; k++) {
        a[k] = v[k];
        m |= a[k] < 0 ? -a[k] : a[k];
    }
    while (m >= 0x8000) {
        for (k = 0; k < 3; k++)
            a[k] >>= 1;
        m >>= 1;
    }
    s = (int64_t)a[0] * a[0] + (int64_t)a[1] * a[1] + (int64_t)a[2] * a[2];
    len = isqrt_u((uint32_t)s);                   /* same scale as a */
    if (len == 0)
        return;
    for (k = 0; k < 3; k++)
        v[k] = hw_div64((int64_t)a[k] << 16, len);
}

/* ------------------------------------------------------------ entities */

GCODE ent_t *g_spawn(void)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++)
        if (ents[i].type == ET_FREE) {
            memset(&ents[i], 0, sizeof(ents[i]));
            ents[i].queue = -1;
            return &ents[i];
        }
    return NULL;
}

GCODE void g_free(ent_t *e)
{
    e->type = ET_FREE;
}

static int overlaps_player_box(const ent_t *e, fixed_t x, fixed_t y, fixed_t z)
{
    int dx = UNITS(x - e->org[0]), dy = UNITS(y - e->org[1]), dz = UNITS(z - e->org[2]);
    return dx + 16 > e->mins[0] && dx - 16 < e->maxs[0] && dy + 16 > e->mins[1]
        && dy - 16 < e->maxs[1] && dz + 32 > e->mins[2] && dz - 24 < e->maxs[2];
}

/* would the player's box at (x, y, z) run into a monster it is not
 * already touching? */
GCODE int g_box_blocked(fixed_t x, fixed_t y, fixed_t z)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *e = &ents[i];
        if (e->type != ET_MONSTER)
            continue;
        if (overlaps_player_box(e, x, y, z) && !overlaps_player_box(e, player.pos[0], player.pos[1], player.pos[2]))
            return 1;
    }
    return 0;
}

/* the floor below p (point trace), or p if none within 256 units */
static fixed_t drop_to_floor(const fixed_t p[3])
{
    fixed_t s[3], e[3];
    trace_t tr;
    s[0] = p[0]; s[1] = p[1]; s[2] = p[2] + FX(4);
    e[0] = p[0]; e[1] = p[1]; e[2] = p[2] - FX(256);
    g_trace(0, s, e, &tr);
    if (tr.frac < 0x10000 && !tr.startsolid)
        return tr.end[2];
    return p[2];
}

static void spawn_item(int idx, const lent_t *le)
{
    ent_t *e = g_spawn();
    fixed_t p[3];
    int box = 0;
    if (!e)
        return;
    e->type = ET_ITEM;
    e->cls = le->cls;
    e->variant = le->variant;
    e->queue = idx;
    switch (le->cls) {
    case CL_I_HEALTH:
        e->model = le->variant & 1 ? MDL_B_BH10 : le->variant & 2 ? MDL_B_BH100 : MDL_B_BH25;
        box = 1;
        break;
    case CL_I_SHELLS: e->model = le->variant & 1 ? MDL_B_SHELL1 : MDL_B_SHELL0; box = 1; break;
    case CL_I_SPIKES: e->model = le->variant & 1 ? MDL_B_NAIL1 : MDL_B_NAIL0; box = 1; break;
    case CL_I_ARMOR1: e->model = MDL_ARMOR; e->skin = 0; break;
    case CL_I_ARMOR2: e->model = MDL_ARMOR; e->skin = 1; break;
    case CL_I_ARMORINV: e->model = MDL_ARMOR; e->skin = 2; break;
    case CL_W_SUPERSHOTGUN: e->model = MDL_G_SHOT; break;
    case CL_W_NAILGUN: e->model = MDL_G_NAIL; break;
    case CL_I_ARTIFACT_SUPER_DAMAGE: e->model = MDL_QUADDAMA; break;
    case CL_I_ARTIFACT_ENVIROSUIT: e->model = MDL_SUIT; break;
    default:
        g_free(e);
        return;
    }
    e->org[0] = FX(le->org[0]);
    e->org[1] = FX(le->org[1]);
    e->org[2] = FX(le->org[2]);
    /* boxes: origin at a bottom corner, 32x32x56; models: centred */
    if (box) {
        e->mins[0] = e->mins[1] = e->mins[2] = 0;
        e->maxs[0] = e->maxs[1] = 32; e->maxs[2] = 56;
        p[0] = e->org[0] + FX(16); p[1] = e->org[1] + FX(16); p[2] = e->org[2];
    } else {
        e->mins[0] = e->mins[1] = -16; e->mins[2] = 0;
        e->maxs[0] = e->maxs[1] = 16; e->maxs[2] = 56;
        p[0] = e->org[0]; p[1] = e->org[1]; p[2] = e->org[2];
    }
    e->org[2] = drop_to_floor(p);
    g_relink(e);
}

static ent_t *spawn_monster(int q, int teleport)
{
    const lent_t *le = &lents[q];
    ent_t *e = g_spawn();
    if (!e)
        return NULL;
    e->queue = q;
    e->org[0] = FX(le->org[0]);
    e->org[1] = FX(le->org[1]);
    e->org[2] = FX(le->org[2]);
    e->yaw = e->ideal_yaw = ANG(le->angle);
    e->seen = level_time;
    m_spawn(e, le->cls, teleport);
    qstate[q] = Q_SPAWNED;
    if (teleport) {
        fixed_t c[3];
        c[0] = e->org[0]; c[1] = e->org[1]; c[2] = e->org[2];
        g_teleport_fx(c);
        snd_start(SND_R_TELE1 + (g_rand() % 5), c, 255);
        g_teleports++;
        g_last_tele_cls = le->cls;
    }
    return e;
}

static int monsters_alive(void)
{
    int i, n = 0;
    for (i = 0; i < MAX_ENTS; i++)
        if (ents[i].type == ET_MONSTER)
            n++;
    return n;
}

/* the spawn cap: while there is a free slot, the nearest queued monster
 * whose spot is potentially visible from the player teleports in */
static void spawner(void)
{
    int cap = g_monster_cap(), alive, i;
    if (ps.dead)
        return;
    if (cap == 255) {
        for (i = 0; i < nlents; i++)
            if (qstate[i] == Q_QUEUED && can_spawn(lents[i].cls))
                spawn_monster(i, 0);
        return;
    }
    alive = monsters_alive();
    while (alive < cap) {
        int best = -1, bestd = 1500, k;
        for (i = 0; i < nlents; i++) {
            fixed_t spot[3];
            int d, blocked = 0;
            if (qstate[i] != Q_QUEUED || !can_spawn(lents[i].cls))
                continue;
            spot[0] = FX(lents[i].org[0]);
            spot[1] = FX(lents[i].org[1]);
            spot[2] = FX(lents[i].org[2]);
            d = vlen_units(spot, player.pos);
            if (d < 96 || d >= bestd)
                continue;
            if (!LEAF_VISIBLE(qleaf[i]))
                continue;
            for (k = 0; k < MAX_ENTS && !blocked; k++)
                if (ents[k].type == ET_MONSTER) {
                    int dx = UNITS(ents[k].org[0] - spot[0]), dy = UNITS(ents[k].org[1] - spot[1]);
                    if (dx > -64 && dx < 64 && dy > -64 && dy < 64)
                        blocked = 1;
                }
            if (blocked)
                continue;
            best = i;
            bestd = d;
        }
        if (best < 0)
            break;
        spawn_monster(best, 1);
        alive++;
    }
}

/* a new game: the player as Quake's SetNewParms makes him */
GCODE void g_new_game(void)
{
    memset(&ps, 0, sizeof(ps));
    ps.health = 100;
    ps.shells = 25;
    ps.weapons = IT_SHOTGUN;
    ps.weapon = IT_SHOTGUN;
    ps_start = ps;
}

/* on to the next level: SetChangeParms keeps the weapons, ammunition and
 * armour; health back to 50..100; no keys or powerups */
GCODE void g_change_parms(void)
{
    pstate_t p;
    memset(&p, 0, sizeof(p));
    if (ps.dead || ps.health <= 0) {
        g_new_game();
        return;
    }
    p.health = ps.health > 100 ? 100 : ps.health < 50 ? 50 : ps.health;
    p.armor = ps.armor;
    p.armortype = ps.armortype;
    p.shells = ps.shells;
    p.nails = ps.nails;
    p.weapons = ps.weapons;
    p.weapon = ps.weapon;
    ps = p;
    ps_start = ps;
}

static void add_static(int model, int frame, const lent_t *le)
{
    fixed_t p[3];
    if (nstatics >= MAX_STATICS || !models[model])
        return;
    p[0] = FX(le->org[0]);
    p[1] = FX(le->org[1]);
    p[2] = FX(le->org[2]);
    statics[nstatics].model = model;
    statics[nstatics].frame = frame;
    statics[nstatics].org[0] = p[0];
    statics[nstatics].org[1] = p[1];
    statics[nstatics].org[2] = p[2];
    statics[nstatics].yaw = ANG(le->angle);
    statics[nstatics].leaf = r_pointleaf(p);
    statics[nstatics].light = 0;
    nstatics++;
}

/* the level's things, its monsters queued; the player as the level began */
GCODE void g_init(void)
{
    int i;
    lents = (const lent_t *)(lvl.base + lvl.hdr->lump[LVL_ENTS].ofs);
    nlents = lvl.hdr->lump[LVL_ENTS].count;
    if (nlents > MAX_LENTS) nlents = MAX_LENTS;
    {
        const uint32_t *mt = (const uint32_t *)(lvl.base + lvl.hdr->lump[LVL_MODELS].ofs);
        for (i = 0; i < MDL_NUM; i++)
            models[i] = mt[i] ? (const lmodel_t *)(lvl.base + mt[i]) : NULL;
    }
    memset(ents, 0, sizeof(ents));
    memset(particles, 0, sizeof(particles));
    memset(qstate, 0, sizeof(qstate));
    ps = ps_start;
    mv_init();
    level_time = 1;
    part_until = 0;
    exit_rec = -1;
    inter_rec = -1;
    g_level_done = 0;
    g_level_ticks = 0;
    nstatics = 0;
    for (i = 0; i < nlents; i++) {
        const lent_t *le = &lents[i];
        int c = le->cls;
        fixed_t spot[3];
        spot[0] = FX(le->org[0]);
        spot[1] = FX(le->org[1]);
        spot[2] = FX(le->org[2]);
        qleaf[i] = r_pointleaf(spot);
        switch (c) {
        case CL_T_CHANGELEVEL: exit_rec = i; break;
        case CL_INTERMISSION: if (inter_rec < 0) inter_rec = i; break;
        case CL_TELEPORT_DESTINATION: break;
        case CL_L_TORCH_SMALL_WALLTORCH:
            add_static(MDL_FLAME, 0, le);
            snd_static(SND_FIRE1, spot, 128);
            break;
        case CL_L_FLAME_LARGE_YELLOW:
        case CL_L_FLAME_SMALL_YELLOW:
            add_static(MDL_FLAME2, c == CL_L_FLAME_LARGE_YELLOW, le);
            snd_static(SND_FIRE1, spot, 128);
            break;
        case CL_AMB_COMP_HUM: snd_static(SND_COMP1, spot, 255); break;
        case CL_AMB_DRONE: snd_static(SND_DRONE6, spot, 128); break;
        case CL_AMB_DRIP: snd_static(SND_DRIP1, spot, 128); break;
        case CL_AMB_SWAMP1: snd_static(SND_SWAMP1, spot, 128); break;
        case CL_AMB_SWAMP2: snd_static(SND_SWAMP2, spot, 128); break;
        case CL_L_FLUORO: snd_static(SND_FL_HUM1, spot, 128); break;
        case CL_L_FLUOROSPARK: snd_static(SND_BUZZ1, spot, 128); break;
        default:
            if (is_monster_class(c))
                ps.total_monsters++;
            else
                spawn_item(i, le);
        }
    }
}

/* the intermission camera: the level's info_intermission (or where the
 * player stands) */
GCODE int g_intermission_view(fixed_t eye[3], int *yaw, int *pitch)
{
    const lent_t *l;
    if (inter_rec < 0)
        return 0;
    l = &lents[inter_rec];
    eye[0] = FX(l->org[0]);
    eye[1] = FX(l->org[1]);
    eye[2] = FX(l->org[2]);
    *yaw = ANG(l->angle);
    *pitch = ANG(l->half[0]);
    return 1;
}

/* ------------------------------------------------------------ effects */

GCODE void g_particles(const fixed_t org[3], const fixed_t dir[3], int color, int count)
{
    int i, k, leaf = r_pointleaf(org);
    for (i = 0; i < MAX_PARTICLES && count > 0; i++) {
        particle_t *p = &particles[i];
        if ((int16_t)(p->die - (uint16_t)level_time) > 0)
            continue;
        for (k = 0; k < 3; k++) {
            p->org[k] = org[k] + ((g_rand() & 15) - 8) * 0x10000;
            p->vel[k] = dir ? dir[k] * 15 : 0;
        }
        p->color = (color & ~7) + (g_rand() & 7);
        p->type = 1;
        p->leaf = leaf;
        if (part_until < level_time + 30) part_until = level_time + 30;
        p->die = (uint16_t)(level_time + 6 * (g_rand() % 5) + 3);
        count--;
    }
}

/* Quake's teleport splash, thinned from 896 particles to 100 */
GCODE void g_teleport_fx(const fixed_t org[3])
{
    int i, j, k, n = 0, leaf = r_pointleaf(org);
    for (i = -16; i <= 16; i += 8)
        for (j = -16; j <= 16; j += 8)
            for (k = -24; k <= 32; k += 16) {
                particle_t *p;
                fixed_t d[3];
                int vel;
                while (n < MAX_PARTICLES && (int16_t)(particles[n].die - (uint16_t)level_time) > 0)
                    n++;
                if (n >= MAX_PARTICLES)
                    return;
                p = &particles[n];
                d[0] = FX(j * 8); d[1] = FX(i * 8); d[2] = FX(k * 8);
                p->org[0] = org[0] + FX(i + (g_rand() & 3));
                p->org[1] = org[1] + FX(j + (g_rand() & 3));
                p->org[2] = org[2] + FX(k + (g_rand() & 3));
                normalize(d);
                vel = 50 + (g_rand() & 63);
                p->vel[0] = d[0] * vel;
                p->vel[1] = d[1] * vel;
                p->vel[2] = d[2] * vel;
                p->color = 7 + (g_rand() & 7);
                p->type = 1;
                p->leaf = leaf;
                if (part_until < level_time + 33) part_until = level_time + 33;
                p->die = (uint16_t)(level_time + 12 + (g_rand() % 21));
            }
}

static void run_particles(void)
{
    int i;
    if (level_time > part_until)
        return;
    for (i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particles[i];
        if ((int16_t)(p->die - (uint16_t)level_time) <= 0)
            continue;
        p->org[0] += p->vel[0] / 60;
        p->org[1] += p->vel[1] / 60;
        p->org[2] += p->vel[2] / 60;
        if ((p->type & 3) == 1)
            p->vel[2] -= FX(800) / 60 / 20;          /* slow gravity */
        else if ((p->type & 3) == 2)
            p->vel[2] -= FX(800) / 60;
    }
}

/* ------------------------------------------------------------ damage */

GCODE void g_damage_player(int dmg, const fixed_t *from)
{
    int save, take;
    (void)from;
    if (ps.dead)
        return;
    if (cfg.god)
        dmg = (dmg + 3) >> 2;            /* the demo bot: quarter damage */
    save = (ps.armortype * dmg + 99) / 100;
    if (save > ps.armor)
        save = ps.armor;
    ps.armor -= save;
    if (ps.armor == 0)
        ps.armortype = 0;
    take = dmg - save;
    ps.health -= take;
    if (ps.health <= 0) {
        ps.health = 0;
        ps.dead = 1;
        ps.died = level_time;
        snd_start(SND_PLAYER_DEATH1, NULL, 255);
        return;
    }
    if (level_time >= ps.pain_finished) {
        snd_start(SND_PLAYER_PAIN1 + (g_rand() % 3), NULL, 255);
        ps.pain_finished = level_time + TICKS(0.5);
    }
}

/* damage from the world (crushing doors): no Quad Damage */
GCODE void g_hurt(ent_t *e, int dmg)
{
    if (e->type != ET_MONSTER || e->health <= 0)
        return;
    e->health -= dmg;
    if (e->health <= 0) {
        m_die(e);
        ps.kills++;
        if (e->queue >= 0)
            qstate[e->queue] = Q_DEAD;
        return;
    }
    m_pain(e, dmg);
}

GCODE void g_damage(ent_t *e, int dmg)
{
    if (e->type != ET_MONSTER || e->health <= 0)
        return;
    if (ps.quad_finished > level_time)
        dmg *= 4;
    e->health -= dmg;
    if (e->health <= 0) {
        m_die(e);
        ps.kills++;
        if (e->queue >= 0)
            qstate[e->queue] = Q_DEAD;
        return;
    }
    m_pain(e, dmg);
}

/* ------------------------------------------------------------ the player */

#define PLAYER_EYE 22

static void eye_pos(fixed_t e[3])
{
    e[0] = player.pos[0];
    e[1] = player.pos[1];
    e[2] = player.pos[2] + FX(PLAYER_EYE);
}

static void view_vectors(fixed_t fwd[3], fixed_t right[3], fixed_t up[3])
{
    fixed_t sy = fsin(player.yaw), cy = fcos(player.yaw);
    fixed_t sp = fsin(player.pitch), cp = fcos(player.pitch);
    fwd[0] = fixmul(cy, cp); fwd[1] = fixmul(sy, cp); fwd[2] = -sp;
    right[0] = sy; right[1] = -cy; right[2] = 0;
    up[0] = fixmul(cy, sp); up[1] = fixmul(sy, sp); up[2] = cp;
}

/* PF_aim: Quake's vertical auto-aim. If a monster is within about 22
 * degrees of the crosshair and in sight, the shot is tilted up or down to
 * its centre (the horizontal aim is the player's own) */
static void aim(const fixed_t src[3], fixed_t fwd[3])
{
    int i, k, best = -1;
    fixed_t bestdot = 0xEE14;                   /* sv_aim 0.93 */
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *e = &ents[i];
        fixed_t c[3], d[3], dt;
        trace_t tr;
        if (e->type != ET_MONSTER || e->health <= 0)
            continue;
        for (k = 0; k < 3; k++) {
            c[k] = e->org[k] + FX(e->mins[k] + e->maxs[k]) / 2;
            d[k] = c[k] - src[k];
        }
        if (vlen_units(c, src) > 2048)
            continue;
        normalize(d);
        dt = dot16(d, fwd);
        if (dt < bestdot)
            continue;
        g_trace(0, src, c, &tr);
        if (tr.frac < 0x10000)
            continue;
        bestdot = dt;
        best = i;
    }
    if (best >= 0) {
        const ent_t *e = &ents[best];
        fixed_t d[3], dist;
        for (k = 0; k < 3; k++)
            d[k] = e->org[k] - player.pos[k];
        dist = dot16(d, fwd);
        fwd[0] = fixmul(fwd[0], dist);
        fwd[1] = fixmul(fwd[1], dist);
        fwd[2] = d[2];
        normalize(fwd);
    }
}

/* would a shot now (straight down the auto-aimed line) hit this monster
 * before a wall? (the demo bot only fires when it would) */
GCODE int g_shot_would_hit(const ent_t *e)
{
    fixed_t src[3], fwd[3], right[3], up[3], end[3], mn[3], mx[3], t;
    trace_t tr;
    int k;
    eye_pos(src);
    view_vectors(fwd, right, up);
    aim(src, fwd);
    for (k = 0; k < 3; k++) {
        end[k] = src[k] + fwd[k] * 2048;
        mn[k] = e->org[k] + FX(e->mins[k]);
        mx[k] = e->org[k] + FX(e->maxs[k]);
    }
    g_trace(0, src, end, &tr);
    return g_ray_box(src, fwd, tr.frac * 2048, mn, mx, &t);
}

/* FireBullets: shotgun pellets against the world and the monsters */
static void fire_bullets(int count, fixed_t sx, fixed_t sy_)
{
    fixed_t src[3], fwd[3], right[3], up[3];
    int16_t hits[MAX_ENTS];
    int i, k, n;
    memset(hits, 0, sizeof(hits));
    eye_pos(src);
    view_vectors(fwd, right, up);
    aim(src, fwd);
    for (n = 0; n < count; n++) {
        fixed_t d[3], end[3], wd, best;
        ent_t *hit = NULL;
        trace_t tr;
        fixed_t cr = fixmul(g_crand(), sx), cu = fixmul(g_crand(), sy_);
        for (k = 0; k < 3; k++)
            d[k] = fwd[k] + fixmul(cr, right[k]) + fixmul(cu, up[k]);
        normalize(d);
        for (k = 0; k < 3; k++)
            end[k] = src[k] + d[k] * 2048;
        g_trace(0, src, end, &tr);
        wd = tr.frac * 2048;                        /* units, 16.16 */
        best = wd;
        mv_shoot_triggers(src, d, wd);              /* shootable switches */
        for (i = 0; i < MAX_ENTS; i++) {
            ent_t *e = &ents[i];
            fixed_t mn[3], mx[3], t;
            if (e->type != ET_MONSTER || e->health <= 0)
                continue;
            for (k = 0; k < 3; k++) {
                mn[k] = e->org[k] + FX(e->mins[k]);
                mx[k] = e->org[k] + FX(e->maxs[k]);
            }
            if (g_ray_box(src, d, best, mn, mx, &t) && t < best) {
                best = t;
                hit = e;
            }
        }
        {
            fixed_t at[3];
            for (k = 0; k < 3; k++)
                at[k] = src[k] + fixmul(d[k], best - FX(4));
            if (hit) {
                hits[hit - ents] += 4;
                g_particles(at, NULL, 73, 6);           /* blood */
            } else if (tr.frac < 0x10000) {
                g_particles(at, NULL, 0, 4);            /* puff */
                if (tr.mover >= 0)
                    mv_damage(tr.mover, 4);             /* secret doors open when shot */
            }
        }
    }
    for (i = 0; i < MAX_ENTS; i++)
        if (hits[i])
            g_damage(&ents[i], hits[i]);
}

static void player_attack(void)
{
    int quad = ps.quad_finished > level_time;
    if (ps.weapon == IT_SUPER_SHOTGUN && ps.shells < 2)
        ps.weapon = IT_SHOTGUN;
    if (ps.shells < 1)
        return;
    if (ps.weapon == IT_SUPER_SHOTGUN) {
        ps.shells -= 2;
        snd_start(SND_SHOTGN2, NULL, 255);
        fire_bullets(14, 0x23D7, 0x147A);          /* spread 0.14, 0.08 */
        ps.attack_finished = level_time + TICKS(0.7);
    } else {
        ps.shells -= 1;
        snd_start(SND_GUNCOCK, NULL, 255);
        fire_bullets(6, 0xA3D, 0xA3D);             /* spread 0.04 */
        ps.attack_finished = level_time + TICKS(0.5);
    }
    if (quad)
        snd_start(SND_DAMAGE3, NULL, 255);
    ps.weaponframe = 1;
    ps.weapon_tick = level_time;
}

static void message(const char *m)
{
    ps.msg = m;
    ps.msg_until = level_time + TICKS(2);
}

/* item pickups (items.qc) */
static void touch_items(void)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        int taken = 1, snd = SND_LOCK4;
        int dx, dy, dz;
        if (e->type != ET_ITEM)
            continue;
        dx = UNITS(player.pos[0] - e->org[0]);
        dy = UNITS(player.pos[1] - e->org[1]);
        dz = UNITS(player.pos[2] - e->org[2]);
        /* player box (-16 -16 -24)..(16 16 32) against the item's box */
        if (dx + 16 <= e->mins[0] || dx - 16 >= e->maxs[0] || dy + 16 <= e->mins[1]
            || dy - 16 >= e->maxs[1] || dz + 32 <= e->mins[2] || dz - 24 >= e->maxs[2])
            continue;
        switch (e->cls) {
        case CL_I_HEALTH:
            if (e->variant & 2) {
                if (ps.health >= 250) { taken = 0; break; }
                ps.health += 100;
                if (ps.health > 250) ps.health = 250;
                snd = SND_R_ITEM2;
                message("You receive 100 health");
            } else {
                int add = e->variant & 1 ? 15 : 25;
                if (ps.health >= 100) { taken = 0; break; }
                ps.health += add;
                if (ps.health > 100) ps.health = 100;
                snd = e->variant & 1 ? SND_R_ITEM1 : SND_HEALTH1;
                message(add == 15 ? "You receive 15 health" : "You receive 25 health");
            }
            break;
        case CL_I_SHELLS:
            if (ps.shells >= 100) { taken = 0; break; }
            ps.shells += e->variant & 1 ? 40 : 20;
            if (ps.shells > 100) ps.shells = 100;
            message("You got the shells");
            break;
        case CL_I_SPIKES:
            if (ps.nails >= 200) { taken = 0; break; }
            ps.nails += e->variant & 1 ? 50 : 25;
            if (ps.nails > 200) ps.nails = 200;
            message("You got the nails");
            break;
        case CL_I_ARMOR1: case CL_I_ARMOR2: case CL_I_ARMORINV: {
            int type = e->cls == CL_I_ARMOR1 ? 30 : e->cls == CL_I_ARMOR2 ? 60 : 80;
            int val = e->cls == CL_I_ARMOR1 ? 100 : e->cls == CL_I_ARMOR2 ? 150 : 200;
            if (ps.armortype * ps.armor >= type * val) { taken = 0; break; }
            ps.armortype = type;
            ps.armor = val;
            snd = SND_ARMOR1;
            message("You got armor");
            break;
        }
        case CL_W_SUPERSHOTGUN:
            ps.weapons |= IT_SUPER_SHOTGUN;
            ps.weapon = IT_SUPER_SHOTGUN;
            ps.shells += 5;
            if (ps.shells > 100) ps.shells = 100;
            snd = SND_PKUP;
            message("You got the Double-barrelled Shotgun");
            break;
        case CL_W_NAILGUN:
            ps.weapons |= IT_NAILGUN;
            ps.nails += 30;
            if (ps.nails > 200) ps.nails = 200;
            snd = SND_PKUP;
            message("You got the nailgun");
            break;
        case CL_I_ARTIFACT_SUPER_DAMAGE:
            ps.quad_finished = level_time + TICKS(30);
            snd = SND_DAMAGE;
            message("You got the Quad Damage");
            break;
        case CL_I_ARTIFACT_ENVIROSUIT:
            ps.suit_finished = level_time + TICKS(30);
            snd = SND_SUIT;
            message("You got the Biosuit");
            break;
        case CL_BACKPACK:
            ps.shells += 5;
            if (ps.shells > 100) ps.shells = 100;
            message("You get 5 shells");
            break;
        default:
            taken = 0;
        }
        if (taken) {
            snd_start(snd, NULL, 255);
            ps.items_got++;
            g_free(e);
        }
    }
}

/* ------------------------------------------------------------ the tick */

/* a monster left far behind, out of sight for a while, goes back to the
 * queue (silently), so the cap's slots follow the player */
static void recall_check(ent_t *e)
{
    if (LEAF_VISIBLE(e->leaf) || vlen_units(e->org, player.pos) < 800) {
        e->seen = level_time;
        return;
    }
    if (level_time - e->seen > TICKS(6) && e->queue >= 0) {
        qstate[e->queue] = Q_QUEUED;
        g_free(e);
    }
}

static void check_exit(void)
{
    if (exit_rec >= 0) {
        const lent_t *l = &lents[exit_rec];
        int dx, dy, dz;
        dx = UNITS(player.pos[0]) - l->org[0];
        dy = UNITS(player.pos[1]) - l->org[1];
        dz = UNITS(player.pos[2]) - l->org[2];
        if (dx + 16 > -l->half[0] && dx - 16 < l->half[0] && dy + 16 > -l->half[1]
            && dy - 16 < l->half[1] && dz + 32 > -l->half[2] && dz - 24 < l->half[2]) {
            g_level_done = 1;
            g_level_ticks = level_time;
        }
    }
}

GCODE void g_tick(int buttons, int pressed)
{
    int i;
    if (g_level_done) {
        /* intermission: the world stops; B or C after two seconds goes on */
        if (++g_level_done > TICKS(2) && (pressed & (ACT_JUMP | ACT_FIRE)))
            g_request = REQ_NEXT_LEVEL;
        return;
    }
    level_time++;

    if (ps.dead) {
        /* respawn at the start after two seconds and a button */
        if (level_time - ps.died > TICKS(2) && (pressed & (ACT_JUMP | ACT_FIRE))) {
            g_request = REQ_RESTART;    /* single player: the level starts again */
            return;
        }
    } else {
        if ((pressed & ACT_WEAPON)
            || (!cfg.bind[BIND_WEAPON] && (pressed & ACT_FIRE) && (buttons & ACT_STRAFE))) {
            /* next weapon: its own button, or strafe + fire */
            if (ps.weapon == IT_SHOTGUN && (ps.weapons & IT_SUPER_SHOTGUN) && ps.shells >= 2)
                ps.weapon = IT_SUPER_SHOTGUN;
            else
                ps.weapon = IT_SHOTGUN;
        } else if ((buttons & ACT_FIRE) && !(!cfg.bind[BIND_WEAPON] && (buttons & ACT_STRAFE))
                   && level_time >= ps.attack_finished
                   && !cfg.fly) {
            player_attack();
        }
        touch_items();
        mv_touch_player();
        check_exit();
    }
    mv_tick();
    if (ps.weaponframe && level_time - ps.weapon_tick >= 6) {
        ps.weapon_tick = level_time;
        if (++ps.weaponframe > 6)
            ps.weaponframe = 0;
    }

    /* monsters think at 10 Hz, spread over the six ticks */
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        if (e->type != ET_MONSTER && e->type != ET_CORPSE)
            continue;
        m_physics(e);
        if (e->type == ET_MONSTER && level_time >= e->nextthink) {
            m_think(e);
            g_relink(e);
            mv_touch_monster(e);
        } else if (e->type == ET_CORPSE && level_time >= e->nextthink) {
            m_think(e);
            g_relink(e);
        } else if (!e->onground)
            g_relink(e);            /* falling or leaping */
        if (e->type == ET_MONSTER && ((level_time + i) & 31) == 0)
            recall_check(e);
    }
    if ((level_time % 6) == 3)
        spawner();
    run_particles();
}

/* ------------------------------------------------------------ the scene */

/* screen rectangle of a model's current frame: its bounding box turned
 * and projected; 0 if off screen. A box reaching behind the eye gets the
 * whole view. dist: nearest corner, in units */
static int model_rect(const lmodel_t *m, int frame, const fixed_t org[3], uint16_t yaw,
                      int16_t rect[4], int *dist)
{
    const uint8_t *bb = (const uint8_t *)m + m->bbox_ofs + frame * 6;
    fixed_t cy = fcos(yaw), sy = fsin(yaw);
    fixed_t zmin = 0x7FFFFFFF, xmin = 0, xmax = 0, ymin = 0, ymax = 0;
    int k, behind = 0, first = 1, x0, x1, y0, y1;
    for (k = 0; k < 8; k++) {
        fixed_t mx = m->origin[0] + m->scale[0] * bb[(k & 1) ? 3 : 0];
        fixed_t my = m->origin[1] + m->scale[1] * bb[(k & 2) ? 4 : 1];
        fixed_t mz = m->origin[2] + m->scale[2] * bb[(k & 4) ? 5 : 2];
        fixed_t w[3], x, y, z;
        int32_t inv;
        w[0] = org[0] + fixmul(cy, mx) - fixmul(sy, my) - view.org[0];
        w[1] = org[1] + fixmul(sy, mx) + fixmul(cy, my) - view.org[1];
        w[2] = org[2] + mz - view.org[2];
        z = dot16(w, view.fwd);
        if (z < zmin) zmin = z;
        if (z < FX(8)) {
            behind++;
            continue;
        }
        x = dot16(w, view.right);
        y = dot16(w, view.down);
        inv = hw_div64((int64_t)view.proj << 32, z);           /* pixels per unit */
        x = view.cx16 + fixmul(x, inv);
        y = view.cy16 + fixmul(y, inv);
        if (first || x < xmin) xmin = x;
        if (first || x > xmax) xmax = x;
        if (first || y < ymin) ymin = y;
        if (first || y > ymax) ymax = y;
        first = 0;
    }
    if (behind == 8 || zmin > view.far)
        return 0;
    *dist = zmin < 0 ? 0 : UNITS(zmin);
    if (behind) {
        rect[0] = 0; rect[1] = 0; rect[2] = view.vw; rect[3] = view.vh;
        return 1;
    }
    x0 = (xmin >> 16) - 1;
    x1 = (xmax >> 16) + 2;
    y0 = (ymin >> 16) - 1;
    y1 = (ymax >> 16) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > view.vw) x1 = view.vw;
    if (y1 > view.vh) y1 = view.vh;
    if (x0 >= x1 || y0 >= y1)
        return 0;
    rect[0] = x0; rect[1] = y0; rect[2] = x1; rect[3] = y1;
    return 1;
}

static void add_rect(const int16_t r[4])
{
    int y;
    for (y = r[1]; y < r[3]; y++) {
        zrow_t *z = &scene.zrow[y];
        if (z->x1 <= z->x0) {
            z->x0 = r[0];
            z->x1 = r[2];
        } else {
            if (r[0] < z->x0) z->x0 = r[0];
            if (r[2] > z->x1) z->x1 = r[2];
        }
    }
}

static int layout_zrows(void)
{
    int y, used = 0;
    for (y = 0; y < view.vh; y++) {
        zrow_t *z = &scene.zrow[y];
        z->ofs = used;
        if (z->x1 > z->x0)
            used += z->x1 - z->x0;
    }
    return used;
}

/* drawables and depth-buffered regions for this frame (master, before GO) */
GCODE void g_build_scene(void)
{
    int i, n = 0;
    scene.ndraw = 0;
    mv_render_state();
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        drawent_t *d;
        const lmodel_t *m;
        int dist;
        uint16_t yaw;
        if (e->type == ET_FREE || n >= MAX_DRAW - 1)
            continue;
        if (!LEAF_VISIBLE(e->leaf))
            continue;
        m = models[e->model];
        if (!m)
            continue;
        yaw = (m->flags & MF_ROTATE) ? (uint16_t)(level_time * 455 / 2) : e->yaw;
        d = &scene.draw[n];
        if (!model_rect(m, e->frame, e->org, yaw, d->rect, &dist))
            continue;
        d->mdl = m;
        d->frame = e->frame;
        d->skin = e->skin;
        d->org[0] = e->org[0];
        d->org[1] = e->org[1];
        d->org[2] = e->org[2];
        d->yaw = yaw;
        d->pitch = 0;
        if (e->lightdirty) {
            fixed_t c[3];
            c[0] = e->org[0];
            c[1] = e->org[1];
            c[2] = e->org[2] + FX(8);
            e->light = g_light_point(c);
            e->lightdirty = 0;
        }
        d->light = e->light;
        d->mode = DM_DEPTH;
        d->dist = dist;
        n++;
    }
    /* torches and flames */
    for (i = 0; i < nstatics && n < MAX_DRAW - 1; i++) {
        drawent_t *d = &scene.draw[n];
        const lmodel_t *m = models[statics[i].model];
        int dist;
        if (!LEAF_VISIBLE(statics[i].leaf))
            continue;
        if (!model_rect(m, statics[i].frame, statics[i].org, statics[i].yaw, d->rect, &dist))
            continue;
        d->mdl = m;
        d->frame = statics[i].frame;
        d->skin = 0;
        d->org[0] = statics[i].org[0];
        d->org[1] = statics[i].org[1];
        d->org[2] = statics[i].org[2];
        d->yaw = statics[i].yaw;
        d->pitch = 0;
        d->light = 0;
        d->mode = DM_DEPTH;
        d->dist = dist;
        n++;
    }
    /* depth for as many as fit; the biggest (nearest) on screen are drawn
       back to front without it */
    for (;;) {
        int big = -1, area = 0;
        memset(scene.zrow, 0, sizeof(scene.zrow));
        for (i = 0; i < n; i++)
            if (scene.draw[i].mode == DM_DEPTH)
                add_rect(scene.draw[i].rect);
        scene.zused = layout_zrows();
        if (scene.zused <= ZBUF_MAX)
            break;
        for (i = 0; i < n; i++) {
            const int16_t *r = scene.draw[i].rect;
            int a = (r[2] - r[0]) * (r[3] - r[1]);
            if (scene.draw[i].mode == DM_DEPTH && a > area) {
                area = a;
                big = i;
            }
        }
        scene.draw[big].mode = DM_SORTED;
    }
    /* sorted models are drawn far to near */
    for (i = 1; i < n; i++) {
        drawent_t t = scene.draw[i];
        int j = i;
        while (j > 0 && scene.draw[j - 1].dist < t.dist) {
            scene.draw[j] = scene.draw[j - 1];
            j--;
        }
        scene.draw[j] = t;
    }
    /* particles: those in a potentially visible leaf; the depth test, where
       there is depth, sorts out the rest */
    scene.nparts = 0;
    for (i = 0; i < MAX_PARTICLES && level_time <= part_until; i++) {
        particle_t *p = &particles[i];
        p->type &= ~PT_VISIBLE;
        if ((int16_t)(p->die - (uint16_t)level_time) <= 0)
            continue;
        if (LEAF_VISIBLE(p->leaf)) {
            p->type |= PT_VISIBLE;
            scene.nparts++;
        }
    }
    /* the view weapon is a Mega Drive sprite (main.c, mdlink.c) */
    scene.ndraw = n;
}

/* ------------------------------------------------------------ status bar */

static char *put_int(char *p, int v)
{
    char t[8];
    int n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { t[n++] = '0' + (v - hw_div(v, 10) * 10); v = hw_div(v, 10); } while (v);
    while (n) *p++ = t[--n];
    return p;
}

static char *put_str(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

static char *put_hi(char *p, const char *s)
{
    while (*s) *p++ = (char)(*s++ | 0x80);
    return p;
}

/* the status bar line: health, armour, ammo and kills, or a pickup
 * message; returns 1 to draw it highlighted */
GCODE int g_hud_text(char *buf)
{
    char *p = buf;
    if (ps.dead) {
        put_str(p, "You died. Press B to restart")[0] = 0;
        return 1;
    }
    if (ps.msg && level_time < ps.msg_until) {
        put_str(p, ps.msg)[0] = 0;
        return 0;
    }
    /* labels in the console font's gold set, numbers in white: fits in
       40 columns with every number at three digits */
    p = put_hi(p, "HEALTH");
    p = put_int(p, ps.health);
    p = put_hi(p, " ARMOR");
    p = put_int(p, ps.armor);
    p = put_hi(p, " SHELLS");
    p = put_int(p, ps.shells);
    p = put_hi(p, " KILLS");
    p = put_int(p, ps.kills);
    *p++ = '/';
    p = put_int(p, ps.total_monsters);
    *p = 0;
    return ps.quad_finished > level_time;
}

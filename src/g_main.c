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
scene_t scene, scene_next;     /* drawn; being prepared for the next frame */
uint32_t level_time;
const lent_t *lents;
int nlents;
int g_level_done;                    /* the exit was touched: intermission */
static uint32_t part_until;          /* no particle lives past this tick */
uint32_t g_teleports;                /* monsters teleported in so far (for demo captions) */
uint32_t g_last_tele_cls;
#define MAX_EXITS 8
static int exit_rec[MAX_EXITS];       /* the trigger_changelevel records (start has five) */
static int nexits;
uint32_t g_level_ticks;

static const lmodel_t *models[MDL_NUM];
static uint8_t qstate[MAX_LENTS];    /* per level record: 0 queued, 1 spawned, 2 dead */
static int16_t qleaf[MAX_LENTS];     /* ... and the leaf of its spot */
static int inter_rec = -1;           /* the info_intermission the camera goes to */
int g_request;                       /* for the main loop: REQ_* (levels.h) */
static pstate_t ps_start;            /* the player as the level began (a death restarts it) */

/* static models (torches, flames): drawn, never moved */
#define MAX_STATICS 64
static struct { uint8_t model, frame; int16_t leaf; uint16_t yaw; int16_t org[3]; } statics[MAX_STATICS];
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
    return (unsigned)i < MDL_NUM ? models[i] : NULL;    /* NULL: not in this level */
}

static int is_monster_class(int c)
{
    return c >= CL_ARMY && c <= CL_ZOMBIE;
}

/* the classes the spawner brings in (their models are in the level) */
static int can_spawn(int c)
{
    static const uint8_t mdl[] = { MDL_SOLDIER, MDL_DOG, MDL_OGRE, MDL_KNIGHT, MDL_DEMON, MDL_WIZARD, MDL_ZOMBIE };
    return is_monster_class(c) && models[mdl[c - CL_ARMY]];
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

/* would the player's box at (x, y, z) run into a monster (or a barrel) it
 * is not already touching? */
GCODE int g_box_blocked(fixed_t x, fixed_t y, fixed_t z)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *e = &ents[i];
        if ((e->type != ET_MONSTER && e->type != ET_BOX) || (e->flags & EF_NOTSOLID))
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
    e->owner = le->target;          /* items fire their target when taken */
    switch (le->cls) {
    case CL_I_HEALTH:
        e->model = le->variant & 1 ? MDL_B_BH10 : le->variant & 2 ? MDL_B_BH100 : MDL_B_BH25;
        box = 1;
        break;
    case CL_I_SHELLS: e->model = le->variant & 1 ? MDL_B_SHELL1 : MDL_B_SHELL0; box = 1; break;
    case CL_I_SPIKES: e->model = le->variant & 1 ? MDL_B_NAIL1 : MDL_B_NAIL0; box = 1; break;
    case CL_I_ROCKETS: e->model = le->variant & 1 ? MDL_B_ROCK1 : MDL_B_ROCK0; box = 1; break;
    case CL_W_GRENADELAUNCHER: e->model = MDL_G_ROCK; break;
    case CL_I_ARTIFACT_INVISIBILITY: e->model = MDL_INVISIBL; break;
    case CL_I_KEY1: e->model = MDL_W_S_KEY; break;
    case CL_I_KEY2: e->model = MDL_W_G_KEY; break;
    case CL_MISC_EXPLOBOX:
        /* a barrel: solid, 20 points set it off */
        e->type = ET_BOX;
        e->model = MDL_B_EXPLOB;
        e->health = 20;
        e->org[0] = FX(le->org[0]);
        e->org[1] = FX(le->org[1]);
        e->org[2] = FX(le->org[2] + 2);
        e->maxs[0] = e->maxs[1] = 32; e->maxs[2] = 64;
        p[0] = e->org[0] + FX(16); p[1] = e->org[1] + FX(16); p[2] = e->org[2];
        e->org[2] = drop_to_floor(p);
        g_relink(e);
        return;
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
    if (!models[e->model])
        g_free(e);
    else
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
    ps.weapons = IT_AXE | IT_SHOTGUN;
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
    p.rockets = ps.rockets;
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
    statics[nstatics].org[0] = le->org[0];
    statics[nstatics].org[1] = le->org[1];
    statics[nstatics].org[2] = le->org[2];
    statics[nstatics].yaw = ANG(le->angle);
    statics[nstatics].leaf = r_pointleaf(p);
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
    nexits = 0;
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
        case CL_T_CHANGELEVEL: if (nexits < MAX_EXITS) exit_rec[nexits++] = i; break;
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
        case CL_TRAP_SPIKESHOOTER: break;
        default:
            if (g_mode == GM_DEMO)
                break;                  /* the demo brings its own things */
            if (c == CL_ZOMBIE && le->variant)
                qstate[i] = Q_DEAD;     /* a crucified zombie: scenery, left out */
            else if (is_monster_class(c))
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

/* a free particle at org (its leaf worked out), or NULL */
GCODE particle_t *g_new_particle(const fixed_t org[3])
{
    static int next;
    int i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        particle_t *p = &particles[next];
        next = next + 1 < MAX_PARTICLES ? next + 1 : 0;
        if ((int16_t)(p->die - (uint16_t)level_time) > 0)
            continue;
        p->org[0] = org[0];
        p->org[1] = org[1];
        p->org[2] = org[2];
        p->leaf = r_pointleaf(org);
        return p;
    }
    return NULL;
}

GCODE void g_particle_life(particle_t *p, int ticks)
{
    p->die = (uint16_t)(level_time + ticks);
    if (part_until < level_time + ticks)
        part_until = level_time + ticks;
}

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

/* a body that has lain for a moment teleports away, in the splash the
 * monsters arrive with: fewer things to draw and to think about. Silently:
 * the sound drew attention to it (the random number it took is still
 * taken, so the game's later choices stay as they were) */
GCODE void g_vanish(ent_t *e)
{
    fixed_t c[3];
    c[0] = e->org[0]; c[1] = e->org[1]; c[2] = e->org[2];
    g_free(e);
    g_teleport_fx(c);
    (void)g_rand();
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
                g_normalize(d);
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

/* T_Damage on the player: armour takes its share; a red flash */
GCODE void g_damage_player(int dmg, const fixed_t *from)
{
    int save, take;
    (void)from;
    if (ps.dead || dmg <= 0)
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
    g_flash(FLASH_DAMAGE, 3 * ((save + take) < 10 ? 10 : save + take));
    if (ps.health <= 0) {
        ps.health = 0;
        ps.dead = 1;
        ps.died = level_time;
        ps.weaponframe = 0;
        if (ps.health < -40)
            snd_start(SND_PLAYER_UDEATH, NULL, 255);
        else
            snd_start(ps.waterlevel >= 3 ? SND_PLAYER_GASP2 : SND_PLAYER_DEATH1 + (g_rand() & 1), NULL, 255);
        return;
    }
    if (level_time >= ps.pain_finished) {
        snd_start(SND_PLAYER_PAIN1 + (g_rand() % 3), NULL, 255);
        ps.pain_finished = level_time + TICKS(0.5);
    }
}

static void killed(ent_t *e)
{
    ps.kills++;
    if (e->queue >= 0)
        qstate[e->queue] = Q_DEAD;
    m_die(e);
}

/* damage from the world, traps and other monsters: no Quad Damage */
GCODE void g_hurt(ent_t *e, int dmg)
{
    if (e->type == ET_BOX) {
        g_box_damage(e, dmg);
        return;
    }
    if (e->type != ET_MONSTER || e->health <= 0 || (e->flags & EF_NOTSOLID))
        return;
    e->health -= dmg;
    if (e->health <= 0) {
        killed(e);
        return;
    }
    m_pain(e, dmg);
}

/* damage the player did */
GCODE void g_damage(ent_t *e, int dmg)
{
    if (ps.quad_finished > level_time)
        dmg *= 4;
    g_hurt(e, dmg);
}

/* ------------------------------------------------------------ the player */

/* the palette flashes of damage and bonuses (Quake's cshifts): damage
 * adds up to 150, a bonus is 50; they fade at 150 and 100 a second */
GCODE void g_flash(int type, int amount)
{
    if (type == FLASH_DAMAGE) {
        amount += ps.dmg_flash;
        ps.dmg_flash = amount > 150 ? 150 : amount;
    } else
        ps.bonus_flash = 50;
}

GCODE void g_flash_decay(void)
{
    int d = (level_time & 1) ? 3 : 2, b = (level_time % 3) ? 2 : 1;
    ps.dmg_flash = ps.dmg_flash > d ? ps.dmg_flash - d : 0;
    ps.bonus_flash = ps.bonus_flash > b ? ps.bonus_flash - b : 0;
}

/* the status line has one line of 40 columns: a message of several lines
   (the start map's episode signs) shows its lines in turn, 1.5 s each */
#define MSG_LINE_TICKS (TICKS(3) / 2)
#define HUD_COLS 40

static int msg_lines(const char *m)
{
    int n = 0, len = 0;
    for (;; m++) {
        if (*m == '\n' || !*m) {
            if (len) n++;
            len = 0;
            if (!*m) break;
        } else
            len++;
    }
    return n;
}

GCODE void g_set_message(const char *m, uint32_t ticks)
{
    uint32_t t;
    /* the same message again while it shows (a trigger stood in): carry on
       through its lines rather than starting over */
    if (ps.msg == m && level_time < ps.msg_until) return;
    ps.msg = m;
    ps.msg_t0 = level_time;
    t = (uint32_t)msg_lines(m) * MSG_LINE_TICKS;
    ps.msg_until = level_time + (t > ticks ? t : ticks);
}

static void message(const char *m)
{
    g_set_message(m, TICKS(2));
}

/* items.qc's ammo_touch: if the player held his best weapon, he gets the
   best one again */
static void ammo_taken(int was_best)
{
    if (was_best && ps.weapon != g_best_weapon()) {
        ps.weapon = g_best_weapon();
        ps.weaponframe = 0;
    }
}

/* item pickups (items.qc) */
static void touch_items(void)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        int taken = 1, snd = SND_LOCK4, best = ps.weapon == g_best_weapon();
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
            ammo_taken(best);
            break;
        case CL_I_SPIKES:
            if (ps.nails >= 200) { taken = 0; break; }
            ps.nails += e->variant & 1 ? 50 : 25;
            if (ps.nails > 200) ps.nails = 200;
            message("You got the nails");
            ammo_taken(best);
            break;
        case CL_I_ROCKETS:
            if (ps.rockets >= 100) { taken = 0; break; }
            ps.rockets += e->variant & 1 ? 10 : 5;
            if (ps.rockets > 100) ps.rockets = 100;
            message("You got the rockets");
            ammo_taken(best);
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
            ps.weapon = IT_NAILGUN;
            ps.nails += 30;
            if (ps.nails > 200) ps.nails = 200;
            snd = SND_PKUP;
            message("You got the nailgun");
            break;
        case CL_W_GRENADELAUNCHER:
            ps.weapons |= IT_GRENADE_LAUNCHER;
            ps.weapon = IT_GRENADE_LAUNCHER;
            ps.rockets += 5;
            if (ps.rockets > 100) ps.rockets = 100;
            snd = SND_PKUP;
            message("You got the Grenade Launcher");
            break;
        case CL_I_ARTIFACT_SUPER_DAMAGE:
            ps.quad_finished = level_time + TICKS(30);
            snd = SND_DAMAGE;
            message("You got the Quad Damage");
            break;
        case CL_I_ARTIFACT_ENVIROSUIT:
            ps.suit_finished = level_time + TICKS(30);
            ps.air_finished = level_time + TICKS(12);
            snd = SND_SUIT;
            message("You got the Biosuit");
            break;
        case CL_I_ARTIFACT_INVISIBILITY:
            ps.invis_finished = level_time + TICKS(30);
            snd = SND_INV1;
            message("You got the Ring of Shadows");
            break;
        case CL_I_KEY1: case CL_I_KEY2:
            ps.items |= e->cls == CL_I_KEY1 ? IT_KEY1 : IT_KEY2;
            snd = e->variant == 1 ? SND_RUNEKEY : SND_MEDKEY;
            message(e->cls == CL_I_KEY1 ? "You got the silver key" : "You got the gold key");
            break;
        case CL_BACKPACK:
            if (e->variant) {
                ps.rockets += 2;
                if (ps.rockets > 100) ps.rockets = 100;
                message("You get 2 rockets");
            } else {
                ps.shells += 5;
                if (ps.shells > 100) ps.shells = 100;
                message("You get 5 shells");
            }
            ammo_taken(best);
            break;
        default:
            taken = 0;
        }
        if (taken) {
            int target = e->owner;
            snd_start(snd, NULL, 255);
            g_flash(FLASH_BONUS, 50);
            ps.items_got++;
            g_free(e);
            if (target > 0)
                mv_use_targets(target);
        }
    }
}

/* powerups running out (CheckPowerups) */
static void powerups(void)
{
    static const struct { uint8_t snd; const char *msg; } fade[3] = {
        { SND_DAMAGE2, "Quad Damage is wearing off" },
        { SND_SUIT2, "Air supply in Biosuit expiring" },
        { SND_INV2, "Ring of Shadows magic is fading" },
    };
    uint32_t *t[3];
    int k;
    t[0] = &ps.quad_finished;
    t[1] = &ps.suit_finished;
    t[2] = &ps.invis_finished;
    for (k = 0; k < 3; k++)
        if (*t[k] && *t[k] - level_time == TICKS(3)) {
            snd_start(fade[k].snd, NULL, 255);
            message(fade[k].msg);
        }
    if (ps.invis_finished > level_time && (level_time & 127) == 0 && (g_rand() & 1))
        snd_play(SND_INV3, NULL, 128, ATTN_IDLE);
}

/* WaterMove: drowning, and slime and lava burns */
static void water(void)
{
    fixed_t p[3];
    int c, lvl_ = 0, type = 0;
    p[0] = player.pos[0];
    p[1] = player.pos[1];
    p[2] = player.pos[2] - FX(23);
    c = g_contents(0, p);
    if (c <= CONTENTS_WATER && c >= CONTENTS_LAVA) {
        type = c;
        lvl_ = 1;
        p[2] = player.pos[2] + FX(4);
        if (g_contents(0, p) == c) {
            lvl_ = 2;
            p[2] = player.pos[2] + FX(22);
            if (g_contents(0, p) == c)
                lvl_ = 3;
        }
    }
    if (lvl_ && !ps.waterlevel)
        snd_start(type == CONTENTS_LAVA ? SND_PLAYER_LBURN1 : type == CONTENTS_SLIME ? SND_PLAYER_SLIMBRN2
                  : SND_PLAYER_INH2O, NULL, 255);
    else if (!lvl_ && ps.waterlevel)
        snd_start(SND_OUTWATER, NULL, 255);
    ps.waterlevel = lvl_;
    ps.watertype = -type;
    /* air */
    if (lvl_ < 3 || ps.suit_finished > level_time) {
        if (lvl_ < 3 && ps.air_finished && ps.air_finished < level_time + TICKS(9))
            snd_start(SND_PLAYER_GASP2, NULL, 255);
        ps.air_finished = level_time + TICKS(12);
        ps.drown_dmg = 2;
    } else if (ps.air_finished < level_time && level_time >= ps.pain_finished) {
        ps.drown_dmg += 2;
        if (ps.drown_dmg > 15) ps.drown_dmg = 10;
        snd_start(SND_PLAYER_DROWN1, NULL, 255);
        g_damage_player(ps.drown_dmg, NULL);
        ps.pain_finished = level_time + TICKS(1);
    }
    if (!lvl_ || level_time < ps.dmg_time)
        return;
    if (type == CONTENTS_LAVA) {
        ps.dmg_time = level_time + (ps.suit_finished > level_time ? TICKS(1) : TICKS(0.2));
        g_damage_player(10 * lvl_, NULL);
    } else if (type == CONTENTS_SLIME && ps.suit_finished <= level_time) {
        ps.dmg_time = level_time + TICKS(1);
        g_damage_player(4 * lvl_, NULL);
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
    int k;
    for (k = 0; k < nexits; k++) {
        const lent_t *l = &lents[exit_rec[k]];
        int dx, dy, dz;
        dx = UNITS(player.pos[0]) - l->org[0];
        dy = UNITS(player.pos[1]) - l->org[1];
        dz = UNITS(player.pos[2]) - l->org[2];
        if (dx + 16 > -l->half[0] && dx - 16 < l->half[0] && dy + 16 > -l->half[1]
            && dy - 16 < l->half[1] && dz + 32 > -l->half[2] && dz - 24 < l->half[2]) {
            g_level_done = 1;
            g_level_ticks = level_time;
            ps.weaponframe = 0;
            snd_stop_dynamic();
            return;
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
    g_flash_decay();

    if (ps.dead) {
        /* respawn at the start after two seconds and a button */
        if (level_time - ps.died > TICKS(2) && (pressed & (ACT_JUMP | ACT_FIRE))) {
            g_request = REQ_RESTART;    /* single player: the level starts again */
            return;
        }
    } else {
        g_weapon_tick(buttons, pressed);
        touch_items();
        mv_touch_player();
        check_exit();
        powerups();
        if ((level_time & 3) == 0)
            water();
    }
    mv_tick();

    /* monsters think at 10 Hz, spread over the six ticks */
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        switch (e->type) {
        case ET_MONSTER:
        case ET_CORPSE:
            m_physics(e);
            if (level_time >= e->nextthink) {
                m_think(e);
                g_relink(e);
                if (e->type == ET_MONSTER)
                    mv_touch_monster(e);
            } else if (!e->onground)
                g_relink(e);            /* falling or leaping */
            if (e->type == ET_MONSTER && ((level_time + i) & 31) == 0)
                recall_check(e);
            break;
        case ET_MISSILE:
        case ET_GIB:
        case ET_BOX:
            g_run_thing(e);
            break;
        }
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
        w[0] = org[0] + fixmul(cy, mx) - fixmul(sy, my) - view_next.org[0];
        w[1] = org[1] + fixmul(sy, mx) + fixmul(cy, my) - view_next.org[1];
        w[2] = org[2] + mz - view_next.org[2];
        z = dot16(w, view_next.fwd);
        if (z < zmin) zmin = z;
        if (z < FX(8)) {
            behind++;
            continue;
        }
        x = dot16(w, view_next.right);
        y = dot16(w, view_next.down);
        inv = hw_div64((int64_t)view_next.proj << 32, z);           /* pixels per unit */
        x = view_next.cx16 + fixmul(x, inv);
        y = view_next.cy16 + fixmul(y, inv);
        if (first || x < xmin) xmin = x;
        if (first || x > xmax) xmax = x;
        if (first || y < ymin) ymin = y;
        if (first || y > ymax) ymax = y;
        first = 0;
    }
    if (behind == 8 || zmin > view_next.far)
        return 0;
    *dist = zmin < 0 ? 0 : UNITS(zmin);
    if (behind) {
        rect[0] = 0; rect[1] = 0; rect[2] = view_next.vw; rect[3] = view_next.vh;
        return 1;
    }
    x0 = (xmin >> 16) - 1;
    x1 = (xmax >> 16) + 2;
    y0 = (ymin >> 16) - 1;
    y1 = (ymax >> 16) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > view_next.vw) x1 = view_next.vw;
    if (y1 > view_next.vh) y1 = view_next.vh;
    if (x0 >= x1 || y0 >= y1)
        return 0;
    rect[0] = x0; rect[1] = y0; rect[2] = x1; rect[3] = y1;
    return 1;
}

static void add_rect(const int16_t r[4])
{
    int y;
    for (y = r[1]; y < r[3]; y++) {
        zrow_t *z = &scene_next.zrow[y];
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
    for (y = 0; y < view_next.vh; y++) {
        zrow_t *z = &scene_next.zrow[y];
        z->ofs = used;
        if (z->x1 > z->x0)
            used += z->x1 - z->x0;
    }
    return used;
}

/* a model to draw this frame: into the list, or in place of the farthest
 * one when the list is full */
static int ndraw;

static void add_draw(const lmodel_t *m, int frame, int skin, const fixed_t org[3], uint16_t yaw,
                     uint16_t pitch, int light)
{
    drawent_t *d;
    int16_t rect[4];
    int dist, i;
    if (!model_rect(m, frame, org, yaw, rect, &dist))
        return;
    if (ndraw < MAX_DRAW) {
        d = &scene_next.draw[ndraw++];
    } else {
        d = &scene_next.draw[0];
        for (i = 1; i < MAX_DRAW; i++)
            if (scene_next.draw[i].dist > d->dist)
                d = &scene_next.draw[i];
        if (d->dist <= dist)
            return;
    }
    d->mdl = m;
    d->frame = frame;
    d->skin = skin;
    d->org[0] = org[0];
    d->org[1] = org[1];
    d->org[2] = org[2];
    d->yaw = yaw;
    d->pitch = pitch;
    d->light = (m->flags & MF_FULLBRIGHT) ? 0 : light;
    d->mode = DM_DEPTH;
    d->dist = dist;
    for (i = 0; i < 4; i++)
        d->rect[i] = rect[i];
}

/* drawables and depth-buffered regions for this frame (master, before GO) */
GCODE void g_build_scene(void)
{
    int i, n;
    scene_next.ndraw = 0;
    ndraw = 0;
    mv_render_state();
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        const lmodel_t *m;
        uint16_t yaw;
        if (e->type == ET_FREE || !LEAF_VISIBLE(e->leaf))
            continue;
        m = models[e->model];
        if (!m)
            continue;
        if (lvl.mdllod && lvl.mdllod[e->model].ofs
            && vlen_units(e->org, view_next.org) > lvl.mdllod[e->model].dist)
            m = (const lmodel_t *)(lvl.base + lvl.mdllod[e->model].ofs);   /* the simpler one, far away */
        yaw = (m->flags & MF_ROTATE) ? (uint16_t)(level_time * 455 / 2) : e->yaw;
        if (e->lightdirty) {
            fixed_t c[3];
            c[0] = e->org[0];
            c[1] = e->org[1];
            c[2] = e->org[2] + FX(8);
            e->light = g_light_point(c);
            e->lightdirty = 0;
        }
        add_draw(m, e->frame, e->skin, e->org, yaw, e->type == ET_MISSILE ? e->pitch : 0, e->light);
    }
    /* torches and flames */
    for (i = 0; i < nstatics; i++)
        if (LEAF_VISIBLE(statics[i].leaf)) {
            fixed_t o[3];
            o[0] = FX(statics[i].org[0]);
            o[1] = FX(statics[i].org[1]);
            o[2] = FX(statics[i].org[2]);
            add_draw(models[statics[i].model], statics[i].frame, 0, o, statics[i].yaw, 0, 0);
        }
    /* doors, lifts and buttons the editor made into models */
    if (lvl.mvmodels) {
        int nm = lvl.hdr->lump[LVL_MOVERS].count;
        if (nm > MAX_MOVERS) nm = MAX_MOVERS;
        for (i = 0; i < nm; i++) {
            const lmodel_t *mm;
            fixed_t c[3];
            int k;
            if (!lvl.mvmodels[i])
                continue;
            mm = (const lmodel_t *)(lvl.base + lvl.mvmodels[i]);
            for (k = 0; k < 3; k++)
                c[k] = mover_off[i][k] + mm->origin[k] + (mm->scale[k] << 7);
            if (LEAF_VISIBLE(r_pointleaf(c)))
                add_draw(mm, 0, 0, mover_off[i], 0, 0, g_light_point(c));
        }
    }
    n = ndraw;
    /* depth for as many as fit; the biggest (nearest) on screen are drawn
       back to front without it */
    for (;;) {
        int big = -1, area = 0;
        memset(scene_next.zrow, 0, sizeof(scene_next.zrow));
        for (i = 0; i < n; i++)
            if (scene_next.draw[i].mode == DM_DEPTH)
                add_rect(scene_next.draw[i].rect);
        scene_next.zused = layout_zrows();
        if (scene_next.zused <= ZBUF_MAX)
            break;
        for (i = 0; i < n; i++) {
            const int16_t *r = scene_next.draw[i].rect;
            int a = (r[2] - r[0]) * (r[3] - r[1]);
            if (scene_next.draw[i].mode == DM_DEPTH && a > area) {
                area = a;
                big = i;
            }
        }
        scene_next.draw[big].mode = DM_SORTED;
    }
    /* sorted models are drawn far to near */
    for (i = 1; i < n; i++) {
        drawent_t t = scene_next.draw[i];
        int j = i;
        while (j > 0 && scene_next.draw[j - 1].dist < t.dist) {
            scene_next.draw[j] = scene_next.draw[j - 1];
            j--;
        }
        scene_next.draw[j] = t;
    }
    /* particles: those in a potentially visible leaf; the depth test, where
       there is depth, sorts out the rest */
    scene_next.nparts = 0;
    for (i = 0; i < MAX_PARTICLES && level_time <= part_until; i++) {
        particle_t *p = &particles[i];
        p->type &= ~PT_VISIBLE;
        if ((int16_t)(p->die - (uint16_t)level_time) <= 0)
            continue;
        if (LEAF_VISIBLE(p->leaf)) {
            p->type |= PT_VISIBLE;
            scene_next.nparts++;
        }
    }
    /* the view weapon is a Mega Drive sprite (main.c, mdlink.c) */
    scene_next.ndraw = n;
}

/* the prepared scene becomes the one drawn (both CPUs idle) */
void g_commit_scene(void)
{
    int i;
    scene.ndraw = scene_next.ndraw;
    for (i = 0; i < scene_next.ndraw; i++)
        scene.draw[i] = scene_next.draw[i];
    scene.nparts = scene_next.nparts;
    scene.zused = scene_next.zused;
    memcpy(scene.zrow, scene_next.zrow, sizeof(scene.zrow));
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

/* the status bar line: health, armour, the weapon's ammunition, keys and
 * kills, or a pickup message; returns 1 to draw it highlighted */
GCODE int g_hud_text(char *buf)
{
    char *p = buf;
    if (ps.dead) {
        put_str(p, "You died. Press B to restart")[0] = 0;
        return 1;
    }
    if (ps.msg && level_time < ps.msg_until) {
        /* this moment's line (empty lines skipped), cut to the bar's width */
        const char *m = ps.msg;
        int n = msg_lines(m), k = 0, want, col = 0;
        want = n > 1 ? (int)((level_time - ps.msg_t0) / MSG_LINE_TICKS) : 0;
        if (want >= n) want = n - 1;
        for (; *m; m++) {
            if (*m == '\n') {
                if (col) k++;
                col = 0;
                continue;
            }
            if (k == want && col < HUD_COLS)
                *p++ = *m;
            col++;
            if (k > want) break;
        }
        *p = 0;
        return 0;
    }
    /* labels in the console font's gold set, numbers in white: fits in
       40 columns with every number at three digits */
    p = put_hi(p, "HEALTH");
    p = put_int(p, ps.health);
    p = put_hi(p, " ARMOR");
    p = put_int(p, ps.armor);
    switch (ps.weapon) {
    case IT_SHOTGUN: case IT_SUPER_SHOTGUN: p = put_hi(p, " SHELLS"); p = put_int(p, ps.shells); break;
    case IT_NAILGUN: p = put_hi(p, " NAILS"); p = put_int(p, ps.nails); break;
    case IT_GRENADE_LAUNCHER: p = put_hi(p, " ROCKETS"); p = put_int(p, ps.rockets); break;
    default: p = put_hi(p, " AXE"); break;
    }
    if (ps.items & IT_KEY1) { *p++ = ' '; *p++ = (char)('S' | 0x80); }
    if (ps.items & IT_KEY2) { *p++ = ' '; *p++ = (char)('G' | 0x80); }
    p = put_hi(p, " KILLS");
    p = put_int(p, ps.kills);
    *p++ = '/';
    p = put_int(p, ps.total_monsters);
    *p = 0;
    return ps.quad_finished > level_time;
}

#ifdef HOST
#include <stdio.h>
/* host debugging: the static things and whether their leaves are visible */
void g_debug_statics(void)
{
    int i;
    for (i = 0; i < nstatics; i++)
        fprintf(stderr, "static %d model %d leaf %d area %d vis %d\n", i, statics[i].model, statics[i].leaf,
                lvl.leafarea ? lvl.leafarea[statics[i].leaf] : -1, LEAF_VISIBLE(statics[i].leaf));
}
#endif

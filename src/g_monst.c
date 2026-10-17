/* Monsters: the grunt (monster_army) and the rottweiler (monster_dog),
 * after id's QuakeC (soldier.qc, dog.qc, ai.qc, fight.qc) and the
 * engine's monster movement (sv_move.c). They think at 10 Hz. */
#include "g_local.h"

#define STEPSIZE   18
#define EYE_OFS    25

enum { K_NONE, K_STAND, K_RUN, K_FACE, K_CHARGE, K_MOVE, K_PAINFWD };
enum { EV_NONE, EV_IDLE, EV_FIRE, EV_DROP, EV_BITE, EV_LEAP, EV_NOTSOLID };
#define HOLD 255

typedef struct anim_s {
    uint8_t first, len, next, kind;
    uint8_t ev_frame, ev;
    int8_t  dist[16];
} anim_t;

enum { A_STAND, A_RUN, A_ATTACK, A_MELEE, A_PAIN1, A_PAIN2, A_PAIN3, A_DIE1, A_DIE2, A_NUM };

/* frame numbers are the order of the $frame lines in the QuakeC */
static const anim_t army_anims[A_NUM] = {
    [A_STAND]  = { 0, 8, A_STAND, K_STAND, 0, EV_NONE, { 0 } },
    [A_RUN]    = { 73, 8, A_RUN, K_RUN, 0, EV_IDLE, { 11, 15, 10, 10, 8, 15, 10, 8 } },
    [A_ATTACK] = { 81, 9, A_RUN, K_FACE, 4, EV_FIRE, { 0 } },
    [A_MELEE]  = { 81, 9, A_RUN, K_FACE, 4, EV_FIRE, { 0 } },
    [A_PAIN1]  = { 40, 6, A_RUN, K_MOVE, 0, EV_NONE, { 0, 0, 0, 0, 0, -1 } },
    [A_PAIN2]  = { 46, 14, A_RUN, K_PAINFWD, 0, EV_NONE, { 0, 13, 9, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0 } },
    [A_PAIN3]  = { 60, 13, A_RUN, K_PAINFWD, 0, EV_NONE, { 0, -1, 0, 0, 1, 1, 0, -1, 4, 3, 6, 8, 0 } },
    [A_DIE1]   = { 8, 10, HOLD, K_NONE, 2, EV_DROP, { 0 } },
    [A_DIE2]   = { 18, 11, HOLD, K_MOVE, 2, EV_DROP, { 0, -5, -4, -13, -3, -4, 0, 0, 0, 0, 0 } },
};

static const anim_t dog_anims[A_NUM] = {
    [A_STAND]  = { 69, 9, A_STAND, K_STAND, 0, EV_NONE, { 0 } },
    [A_RUN]    = { 48, 12, A_RUN, K_RUN, 0, EV_IDLE, { 16, 32, 32, 20, 64, 32, 16, 32, 32, 20, 64, 32 } },
    [A_ATTACK] = { 60, 9, HOLD, K_FACE, 1, EV_LEAP, { 0 } },
    [A_MELEE]  = { 0, 8, A_RUN, K_CHARGE, 3, EV_BITE, { 10, 10, 10, 10, 10, 10, 10, 10 } },
    [A_PAIN1]  = { 26, 6, A_RUN, K_NONE, 0, EV_NONE, { 0 } },
    [A_PAIN2]  = { 32, 16, A_RUN, K_MOVE, 0, EV_NONE, { 0, 0, -4, -12, -12, -2, 0, -4, 0, -10 } },
    [A_PAIN3]  = { 26, 6, A_RUN, K_NONE, 0, EV_NONE, { 0 } },
    [A_DIE1]   = { 8, 9, HOLD, K_NONE, 0, EV_NONE, { 0 } },
    [A_DIE2]   = { 17, 9, HOLD, K_NONE, 0, EV_NONE, { 0 } },
};

static inline const anim_t *anims(const ent_t *e)
{
    return e->cls == CL_DOG ? dog_anims : army_anims;
}

static int anim_changed;

static void set_anim(ent_t *e, int a)
{
    e->anim = &anims(e)[a];
    e->animpos = 0;
    e->frame = e->anim->first;
    anim_changed = 1;
}

GCODE int m_hull(const ent_t *e)
{
    return e->cls == CL_DOG ? 2 : 1;
}

static void eye(const ent_t *e, fixed_t o[3])
{
    o[0] = e->org[0];
    o[1] = e->org[1];
    o[2] = e->org[2] + FX(EYE_OFS);
}

static void player_eye(fixed_t o[3])
{
    o[0] = player.pos[0];
    o[1] = player.pos[1];
    o[2] = player.pos[2] + FX(22);
}

/* ------------------------------------------------------------ senses */

static int can_see_player(const ent_t *e)
{
    fixed_t a[3], b[3];
    trace_t tr;
    eye(e, a);
    player_eye(b);
    g_trace(0, a, b, &tr);
    if (tr.inopen && tr.inwater)
        return 0;                   /* the sight line crossed a water surface */
    return tr.frac == 0x10000;
}

enum { RANGE_MELEE, RANGE_NEAR, RANGE_MID, RANGE_FAR };

static int range_of(const ent_t *e)
{
    fixed_t a[3], b[3];
    int r;
    eye(e, a);
    player_eye(b);
    r = vlen_units(a, b);
    return r < 120 ? RANGE_MELEE : r < 500 ? RANGE_NEAR : r < 1000 ? RANGE_MID : RANGE_FAR;
}

static int in_front(const ent_t *e)
{
    fixed_t dx = player.pos[0] - e->org[0], dy = player.pos[1] - e->org[1];
    uint16_t d = vectoyaw(dx, dy) - e->yaw;
    /* dot > 0.3: within about 72 degrees of the facing */
    return d < ANG(72) || d > (uint16_t)ANG(288);
}

static void change_yaw(ent_t *e)
{
    int16_t move = (int16_t)(e->ideal_yaw - e->yaw);
    const int speed = ANG(20);
    if (move > speed) move = speed;
    if (move < -speed) move = -speed;
    e->yaw += move;
}

static int facing_ideal(const ent_t *e)
{
    uint16_t d = e->yaw - e->ideal_yaw;
    return !(d > ANG(45) && d < (uint16_t)ANG(315));
}

static void hunt_target(ent_t *e)
{
    e->hunting = 1;
    e->ideal_yaw = vectoyaw(player.pos[0] - e->org[0], player.pos[1] - e->org[1]);
    e->attack_finished = level_time + TICKS(1);
    set_anim(e, A_RUN);
}

static void found_target(ent_t *e)
{
    fixed_t c[3];
    eye(e, c);
    snd_start(e->cls == CL_DOG ? SND_DOG_DSIGHT : SND_SOLDIER_SIGHT1, c, 255);
    hunt_target(e);
}

static int find_target(ent_t *e)
{
    int r;
    if (ps.dead || !LEAF_VISIBLE(e->leaf))
        return 0;
    r = range_of(e);
    if (r == RANGE_FAR)
        return 0;
    if (!can_see_player(e))
        return 0;
    if ((r == RANGE_NEAR || r == RANGE_MID) && !in_front(e))
        return 0;
    found_target(e);
    return 1;
}

/* ------------------------------------------------------------ movement */

/* SV_movestep: move by (dx, dy), stepping up and down stairs; monsters do
 * not walk off ledges */
static int movestep(ent_t *e, fixed_t dx, fixed_t dy)
{
    fixed_t s[3], end[3];
    trace_t tr;
    int hull = m_hull(e);
    s[0] = e->org[0] + dx;
    s[1] = e->org[1] + dy;
    s[2] = e->org[2] + FX(STEPSIZE);
    end[0] = s[0];
    end[1] = s[1];
    end[2] = e->org[2] - FX(STEPSIZE);
    g_trace(hull, s, end, &tr);
    if (tr.allsolid)
        return 0;
    if (tr.startsolid) {
        s[2] -= FX(STEPSIZE);
        g_trace(hull, s, end, &tr);
        if (tr.allsolid || tr.startsolid)
            return 0;
    }
    if (tr.frac == 0x10000)
        return 0;                   /* would walk off an edge */
    /* other monsters and the player block */
    {
        int i, nx = UNITS(tr.end[0]), ny = UNITS(tr.end[1]);
        int r = e->maxs[0];
        int px = UNITS(player.pos[0]), py = UNITS(player.pos[1]), pz = UNITS(player.pos[2]);
        int ez = UNITS(tr.end[2]);
        if (!ps.dead && nx - r < px + 16 && nx + r > px - 16 && ny - r < py + 16 && ny + r > py - 16
            && ez - 24 < pz + 32 && ez + 40 > pz - 24)
            return 0;
        for (i = 0; i < MAX_ENTS; i++) {
            const ent_t *o = &ents[i];
            int ox, oy, orr;
            if (o == e || o->type != ET_MONSTER)
                continue;
            ox = UNITS(o->org[0]);
            oy = UNITS(o->org[1]);
            orr = o->maxs[0];
            if (nx - r < ox + orr && nx + r > ox - orr && ny - r < oy + orr && ny + r > oy - orr)
                return 0;
        }
    }
    e->org[0] = tr.end[0];
    e->org[1] = tr.end[1];
    e->org[2] = tr.end[2];
    return 1;
}

static int walkmove(ent_t *e, uint16_t yaw, int dist)
{
    return movestep(e, fcos(yaw) * dist, fsin(yaw) * dist);
}

static int step_direction(ent_t *e, uint16_t yaw, int dist)
{
    fixed_t old[3];
    e->ideal_yaw = yaw;
    change_yaw(e);
    old[0] = e->org[0]; old[1] = e->org[1]; old[2] = e->org[2];
    if (walkmove(e, yaw, dist)) {
        if (!facing_ideal(e)) {
            e->org[0] = old[0]; e->org[1] = old[1]; e->org[2] = old[2];
        }
        return 1;
    }
    return 0;
}

#define DI_NODIR 0xFFFF

static void new_chase_dir(ent_t *e, int dist)
{
    uint16_t d1, d2, tdir, olddir, turnaround;
    fixed_t dx = player.pos[0] - e->org[0], dy = player.pos[1] - e->org[1];
    int i;
    olddir = (uint16_t)((e->ideal_yaw + 4096) & 0xE000);    /* nearest 45 degrees */
    turnaround = olddir + 32768;
    d1 = dx > FX(10) ? 0 : dx < -FX(10) ? 32768 : DI_NODIR;
    d2 = dy < -FX(10) ? 49152 : dy > FX(10) ? 16384 : DI_NODIR;
    if (d1 != DI_NODIR && d2 != DI_NODIR) {
        if (d1 == 0)
            tdir = d2 == 16384 ? 8192 : 57344;
        else
            tdir = d2 == 16384 ? 24576 : 40960;
        if (tdir != turnaround && step_direction(e, tdir, dist))
            return;
    }
    if ((g_rand() & 1) || (dy < 0 ? -dy : dy) > (dx < 0 ? -dx : dx)) {
        tdir = d1; d1 = d2; d2 = tdir;
    }
    if (d1 != DI_NODIR && d1 != turnaround && step_direction(e, d1, dist))
        return;
    if (d2 != DI_NODIR && d2 != turnaround && step_direction(e, d2, dist))
        return;
    if (step_direction(e, olddir, dist))
        return;
    if (g_rand() & 1) {
        for (i = 0; i < 8; i++)
            if ((uint16_t)(i * 8192) != turnaround && step_direction(e, i * 8192, dist))
                return;
    } else {
        for (i = 7; i >= 0; i--)
            if ((uint16_t)(i * 8192) != turnaround && step_direction(e, i * 8192, dist))
                return;
    }
    if (step_direction(e, turnaround, dist))
        return;
    e->ideal_yaw = olddir;          /* can't move */
}

static int close_enough(const ent_t *e, int dist)
{
    int k;
    for (k = 0; k < 2; k++) {
        int emin = UNITS(e->org[k]) + e->mins[k], emax = UNITS(e->org[k]) + e->maxs[k];
        int pmin = UNITS(player.pos[k]) - 16, pmax = UNITS(player.pos[k]) + 16;
        if (pmin > emax + dist || pmax < emin - dist)
            return 0;
    }
    return 1;
}

static void movetogoal(ent_t *e, int dist)
{
    if (!e->onground)
        return;
    if (close_enough(e, dist))
        return;
    if ((g_rand() & 3) == 1 || !step_direction(e, e->ideal_yaw, dist))
        new_chase_dir(e, dist);
}

/* ------------------------------------------------------------ attacks */

/* the grunt's shotgun: 4 pellets at the player */
static void army_fire(ent_t *e)
{
    fixed_t src[3], dir[3], tgt[3], len;
    int n, k, dmg = 0;
    eye(e, src);
    src[2] = e->org[2] + FX(-24 + 64 * 7 / 10);
    snd_start(SND_SOLDIER_SATTCK1, src, 255);
    player_eye(tgt);
    tgt[2] -= FX(8);
    for (k = 0; k < 3; k++)
        dir[k] = tgt[k] - src[k];
    len = FX(vlen_units(src, tgt));
    if (len <= 0)
        return;
    for (k = 0; k < 3; k++)
        dir[k] = hw_fixdiv(dir[k], len);
    for (n = 0; n < 4; n++) {
        fixed_t d[3], end[3], mn[3], mx[3], t;
        trace_t tr;
        fixed_t cr = fixmul(g_crand(), 0x1999), cu = fixmul(g_crand(), 0x1999);
        /* spread 0.1 x 0.1 around the aim (right = (-dy, dx), up = z) */
        d[0] = dir[0] - fixmul(cr, dir[1]);
        d[1] = dir[1] + fixmul(cr, dir[0]);
        d[2] = dir[2] + cu;
        for (k = 0; k < 3; k++)
            end[k] = src[k] + d[k] * 2048;
        g_trace(0, src, end, &tr);
        mn[0] = player.pos[0] - FX(16); mx[0] = player.pos[0] + FX(16);
        mn[1] = player.pos[1] - FX(16); mx[1] = player.pos[1] + FX(16);
        mn[2] = player.pos[2] - FX(24); mx[2] = player.pos[2] + FX(32);
        if (g_ray_box(src, d, tr.frac * 2048, mn, mx, &t))
            dmg += 4;
        else if (tr.frac < 0x10000) {
            fixed_t at[3];
            for (k = 0; k < 3; k++)
                at[k] = tr.end[k];
            g_particles(at, NULL, 0, 3);
        }
    }
    if (dmg)
        g_damage_player(dmg, e->org);
}

static void dog_bite(ent_t *e)
{
    int d;
    fixed_t c[3];
    eye(e, c);
    snd_start(SND_DOG_DATTACK1, c, 255);
    if (!can_see_player(e))
        return;
    d = vlen_units(e->org, player.pos);
    if (d > 100)
        return;
    g_damage_player(((g_rand() + g_rand() + g_rand()) * 8) >> 15, e->org);
}

static void dog_leap(ent_t *e)
{
    e->vel[0] = fcos(e->yaw) * 300;
    e->vel[1] = fsin(e->yaw) * 300;
    e->vel[2] = FX(200);
    e->org[2] += FX(1);
    e->onground = 0;
    e->leaping = 1;
}

static int check_attack(ent_t *e, int range, int visible)
{
    int chance;
    if (!visible)
        return 0;
    if (e->cls == CL_DOG) {
        int dz, d;
        if (range == RANGE_MELEE) {
            e->attack_state = AS_MELEE;
            return 1;
        }
        /* CheckDogJump: player roughly level, 80..150 units away */
        dz = UNITS(e->org[2] - player.pos[2]);
        if (dz - 24 > -24 + 42 || dz + 40 < -24 + 14)
            return 0;
        {
            fixed_t a[3], b[3];
            a[0] = e->org[0]; a[1] = e->org[1]; a[2] = 0;
            b[0] = player.pos[0]; b[1] = player.pos[1]; b[2] = 0;
            d = vlen_units(a, b);
        }
        if (d < 80 || d > 150)
            return 0;
        e->attack_state = AS_MISSILE;
        return 1;
    }
    /* SoldierCheckAttack */
    if (level_time < e->attack_finished || range == RANGE_FAR)
        return 0;
    chance = range == RANGE_MELEE ? 29491 : range == RANGE_NEAR ? 13107 : 1638;   /* 0.9 0.4 0.05 */
    if (g_rand() < chance) {
        set_anim(e, A_ATTACK);
        e->attack_finished = level_time + TICKS(1) + (g_rand() * 60 >> 15);
        return 1;
    }
    return 0;
}

static void ai_run(ent_t *e, int dist)
{
    int visible, range;
    uint16_t enemy_yaw;
    if (ps.dead) {
        e->hunting = 0;
        set_anim(e, A_STAND);
        return;
    }
    visible = can_see_player(e);
    range = range_of(e);
    enemy_yaw = vectoyaw(player.pos[0] - e->org[0], player.pos[1] - e->org[1]);
    if (e->attack_state == AS_MISSILE || e->attack_state == AS_MELEE) {
        e->ideal_yaw = enemy_yaw;
        change_yaw(e);
        if (facing_ideal(e)) {
            set_anim(e, e->attack_state == AS_MISSILE ? A_ATTACK : A_MELEE);
            e->attack_state = AS_STRAIGHT;
        }
        return;
    }
    if (check_attack(e, range, visible))
        return;
    movetogoal(e, dist);
}

static void ai_face(ent_t *e)
{
    e->ideal_yaw = vectoyaw(player.pos[0] - e->org[0], player.pos[1] - e->org[1]);
    change_yaw(e);
}

/* ------------------------------------------------------------ entry points */

GCODE void m_spawn(ent_t *e, int cls, int hunting)
{
    fixed_t s[3], end[3];
    trace_t tr;
    e->type = ET_MONSTER;
    e->cls = cls;
    if (cls == CL_DOG) {
        e->model = MDL_DOG;
        e->health = 25;
        e->mins[0] = e->mins[1] = -32;
        e->maxs[0] = e->maxs[1] = 32;
    } else {
        e->model = MDL_SOLDIER;
        e->health = 30;
        e->mins[0] = e->mins[1] = -16;
        e->maxs[0] = e->maxs[1] = 16;
    }
    e->mins[2] = -24;
    e->maxs[2] = 40;
    e->spawned = level_time;
    /* droptofloor */
    s[0] = e->org[0]; s[1] = e->org[1]; s[2] = e->org[2] + FX(1);
    end[0] = s[0]; end[1] = s[1]; end[2] = s[2] - FX(256);
    g_trace(m_hull(e), s, end, &tr);
    if (!tr.allsolid && !tr.startsolid && tr.frac < 0x10000) {
        e->org[2] = tr.end[2];
        e->onground = 1;
    }
    e->nextthink = level_time + 1 + (g_rand() % 30);
    g_relink(e);
    if (hunting)
        found_target(e);
    else
        set_anim(e, A_STAND);
}

GCODE void m_pain(ent_t *e, int dmg)
{
    int r;
    fixed_t c[3];
    (void)dmg;
    if (!e->hunting)
        found_target(e);
    eye(e, c);
    if (e->cls == CL_DOG) {
        snd_start(SND_DOG_DPAIN1, c, 255);
        set_anim(e, g_rand() & 1 ? A_PAIN1 : A_PAIN2);
        e->nextthink = level_time + 6;
        return;
    }
    if (e->pain_finished > level_time)
        return;
    r = g_rand();
    if (r < 6554) {
        e->pain_finished = level_time + TICKS(0.6);
        set_anim(e, A_PAIN1);
        snd_start(SND_SOLDIER_PAIN1, c, 255);
    } else {
        e->pain_finished = level_time + TICKS(1.1);
        set_anim(e, r < 19661 ? A_PAIN2 : A_PAIN3);
        snd_start(SND_SOLDIER_PAIN2, c, 255);
    }
    e->nextthink = level_time + 6;
}

GCODE void m_die(ent_t *e)
{
    fixed_t c[3];
    int i, corpses = 0, oldest = -1;
    eye(e, c);
    snd_start(e->cls == CL_DOG ? SND_DOG_DDEATH : SND_SOLDIER_DEATH1, c, 255);
    set_anim(e, g_rand() & 1 ? A_DIE1 : A_DIE2);
    e->type = ET_CORPSE;
    e->died = level_time;
    e->nextthink = level_time + 6;
    /* keep only the most recent bodies */
    for (i = 0; i < MAX_ENTS; i++)
        if (ents[i].type == ET_CORPSE) {
            corpses++;
            if (oldest < 0 || ents[i].died < ents[oldest].died)
                oldest = i;
        }
    if (corpses > MAX_CORPSES && oldest >= 0)
        g_free(&ents[oldest]);
}

static void drop_backpack(ent_t *e)
{
    ent_t *b = g_spawn();
    if (!b)
        return;
    b->type = ET_ITEM;
    b->cls = CL_BACKPACK;
    b->model = MDL_BACKPACK;
    b->org[0] = e->org[0];
    b->org[1] = e->org[1];
    b->org[2] = e->org[2] - FX(24);
    b->mins[0] = b->mins[1] = -16; b->mins[2] = 0;
    b->maxs[0] = b->maxs[1] = 16; b->maxs[2] = 56;
    g_relink(b);
}

GCODE void m_think(ent_t *e)
{
    const anim_t *a = e->anim;
    int pos = e->animpos, d = a->dist[pos < 16 ? pos : 15];
    e->nextthink = level_time + 6;
    e->frame = a->first + pos;
    anim_changed = 0;

    if (e->type == ET_CORPSE) {
        if (a->ev == EV_DROP && pos == a->ev_frame && e->cls == CL_ARMY)
            drop_backpack(e);
        if (a->kind == K_MOVE && d)
            walkmove(e, e->yaw, d);
        if (pos + 1 < a->len)
            e->animpos++;
        return;
    }

    switch (a->kind) {
    case K_STAND:
        find_target(e);
        break;
    case K_RUN:
        ai_run(e, d);
        break;
    case K_FACE:
        ai_face(e);
        break;
    case K_CHARGE:
        ai_face(e);
        movetogoal(e, d);
        break;
    case K_MOVE:
        if (d) walkmove(e, d > 0 ? e->ideal_yaw : e->yaw + 32768, d > 0 ? d : -d);
        break;
    case K_PAINFWD:
        if (d) walkmove(e, d > 0 ? e->ideal_yaw : e->yaw + 32768, d > 0 ? d : -d);
        break;
    }
    if (!anim_changed && pos == a->ev_frame) {
        fixed_t c[3];
        switch (a->ev) {
        case EV_IDLE:
            if (g_rand() < 6554) {
                eye(e, c);
                snd_start(e->cls == CL_DOG ? SND_DOG_IDLE : SND_SOLDIER_IDLE, c, 160);
            }
            break;
        case EV_FIRE: army_fire(e); break;
        case EV_BITE: dog_bite(e); break;
        case EV_LEAP: dog_leap(e); break;
        }
    }
    if (anim_changed)
        return;
    if (pos + 1 < a->len)
        e->animpos++;
    else if (a->next != HOLD)
        set_anim(e, a->next);
}

/* gravity and leaps (MOVETYPE_STEP / the dog's jump), every tick */
GCODE void m_physics(ent_t *e)
{
    fixed_t end[3];
    trace_t tr;
    int k;
    if (e->onground && !e->leaping) {
        /* still standing on something? (checked at 10 Hz) */
        if (((level_time + (e - ents)) % 6) == 0) {
            fixed_t p[3];
            p[0] = e->org[0]; p[1] = e->org[1]; p[2] = e->org[2] - FX(1);
            if (g_contents(m_hull(e), p) != CONTENTS_SOLID)
                e->onground = 0;
        }
        if (e->onground)
            return;
    }
    e->vel[2] -= FX(800) / 60;
    for (k = 0; k < 3; k++)
        end[k] = e->org[k] + e->vel[k] / 60;
    g_trace(m_hull(e), e->org, end, &tr);
    if (tr.allsolid) {
        e->vel[0] = e->vel[1] = e->vel[2] = 0;
        e->onground = 1;
        e->leaping = 0;
        return;
    }
    e->org[0] = tr.end[0];
    e->org[1] = tr.end[1];
    e->org[2] = tr.end[2];
    /* a leaping dog bites whatever it lands on */
    if (e->leaping && e->type == ET_MONSTER && !ps.dead) {
        int dx = UNITS(e->org[0] - player.pos[0]), dy = UNITS(e->org[1] - player.pos[1]);
        int dz = UNITS(e->org[2] - player.pos[2]);
        if (dx > -48 && dx < 48 && dy > -48 && dy < 48 && dz > -64 && dz < 56) {
            g_damage_player(10 + ((g_rand() * 10) >> 15), e->org);
            e->leaping = 2;          /* hit once */
        }
    }
    if (tr.frac < 0x10000) {
        if (tr.normal[2] > 0xB333) {   /* floor (normal z > 0.7) */
            e->onground = 1;
            e->vel[0] = e->vel[1] = e->vel[2] = 0;
            if (e->leaping) {
                e->leaping = 0;
                if (e->type == ET_MONSTER) {
                    set_anim(e, A_RUN);
                    e->nextthink = level_time + 6;
                }
            }
        } else {
            e->vel[0] = e->vel[1] = 0;      /* hit a wall: drop */
        }
    }
}

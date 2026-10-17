/* The player's weapons, missiles, explosions, gibs and the effects that go
 * with them, after id's QuakeC (weapons.qc, player.qc, combat.qc, misc.qc)
 * and the client's temporary entities (cl_tent.c, r_part.c). Runs on the
 * master SH-2, from cartridge ROM (GCODE). */
#include "g_local.h"
#include "r_local.h"

/* ------------------------------------------------------------ vectors */

/* normalize a vector (16.16) in place; returns its length (16.16) */
GCODE fixed_t g_normalize(fixed_t v[3])
{
    int32_t a[3];
    int64_t s;
    uint32_t len, r = 0, b = 1u << 30, x;
    int k, m = 0, sh = 0;
    for (k = 0; k < 3; k++) {
        a[k] = v[k];
        m |= a[k] < 0 ? -a[k] : a[k];
    }
    while (m >= 0x8000) {
        for (k = 0; k < 3; k++)
            a[k] >>= 1;
        m >>= 1;
        sh++;
    }
    s = (int64_t)a[0] * a[0] + (int64_t)a[1] * a[1] + (int64_t)a[2] * a[2];
    x = (uint32_t)s;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    len = r;                                      /* same scale as a */
    if (len == 0)
        return 0;
    for (k = 0; k < 3; k++)
        v[k] = hw_div64((int64_t)a[k] << 16, len);
    return (fixed_t)(len << sh);
}

/* the player's view directions (Quake's makevectors on v_angle) */
GCODE void g_view_vectors(fixed_t fwd[3], fixed_t right[3], fixed_t up[3])
{
    fixed_t sy = fsin(player.yaw), cy = fcos(player.yaw);
    fixed_t sp = fsin(player.pitch), cp = fcos(player.pitch);
    fwd[0] = fixmul(cy, cp); fwd[1] = fixmul(sy, cp); fwd[2] = -sp;
    right[0] = sy; right[1] = -cy; right[2] = 0;
    up[0] = fixmul(cy, sp); up[1] = fixmul(sy, sp); up[2] = cp;
}

static void yaw_vectors(uint16_t yaw, fixed_t fwd[3], fixed_t right[3])
{
    fwd[0] = fcos(yaw); fwd[1] = fsin(yaw); fwd[2] = 0;
    right[0] = fsin(yaw); right[1] = -fcos(yaw); right[2] = 0;
}

/* pitch of a direction, as a 16-bit angle (up positive) */
static uint16_t vectopitch(const fixed_t d[3])
{
    fixed_t h = d[0] < 0 ? -d[0] : d[0], v = d[1] < 0 ? -d[1] : d[1];
    h = h > v ? h + (v >> 1) : v + (h >> 1);        /* rough length of x, y */
    return vectoyaw(h, d[2]);
}

/* ------------------------------------------------------------ effects */

/* R_RunParticleEffect's cousin with a speed: a burst of count particles */
GCODE void g_burst(const fixed_t org[3], int color, int count, int speed, int life)
{
    int i, k, leaf = r_pointleaf(org);
    for (i = 0; i < MAX_PARTICLES && count > 0; i++) {
        particle_t *p = &particles[i];
        if ((int16_t)(p->die - (uint16_t)level_time) > 0)
            continue;
        for (k = 0; k < 3; k++) {
            p->org[k] = org[k] + ((g_rand() & 15) - 8) * 0x10000;
            p->vel[k] = ((g_rand() % (2 * speed + 1)) - speed) << 16;
        }
        p->color = color < 0 ? 0x6F - (g_rand() & 7) * 2 : (color & ~7) + (g_rand() & 7);
        p->type = 2;
        p->leaf = leaf;
        g_particle_life(p, life / 2 + (g_rand() % (life / 2 + 1)));
        count--;
    }
}

/* TE_EXPLOSION: Quake's fireball, thinned, and its sound */
GCODE void g_explosion(const fixed_t org[3])
{
    g_burst(org, -1, 40, 256, TICKS(0.8));
    snd_start(SND_R_EXP3, org, 255);
}

/* TE_SPIKE / TE_GUNSHOT / TE_WIZSPIKE where a missile hit a wall */
static void spike_hit_fx(int kind, const fixed_t org[3])
{
    if (kind == MK_WIZSPIT) {
        g_particles(org, NULL, 20, 15);
        snd_start(SND_WIZARD_HIT, org, 255);
        return;
    }
    g_particles(org, NULL, 0, kind == MK_NAIL ? 5 : 10);
    if (g_rand() % 5)
        snd_start(SND_TINK1, org, 255);
    else
        snd_start(SND_RIC1 + (g_rand() % 3), org, 255);
}

/* a trail particle behind a missile or gib (R_RocketTrail) */
static void trail(const ent_t *e)
{
    const lmodel_t *m = g_model(e->model);
    int fl, color, type;
    particle_t *p;
    if (!m || ((level_time + (e - ents)) & 1))
        return;
    fl = m->flags;
    if (fl & MF_GRENADE) { color = 0x0E - (g_rand() & 3); type = 1; }      /* smoke */
    else if (fl & (MF_GIB | MF_ZOMGIB)) { color = 67 + (g_rand() & 3); type = 2; }
    else if (fl & MF_TRACER) { color = 52 + (g_rand() & 7); type = 0; }    /* wizard spit */
    else return;
    p = g_new_particle(e->org);
    if (!p)
        return;
    p->color = color;
    p->type = type;
    p->vel[0] = p->vel[1] = 0;
    p->vel[2] = type == 1 ? FX(12) : 0;
    g_particle_life(p, TICKS(0.4) + (g_rand() & 15));
}

/* ------------------------------------------------------------ damage */

static void ent_box(const ent_t *e, fixed_t mn[3], fixed_t mx[3])
{
    int k;
    for (k = 0; k < 3; k++) {
        mn[k] = e->org[k] + FX(e->mins[k]);
        mx[k] = e->org[k] + FX(e->maxs[k]);
    }
}

/* can the explosion at org reach the point p (CanDamage: centre or a corner) */
static int can_damage(const fixed_t org[3], const fixed_t p[3])
{
    trace_t tr;
    fixed_t q[3];
    g_trace(0, org, p, &tr);
    if (tr.frac == 0x10000)
        return 1;
    q[0] = p[0] + FX(15); q[1] = p[1] + FX(15); q[2] = p[2];
    g_trace(0, org, q, &tr);
    if (tr.frac == 0x10000)
        return 1;
    q[0] = p[0] - FX(15); q[1] = p[1] - FX(15);
    g_trace(0, org, q, &tr);
    return tr.frac == 0x10000;
}

/* T_RadiusDamage: everything within dmg + 40 takes dmg less half the
 * distance; attacker OWN_PLAYER takes half of it himself */
GCODE void g_radius_damage(const fixed_t org[3], int dmg, int attacker, int ignore)
{
    int i, k;
    fixed_t c[3];
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        int pts;
        if (i == ignore || (e->type != ET_MONSTER && e->type != ET_BOX) || e->health <= 0
            || (e->flags & EF_NOTSOLID))
            continue;
        for (k = 0; k < 3; k++)
            c[k] = e->org[k] + (FX(e->mins[k] + e->maxs[k]) >> 1);
        pts = dmg - (vlen_units(org, c) >> 1);
        if (pts <= 0 || !can_damage(org, c))
            continue;
        if (e->type == ET_BOX)
            g_box_damage(e, pts);
        else if (attacker == OWN_PLAYER)
            g_damage(e, pts);
        else
            g_hurt(e, pts);
    }
    if (ignore != OWN_PLAYER && !ps.dead) {
        int pts;
        c[0] = player.pos[0];
        c[1] = player.pos[1];
        c[2] = player.pos[2] + FX(4);
        pts = dmg - (vlen_units(org, c) >> 1);
        if (attacker == OWN_PLAYER)
            pts >>= 1;
        if (pts > 0 && can_damage(org, c))
            g_damage_player(pts, org);
    }
}

/* ------------------------------------------------------------ gibs */

/* VelocityForDamage */
static void gib_velocity(fixed_t v[3], int health)
{
    int s = health > -50 ? 0xB333 : health > -200 ? 0x20000 : 0xA0000;    /* 0.7, 2, 10 */
    v[0] = fixmul(FX(100) * 1, g_crand());
    v[1] = fixmul(FX(100) * 1, g_crand());
    v[2] = FX(200) + fixmul(FX(100), g_frand());
    v[0] = fixmul(v[0], s);
    v[1] = fixmul(v[1], s);
    v[2] = fixmul(v[2], s);
}

/* ThrowGib: a bit of the body flies off; gone after 10 to 20 seconds */
GCODE void g_throw_gib(int model, const fixed_t org[3], int health)
{
    ent_t *e, *oldest = NULL;
    int i, n = 0;
    if (!g_model(model))
        return;
    /* only a few at a time: the oldest goes */
    for (i = 0; i < MAX_ENTS; i++)
        if (ents[i].type == ET_GIB && ents[i].cls == 0) {
            n++;
            if (!oldest || ents[i].nextthink < oldest->nextthink)
                oldest = &ents[i];
        }
    if (n >= MAX_GIBS && oldest)
        g_free(oldest);
    e = g_spawn();
    if (!e)
        return;
    e->type = ET_GIB;
    e->model = model;
    e->org[0] = org[0];
    e->org[1] = org[1];
    e->org[2] = org[2];
    gib_velocity(e->vel, health);
    e->yaw = g_rand() << 1;
    e->nextthink = level_time + TICKS(10) + (g_rand() % TICKS(10));
    g_relink(e);
}

/* ThrowHead: the monster itself becomes its head (which lies a moment) */
GCODE void g_throw_head(ent_t *e, int model, int health)
{
    e->type = ET_GIB;
    e->cls = 1;                         /* a head: goes once at rest */
    e->model = model;
    e->frame = 0;
    e->skin = 0;
    e->anim = NULL;
    e->org[2] += FX(24);
    gib_velocity(e->vel, health);
    e->mins[0] = e->mins[1] = e->mins[2] = 0;
    e->maxs[0] = e->maxs[1] = e->maxs[2] = 0;
    e->onground = 0;
    e->died = level_time;
    e->nextthink = 0;
    g_relink(e);
}

/* ------------------------------------------------------------ missiles */

/* a missile: kind MK_*, from org with velocity vel (units a second) */
GCODE ent_t *g_missile(int kind, const fixed_t org[3], const fixed_t vel[3], int owner)
{
    static const uint8_t models[MK_NUM] = {
        [MK_NAIL] = MDL_SPIKE, [MK_TRAPSPIKE] = MDL_SPIKE, [MK_GRENADE] = MDL_GRENADE,
        [MK_OGRE_GRENADE] = MDL_GRENADE, [MK_WIZSPIT] = MDL_W_SPIKE, [MK_ZOMGIB] = MDL_ZOM_GIB,
    };
    ent_t *e = g_spawn();
    fixed_t d[3];
    if (!e)
        return NULL;
    e->type = ET_MISSILE;
    e->cls = kind;
    e->model = models[kind];
    e->owner = owner;
    e->org[0] = org[0];
    e->org[1] = org[1];
    e->org[2] = org[2];
    e->vel[0] = vel[0];
    e->vel[1] = vel[1];
    e->vel[2] = vel[2];
    d[0] = vel[0]; d[1] = vel[1]; d[2] = vel[2];
    e->yaw = vectoyaw(d[0], d[1]);
    e->pitch = vectopitch(d);
    /* fuses and lifetimes */
    e->nextthink = level_time + (kind == MK_GRENADE || kind == MK_OGRE_GRENADE || kind == MK_ZOMGIB
                                 ? TICKS(2.5) : kind == MK_WIZSPIT ? TICKS(5) : TICKS(6));
    g_relink(e);
    return e;
}

static int bounces(int kind)
{
    return kind == MK_GRENADE || kind == MK_OGRE_GRENADE || kind == MK_ZOMGIB;
}

static void missile_explode(ent_t *e)
{
    int dmg = e->cls == MK_GRENADE ? 120 : 40;
    g_radius_damage(e->org, dmg, e->owner, -1000);
    g_explosion(e->org);
    g_free(e);
}

/* the missile touched a monster, a barrel or the player (hit >= 0: ents
 * index, -1 the player) */
static void missile_touch_thing(ent_t *e, int hit)
{
    int kind = e->cls;
    if (kind == MK_GRENADE || kind == MK_OGRE_GRENADE) {
        missile_explode(e);
        return;
    }
    if (kind == MK_ZOMGIB) {
        if (hit < 0) g_damage_player(10, e->org);
        else if (ents[hit].type == ET_BOX) g_box_damage(&ents[hit], 10);
        else g_hurt(&ents[hit], 10);
        snd_start(SND_ZOMBIE_Z_HIT, e->org, 255);
        g_free(e);
        return;
    }
    /* spikes: 9 points, and blood */
    g_particles(e->org, NULL, 73, 9);
    if (hit < 0)
        g_damage_player(9, e->org);
    else if (ents[hit].type == ET_BOX)
        g_box_damage(&ents[hit], 9);
    else if (e->owner == OWN_PLAYER)
        g_damage(&ents[hit], 9);
    else
        g_hurt(&ents[hit], 9);
    g_free(e);
}

/* MOVETYPE_FLYMISSILE / MOVETYPE_BOUNCE for one tick */
static void run_missile(ent_t *e)
{
    fixed_t end[3], d[3], len, best;
    trace_t tr;
    int k, i, hit = -2;
    if (level_time >= e->nextthink) {
        if (e->cls == MK_GRENADE || e->cls == MK_OGRE_GRENADE)
            missile_explode(e);
        else
            g_free(e);
        return;
    }
    if (bounces(e->cls) && !e->onground)
        e->vel[2] -= FX(800) / 60;
    if (e->onground)
        return;
    for (k = 0; k < 3; k++) {
        d[k] = e->vel[k] / 60;
        end[k] = e->org[k] + d[k];
    }
    g_trace(0, e->org, end, &tr);
    len = g_normalize(d);
    best = fixmul(len, tr.frac);
    /* the things it could hit on the way */
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *o = &ents[i];
        fixed_t mn[3], mx[3], t;
        if ((o->type != ET_MONSTER && o->type != ET_BOX) || i == e->owner || (o->flags & EF_NOTSOLID))
            continue;
        ent_box(o, mn, mx);
        if (g_ray_box(e->org, d, best, mn, mx, &t) && t <= best) {
            best = t;
            hit = i;
        }
    }
    if (e->owner != OWN_PLAYER && !ps.dead) {
        fixed_t mn[3], mx[3], t;
        mn[0] = player.pos[0] - FX(16); mx[0] = player.pos[0] + FX(16);
        mn[1] = player.pos[1] - FX(16); mx[1] = player.pos[1] + FX(16);
        mn[2] = player.pos[2] - FX(24); mx[2] = player.pos[2] + FX(32);
        if (g_ray_box(e->org, d, best, mn, mx, &t) && t <= best) {
            best = t;
            hit = -1;
        }
    }
    if (hit != -2) {
        for (k = 0; k < 3; k++)
            e->org[k] += fixmul(d[k], best);
        missile_touch_thing(e, hit);
        return;
    }
    if (tr.frac == 0x10000) {
        e->org[0] = end[0];
        e->org[1] = end[1];
        e->org[2] = end[2];
        if (bounces(e->cls) || e->cls == MK_GRENADE)
            e->yaw += 300 * 65536 / 360 / 60;       /* tumbling */
        trail(e);
        if (((level_time + (e - ents)) & 3) == 0)
            g_relink(e);
        return;
    }
    /* hit the world */
    for (k = 0; k < 3; k++)
        e->org[k] = tr.end[k] - (d[k] >> 3);       /* an eighth of a unit back */
    if (g_contents(0, e->org) == CONTENTS_SKY) {
        g_free(e);
        return;
    }
    if (!bounces(e->cls)) {
        if (tr.mover >= 0 && e->owner == OWN_PLAYER)
            mv_damage(tr.mover, 9);
        spike_hit_fx(e->cls, e->org);
        g_free(e);
        return;
    }
    if (e->cls == MK_ZOMGIB) {
        /* ZombieGrenadeTouch: splat, and it lies there */
        snd_start(SND_ZOMBIE_Z_MISS, e->org, 255);
        e->vel[0] = e->vel[1] = e->vel[2] = 0;
        e->onground = 1;
        e->nextthink = level_time + TICKS(1);
        g_relink(e);
        return;
    }
    /* ClipVelocity with overbounce 1.5 */
    {
        fixed_t back = fixmul(dot16(e->vel, tr.normal), 0x18000);
        for (k = 0; k < 3; k++)
            e->vel[k] -= fixmul(tr.normal[k], back);
    }
    if (tr.normal[2] > 0xB333 && e->vel[2] < FX(60)) {
        e->vel[0] = e->vel[1] = e->vel[2] = 0;
        e->onground = 1;
    } else
        snd_start(SND_BOUNCE, e->org, 255);
    g_relink(e);
}

/* gibs and heads: MOVETYPE_BOUNCE, then still; gibs go after a while */
static void run_gib(ent_t *e)
{
    fixed_t end[3];
    trace_t tr;
    int k;
    if (e->cls == 0 && level_time >= e->nextthink) {
        g_free(e);
        return;
    }
    if (e->onground) {
        /* a head at rest teleports away, like the bodies */
        if (e->cls == 1) {
            if (!e->nextthink)
                e->nextthink = level_time + CORPSE_STAY;
            else if (level_time >= e->nextthink)
                g_vanish(e);
        }
        return;
    }
    e->vel[2] -= FX(800) / 60;
    for (k = 0; k < 3; k++)
        end[k] = e->org[k] + e->vel[k] / 60;
    g_trace(0, e->org, end, &tr);
    if (tr.frac == 0x10000) {
        e->org[0] = end[0]; e->org[1] = end[1]; e->org[2] = end[2];
        e->yaw += 1000 * 65536 / 360 / 60;
        trail(e);
        if (((level_time + (e - ents)) & 3) == 0)
            g_relink(e);
        return;
    }
    for (k = 0; k < 3; k++)
        e->org[k] = tr.end[k] + (tr.normal[k] >> 4);
    {
        fixed_t back = fixmul(dot16(e->vel, tr.normal), 0x18000);
        for (k = 0; k < 3; k++)
            e->vel[k] -= fixmul(tr.normal[k], back);
    }
    if (tr.normal[2] > 0xB333 && e->vel[2] < FX(60)) {
        e->vel[0] = e->vel[1] = e->vel[2] = 0;
        e->onground = 1;
    }
    g_relink(e);
}

GCODE void g_run_thing(ent_t *e)
{
    if (e->type == ET_MISSILE)
        run_missile(e);
    else if (e->type == ET_GIB)
        run_gib(e);
    else if (e->type == ET_BOX && e->nextthink && level_time >= e->nextthink) {
        /* barrel_explode */
        fixed_t c[3];
        c[0] = e->org[0] + FX(16);
        c[1] = e->org[1] + FX(16);
        c[2] = e->org[2] + FX(32);
        g_free(e);
        g_radius_damage(c, 160, OWN_WORLD, e - ents);
        g_burst(c, 75, 50, 256, TICKS(0.8));
        g_explosion(c);
    }
}

/* misc_explobox: 20 points of damage set it off, 0.3 s later */
GCODE void g_box_damage(ent_t *e, int dmg)
{
    if (e->type != ET_BOX || e->health <= 0)
        return;
    e->health -= dmg;
    if (e->health <= 0)
        e->nextthink = level_time + TICKS(0.3);
}

/* trap_spikeshooter: the level records named `name` fire */
GCODE void g_use_entities(int name)
{
    int i;
    for (i = 0; i < nlents; i++) {
        const lent_t *l = &lents[i];
        fixed_t org[3], vel[3];
        if (l->targetname != name || l->cls != CL_TRAP_SPIKESHOOTER)
            continue;
        org[0] = FX(l->org[0]);
        org[1] = FX(l->org[1]);
        org[2] = FX(l->org[2]);
        if (l->angle == -1) { vel[0] = vel[1] = 0; vel[2] = FX(500); }
        else if (l->angle == -2) { vel[0] = vel[1] = 0; vel[2] = -FX(500); }
        else {
            vel[0] = fcos(ANG(l->angle)) * 500;
            vel[1] = fsin(ANG(l->angle)) * 500;
            vel[2] = 0;
        }
        snd_start(SND_SPIKE2, org, 255);
        g_missile(MK_TRAPSPIKE, org, vel, OWN_WORLD);
    }
}

/* ------------------------------------------------------------ the player's weapons */

/* PF_aim: Quake's vertical auto-aim. If a monster is within about 22
 * degrees of the crosshair and in sight, the shot is tilted up or down to
 * its centre (the horizontal aim is the player's own) */
GCODE void g_aim(const fixed_t src[3], fixed_t fwd[3])
{
    int i, k, best = -1;
    fixed_t bestdot = 0xEE14;                   /* sv_aim 0.93 */
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *e = &ents[i];
        fixed_t c[3], d[3], dt;
        trace_t tr;
        if ((e->type != ET_MONSTER || e->health <= 0 || (e->flags & EF_NOTSOLID)) && e->type != ET_BOX)
            continue;
        for (k = 0; k < 3; k++) {
            c[k] = e->org[k] + FX(e->mins[k] + e->maxs[k]) / 2;
            d[k] = c[k] - src[k];
        }
        if (vlen_units(c, src) > 2048)
            continue;
        g_normalize(d);
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
            d[k] = e->org[k] + FX(e->mins[k] + e->maxs[k]) / 2 - src[k];
        dist = dot16(d, fwd);
        fwd[0] = fixmul(fwd[0], dist);
        fwd[1] = fixmul(fwd[1], dist);
        fwd[2] = d[2];
        g_normalize(fwd);
    }
}

static void eye_pos(fixed_t e[3])
{
    e[0] = player.pos[0];
    e[1] = player.pos[1];
    e[2] = player.pos[2] + FX(22);
}

/* would a shot now (straight down the auto-aimed line) hit this monster
 * before a wall? (the demo bot only fires when it would) */
GCODE int g_shot_would_hit(const ent_t *e)
{
    fixed_t src[3], fwd[3], right[3], up[3], end[3], mn[3], mx[3], t;
    trace_t tr;
    int k;
    eye_pos(src);
    g_view_vectors(fwd, right, up);
    g_aim(src, fwd);
    for (k = 0; k < 3; k++)
        end[k] = src[k] + fwd[k] * 2048;
    ent_box(e, mn, mx);
    g_trace(0, src, end, &tr);
    return g_ray_box(src, fwd, tr.frac * 2048, mn, mx, &t);
}

/* FireBullets: shotgun pellets against the world and the monsters; the
 * damage each thing takes is added up first (multi-damage) */
static void fire_bullets(int count, fixed_t sx, fixed_t sy_)
{
    fixed_t src[3], fwd[3], right[3], up[3];
    int16_t hits[MAX_ENTS];
    int i, k, n;
    memset(hits, 0, sizeof(hits));
    eye_pos(src);
    g_view_vectors(fwd, right, up);
    g_aim(src, fwd);
    for (n = 0; n < count; n++) {
        fixed_t d[3], end[3], wd, best;
        ent_t *hit = NULL;
        trace_t tr;
        fixed_t cr = fixmul(g_crand(), sx), cu = fixmul(g_crand(), sy_);
        for (k = 0; k < 3; k++)
            d[k] = fwd[k] + fixmul(cr, right[k]) + fixmul(cu, up[k]);
        g_normalize(d);
        for (k = 0; k < 3; k++)
            end[k] = src[k] + d[k] * 2048;
        g_trace(0, src, end, &tr);
        wd = tr.frac * 2048;                        /* units, 16.16 */
        best = wd;
        mv_shoot_triggers(src, d, wd);              /* shootable switches */
        for (i = 0; i < MAX_ENTS; i++) {
            ent_t *e = &ents[i];
            fixed_t mn[3], mx[3], t;
            if ((e->type != ET_MONSTER || e->health <= 0 || (e->flags & EF_NOTSOLID)) && e->type != ET_BOX)
                continue;
            ent_box(e, mn, mx);
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
                if (hit->type == ET_MONSTER)
                    g_particles(at, NULL, 73, 6);       /* blood */
                else
                    g_particles(at, NULL, 0, 4);
            } else if (tr.frac < 0x10000) {
                g_particles(at, NULL, 0, 4);            /* puff */
                if (tr.mover >= 0)
                    mv_damage(tr.mover, 4);             /* secret doors open when shot */
            }
        }
    }
    for (i = 0; i < MAX_ENTS; i++)
        if (hits[i]) {
            if (ents[i].type == ET_BOX)
                g_box_damage(&ents[i], hits[i]);
            else
                g_damage(&ents[i], hits[i]);
        }
}

/* W_FireAxe: 64 units in front, 20 points */
static void fire_axe(void)
{
    fixed_t src[3], fwd[3], right[3], up[3], end[3], best, at[3];
    trace_t tr;
    ent_t *hit = NULL;
    int i, k;
    src[0] = player.pos[0];
    src[1] = player.pos[1];
    src[2] = player.pos[2] + FX(16);
    g_view_vectors(fwd, right, up);
    for (k = 0; k < 3; k++)
        end[k] = src[k] + fwd[k] * 64;
    g_trace(0, src, end, &tr);
    best = tr.frac * 64;
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        fixed_t mn[3], mx[3], t;
        if ((e->type != ET_MONSTER || e->health <= 0 || (e->flags & EF_NOTSOLID)) && e->type != ET_BOX)
            continue;
        ent_box(e, mn, mx);
        if (g_ray_box(src, fwd, best, mn, mx, &t) && t < best) {
            best = t;
            hit = e;
        }
    }
    if (!hit && tr.frac == 0x10000)
        return;
    for (k = 0; k < 3; k++)
        at[k] = src[k] + fixmul(fwd[k], best - FX(4));
    if (hit) {
        if (hit->type == ET_BOX)
            g_box_damage(hit, 20);
        else {
            g_particles(at, NULL, 73, 20);
            g_damage(hit, 20);
        }
    } else {
        snd_start(SND_PLAYER_AXHIT2, at, 255);
        g_particles(at, NULL, 0, 20);
        if (tr.mover >= 0)
            mv_damage(tr.mover, 20);
    }
}

/* W_FireSpikes: one nail, from the right or the left barrel */
static void fire_nail(int side)
{
    fixed_t src[3], fwd[3], right[3], up[3], vel[3];
    int k;
    if (ps.nails < 1) {
        ps.weapon = g_best_weapon();
        ps.weaponframe = 0;
        return;
    }
    snd_start(SND_ROCKET1I, NULL, 255);
    ps.nails--;
    ps.attack_finished = level_time + TICKS(0.2);
    g_view_vectors(fwd, right, up);
    src[0] = player.pos[0];
    src[1] = player.pos[1];
    src[2] = player.pos[2] + FX(16);
    g_aim(src, fwd);
    for (k = 0; k < 3; k++) {
        src[k] += right[k] * side;
        vel[k] = fwd[k] * 1000;
    }
    g_missile(MK_NAIL, src, vel, OWN_PLAYER);
}

/* W_FireGrenade */
static void fire_grenade(void)
{
    fixed_t src[3], fwd[3], right[3], up[3], vel[3];
    int k;
    ps.rockets--;
    snd_start(SND_GRENADE, NULL, 255);
    g_view_vectors(fwd, right, up);
    src[0] = player.pos[0];
    src[1] = player.pos[1];
    src[2] = player.pos[2];
    if (player.pitch) {
        fixed_t cr = g_crand() * 10, cu = g_crand() * 10;
        for (k = 0; k < 3; k++)
            vel[k] = fwd[k] * 600 + up[k] * 200 + fixmul(cr, right[k]) + fixmul(cu, up[k]);
    } else {
        fixed_t s[3];
        eye_pos(s);
        g_aim(s, fwd);
        for (k = 0; k < 3; k++)
            vel[k] = fwd[k] * 600;
        vel[2] = FX(200);
    }
    g_missile(MK_GRENADE, src, vel, OWN_PLAYER);
}

/* W_BestWeapon: no grenade launcher, as in Quake */
GCODE int g_best_weapon(void)
{
    if (ps.nails >= 1 && (ps.weapons & IT_NAILGUN))
        return IT_NAILGUN;
    if (ps.shells >= 2 && (ps.weapons & IT_SUPER_SHOTGUN))
        return IT_SUPER_SHOTGUN;
    if (ps.shells >= 1 && (ps.weapons & IT_SHOTGUN))
        return IT_SHOTGUN;
    return IT_AXE;
}

static int has_ammo(int w)
{
    switch (w) {
    case IT_SHOTGUN: return ps.shells >= 1;
    case IT_SUPER_SHOTGUN: return ps.shells >= 2;
    case IT_NAILGUN: return ps.nails >= 1;
    case IT_GRENADE_LAUNCHER: return ps.rockets >= 1;
    default: return 1;
    }
}

/* CycleWeaponCommand: the next weapon the player has, with ammunition */
GCODE void g_next_weapon(void)
{
    static const uint16_t order[] = { IT_AXE, IT_SHOTGUN, IT_SUPER_SHOTGUN, IT_NAILGUN, IT_GRENADE_LAUNCHER };
    int i, cur = 0, k;
    for (i = 0; i < 5; i++)
        if (order[i] == ps.weapon)
            cur = i;
    for (k = 1; k <= 5; k++) {
        int w = order[(cur + k) % 5];
        if ((ps.weapons & w) && has_ammo(w)) {
            ps.weapon = w;
            ps.weaponframe = 0;
            ps.attack_finished = level_time;
            return;
        }
    }
}

/* W_Attack */
static void attack(void)
{
    if (!has_ammo(ps.weapon)) {
        ps.weapon = g_best_weapon();
        ps.weaponframe = 0;
        return;
    }
    ps.weapon_tick = level_time;
    switch (ps.weapon) {
    case IT_AXE:
        snd_start(SND_AX1, NULL, 255);
        ps.weaponframe = 1;     /* (one swing: the second, frames 5-8, is not on the cartridge) */
        ps.attack_finished = level_time + TICKS(0.5);
        break;
    case IT_SHOTGUN:
        ps.shells -= 1;
        snd_start(SND_GUNCOCK, NULL, 255);
        fire_bullets(6, 0xA3D, 0xA3D);             /* spread 0.04 */
        ps.attack_finished = level_time + TICKS(0.5);
        ps.weaponframe = 1;
        break;
    case IT_SUPER_SHOTGUN:
        ps.shells -= 2;
        snd_start(SND_SHOTGN2, NULL, 255);
        fire_bullets(14, 0x23D7, 0x147A);          /* spread 0.14, 0.08 */
        ps.attack_finished = level_time + TICKS(0.7);
        ps.weaponframe = 1;
        break;
    case IT_NAILGUN:
        ps.weaponframe = 1;
        ps.nailside ^= 1;
        fire_nail(ps.nailside ? 4 : -4);
        break;
    case IT_GRENADE_LAUNCHER:
        fire_grenade();
        ps.attack_finished = level_time + TICKS(0.6);
        ps.weaponframe = 1;
        break;
    }
    if (ps.quad_finished > level_time && ps.weapon != IT_AXE)
        snd_start(SND_DAMAGE3, NULL, 255);
}

/* the weapon each tick: firing, and the view weapon's frames at 10 Hz
 * (player.qc's player_axe, player_shot, player_nail, player_rocket) */
GCODE void g_weapon_tick(int buttons, int pressed)
{
    int fire = (buttons & ACT_FIRE) && !(!cfg.bind[BIND_WEAPON] && (buttons & ACT_STRAFE)) && !cfg.fly;
    if ((pressed & ACT_WEAPON) || (!cfg.bind[BIND_WEAPON] && (pressed & ACT_FIRE) && (buttons & ACT_STRAFE))) {
        g_next_weapon();            /* its own button, or strafe + fire */
        return;
    }
    if (ps.weaponframe && level_time - ps.weapon_tick >= 6) {
        ps.weapon_tick = level_time;
        switch (ps.weapon) {
        case IT_AXE:
            ps.weaponframe++;
            if (ps.weaponframe == 3 || ps.weaponframe == 7)
                fire_axe();
            if (ps.weaponframe == 5 || ps.weaponframe == 9)
                ps.weaponframe = 0;
            break;
        case IT_NAILGUN:
            /* player_nail: a nail every tenth of a second while held */
            if (!fire) {
                ps.weaponframe = 0;
                break;
            }
            if (++ps.weaponframe == 9)
                ps.weaponframe = 1;
            ps.nailside ^= 1;
            fire_nail(ps.nailside ? 4 : -4);
            if (ps.quad_finished > level_time)
                snd_start(SND_DAMAGE3, NULL, 255);
            break;
        default:
            if (++ps.weaponframe > 6)
                ps.weaponframe = 0;
        }
    }
    if (fire && level_time >= ps.attack_finished && !(ps.weapon == IT_NAILGUN && ps.weaponframe))
        attack();
}

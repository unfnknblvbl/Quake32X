/* Monsters: the grunt, rottweiler, ogre, knight, fiend, scrag and zombie,
 * after id's QuakeC (soldier.qc, dog.qc, ogre.qc, knight.qc, demon.qc,
 * wizard.qc, zombie.qc, ai.qc, fight.qc) and the engine's monster movement
 * (sv_move.c). They think at 10 Hz: each think is one frame of an
 * animation, which says how the monster moves on that frame (its AI kind
 * and distance) and what it does (an event: a shot, a bite, a sound). */
#include "g_local.h"
#include "g_frames.h"

#define STEPSIZE   18
#define EYE_OFS    25

/* what a monster does on every frame of an animation */
enum {
    K_NONE,
    K_STAND,                        /* ai_stand: look for the player */
    K_RUN,                          /* ai_run(dist): chase, attack */
    K_FACE,                         /* ai_face */
    K_CHARGE,                       /* ai_charge(dist): face and close in */
    K_CHARGE_SIDE,                  /* ai_charge_side: swing past the player's left */
    K_MOVE,                         /* walk dist forward (ideal yaw), or back if negative */
};

/* what it does on one frame (anim_t.ev) */
enum {
    EV_NONE,
    EV_IDLE,                        /* the class's idle sound, sometimes */
    EV_ARMY_FIRE, EV_DOG_BITE, EV_DOG_LEAP, EV_DROP,
    EV_SAW_SND, EV_SAW, EV_OGRE_GRENADE,
    EV_SWORD_SND, EV_SWORD1_SND, EV_KNIGHT_MELEE,
    EV_DEMON_LEAP, EV_DEMON_MELEE_R, EV_DEMON_MELEE_L, EV_DEMON_DIE,
    EV_WIZ_START, EV_WIZ_FIRE_L, EV_WIZ_FIRE_R, EV_WIZ_DONE, EV_WIZ_DIE,
    EV_ZOM_RUN, EV_ZOM_IDLE, EV_ZOM_THROW_A, EV_ZOM_THROW_B, EV_ZOM_THROW_C,
    EV_ZOM_PAIN, EV_ZOM_PAIN1, EV_ZOM_FALL, EV_ZOM_DOWN, EV_ZOM_WAIT, EV_ZOM_UP,
    EV_NOTSOLID,
};

#define HOLD 255

typedef struct anim_s {
    uint8_t first, len, next, kind;
    const uint8_t *frames;          /* NULL: first, first + 1 ... */
    int8_t  dist[32];               /* per frame */
    uint8_t ev[32];                 /* per frame */
} anim_t;

enum {
    A_STAND, A_RUN, A_ATTACK, A_ATTACK2, A_ATTACK3, A_MELEE, A_MELEE2,
    A_PAIN1, A_PAIN2, A_PAIN3, A_PAIN4, A_PAIN5, A_DIE1, A_DIE2, A_SIDE, A_LAND, A_NUM
};

#define D(...) { __VA_ARGS__ }
#define E(...) { __VA_ARGS__ }
#define F(...) (const uint8_t[]){ __VA_ARGS__ }
#define NONE { 0 }
#define AN(fr, n, next, kind, dist, ev) { fr, n, next, kind, NULL, dist, ev }

static const anim_t army_anims[A_NUM] = {
    [A_STAND]  = AN(FR_SOLDIER_STAND, 8, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_SOLDIER_RUN, 8, A_RUN, K_RUN, D(11, 15, 10, 10, 8, 15, 10, 8), E(EV_IDLE)),
    [A_ATTACK] = AN(FR_SOLDIER_SHOOT, 9, A_RUN, K_FACE, NONE, E(0, 0, 0, 0, EV_ARMY_FIRE)),
    [A_PAIN1]  = AN(FR_SOLDIER_PAIN, 6, A_RUN, K_MOVE, D(0, 0, 0, 0, 0, -1), NONE),
    [A_PAIN2]  = AN(FR_SOLDIER_PAINB, 14, A_RUN, K_MOVE, D(0, 13, 9, 0, 0, 0, 0, 0, 0, 0, 0, -2, 0, 0), NONE),
    [A_PAIN3]  = AN(FR_SOLDIER_PAINC, 13, A_RUN, K_MOVE, D(0, -1, 0, 0, 1, 1, 0, -1, 4, 3, 6, 8, 0), NONE),
    [A_DIE1]   = AN(FR_SOLDIER_DEATH, 10, HOLD, K_NONE, NONE, E(0, 0, EV_DROP)),
    [A_DIE2]   = AN(FR_SOLDIER_DEATHC, 11, HOLD, K_MOVE, D(0, -5, -4, -13, -3, -4), E(0, 0, EV_DROP)),
};

static const anim_t dog_anims[A_NUM] = {
    [A_STAND]  = AN(FR_DOG_STAND, 9, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_DOG_RUN, 12, A_RUN, K_RUN, D(16, 32, 32, 20, 64, 32, 16, 32, 32, 20, 64, 32), E(EV_IDLE)),
    [A_ATTACK] = AN(FR_DOG_LEAP, 9, HOLD, K_FACE, NONE, E(0, EV_DOG_LEAP)),
    [A_MELEE]  = AN(FR_DOG_ATTACK, 8, A_RUN, K_CHARGE, D(10, 10, 10, 10, 10, 10, 10, 10), E(0, 0, 0, EV_DOG_BITE)),
    [A_PAIN1]  = AN(FR_DOG_PAIN, 6, A_RUN, K_NONE, NONE, NONE),
    [A_PAIN2]  = AN(FR_DOG_PAINB, 16, A_RUN, K_MOVE, D(0, 0, -4, -12, -12, -2, 0, -4, 0, -10), NONE),
    [A_DIE1]   = AN(FR_DOG_DEATH, 9, HOLD, K_NONE, NONE, NONE),
    [A_DIE2]   = AN(FR_DOG_DEATHB, 9, HOLD, K_NONE, NONE, NONE),
};

static const anim_t ogre_anims[A_NUM] = {
    [A_STAND]  = AN(FR_OGRE_STAND, 9, A_STAND, K_STAND, NONE, E(0, 0, 0, 0, EV_IDLE)),
    [A_RUN]    = AN(FR_OGRE_RUN, 8, A_RUN, K_RUN, D(9, 12, 8, 22, 16, 4, 13, 24), E(EV_IDLE)),
    [A_ATTACK] = { FR_OGRE_SHOOT, 7, A_RUN, K_FACE,
                   F(FR_OGRE_SHOOT, FR_OGRE_SHOOT + 1, FR_OGRE_SHOOT + 1, FR_OGRE_SHOOT + 2,
                     FR_OGRE_SHOOT + 3, FR_OGRE_SHOOT + 4, FR_OGRE_SHOOT + 5),
                   NONE, E(0, 0, 0, EV_OGRE_GRENADE) },
    /* the swing and the smash: chainsaw() closes 10 more on its frames */
    [A_MELEE]  = AN(FR_OGRE_SWING, 14, A_RUN, K_CHARGE, D(11, 1, 4, 13, 19, 10, 10, 10, 10, 10, 10, 3, 8, 9),
                    E(EV_SAW_SND, 0, 0, 0, EV_SAW, EV_SAW, EV_SAW, EV_SAW, EV_SAW, EV_SAW, EV_SAW)),
    [A_MELEE2] = AN(FR_OGRE_SMASH, 14, A_RUN, K_CHARGE, D(6, 0, 0, 1, 4, 14, 14, 20, 23, 10, 12, 0, 4, 12),
                    E(EV_SAW_SND, 0, 0, 0, 0, EV_SAW, EV_SAW, EV_SAW, EV_SAW, EV_SAW, EV_SAW)),
    [A_PAIN1]  = AN(FR_OGRE_PAIN, 5, A_RUN, K_NONE, NONE, NONE),
    [A_PAIN2]  = AN(FR_OGRE_PAINB, 3, A_RUN, K_NONE, NONE, NONE),
    [A_PAIN3]  = AN(FR_OGRE_PAINC, 6, A_RUN, K_NONE, NONE, NONE),
    [A_PAIN4]  = AN(FR_OGRE_PAIND, 16, A_RUN, K_MOVE, D(0, -10, -9, -4), NONE),
    [A_PAIN5]  = AN(FR_OGRE_PAINE, 15, A_RUN, K_MOVE, D(0, -10, -9, -4), NONE),
    [A_DIE1]   = AN(FR_OGRE_DEATH, 14, HOLD, K_NONE, NONE, E(0, 0, EV_DROP)),
    [A_DIE2]   = AN(FR_OGRE_BDEATH, 10, HOLD, K_MOVE, D(0, 5, 0, 1, 3, 7, 25), E(0, 0, EV_DROP)),
};

static const anim_t knight_anims[A_NUM] = {
    [A_STAND]  = AN(FR_KNIGHT_STAND, 9, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_KNIGHT_RUNB, 8, A_RUN, K_RUN, D(16, 20, 13, 7, 16, 20, 14, 6), E(EV_IDLE)),
    /* knight_runatk: a swing in passing; knight_atk: standing */
    [A_MELEE]  = AN(FR_KNIGHT_RUNATTACK, 11, A_RUN, K_CHARGE_SIDE, NONE,
                    E(EV_SWORD_SND, 0, 0, 0, EV_KNIGHT_MELEE, EV_KNIGHT_MELEE, EV_KNIGHT_MELEE,
                      EV_KNIGHT_MELEE, EV_KNIGHT_MELEE)),
    [A_MELEE2] = AN(FR_KNIGHT_ATTACKB, 10, A_RUN, K_CHARGE, D(0, 7, 4, 0, 3, 4, 1, 3, 1, 5),
                    E(EV_SWORD1_SND, 0, 0, 0, 0, EV_KNIGHT_MELEE, EV_KNIGHT_MELEE, EV_KNIGHT_MELEE)),
    [A_PAIN1]  = AN(FR_KNIGHT_PAIN, 3, A_RUN, K_NONE, NONE, NONE),
    [A_PAIN2]  = AN(FR_KNIGHT_PAINB, 11, A_RUN, K_MOVE, D(0, 3, 0, 0, 2, 4, 2, 5, 5, 0, 0), NONE),
    [A_DIE1]   = AN(FR_KNIGHT_DEATH, 10, HOLD, K_NONE, NONE, NONE),
    [A_DIE2]   = AN(FR_KNIGHT_DEATHB, 11, HOLD, K_NONE, NONE, NONE),
};

static const anim_t demon_anims[A_NUM] = {
    [A_STAND]  = AN(FR_DEMON_STAND, 13, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_DEMON_RUN, 6, A_RUN, K_RUN, D(20, 15, 36, 20, 15, 36), E(EV_IDLE)),
    /* the leap: in the air from its fourth frame until it lands (A_LAND) */
    [A_ATTACK] = AN(FR_DEMON_LEAP, 10, HOLD, K_FACE, NONE, E(0, 0, 0, EV_DEMON_LEAP)),
    [A_LAND]   = AN(FR_DEMON_LEAP + 10, 2, A_RUN, K_NONE, NONE, NONE),
    [A_MELEE]  = AN(FR_DEMON_ATTACKA, 15, A_RUN, K_CHARGE, D(4, 0, 0, 1, 2, 1, 6, 8, 4, 2, 0, 5, 8, 4, 4),
                    E(0, 0, 0, 0, EV_DEMON_MELEE_R, 0, 0, 0, 0, 0, EV_DEMON_MELEE_L)),
    [A_PAIN1]  = AN(FR_DEMON_PAIN, 6, A_RUN, K_NONE, NONE, NONE),
    [A_DIE1]   = AN(FR_DEMON_DEATH, 9, HOLD, K_NONE, NONE, E(EV_DEMON_DIE)),
};

static const anim_t wizard_anims[A_NUM] = {
    [A_STAND]  = AN(FR_WIZARD_HOVER, 8, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_WIZARD_FLY, 14, A_RUN, K_RUN,
                    D(16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16), E(EV_IDLE)),
    [A_SIDE]   = AN(FR_WIZARD_HOVER, 8, A_SIDE, K_RUN, D(8, 8, 8, 8, 8, 8, 8, 8), E(EV_IDLE)),
    /* two spits: the left one after 0.3 s, the right after 0.8 s */
    [A_ATTACK] = { FR_WIZARD_MAGATT, 10, A_RUN, K_FACE, F(FR_WIZARD_MAGATT, FR_WIZARD_MAGATT + 1, FR_WIZARD_MAGATT + 2, FR_WIZARD_MAGATT + 3,
                     FR_WIZARD_MAGATT + 4, FR_WIZARD_MAGATT + 5, FR_WIZARD_MAGATT + 4,
                     FR_WIZARD_MAGATT + 3, FR_WIZARD_MAGATT + 2, FR_WIZARD_MAGATT + 1), NONE, E(EV_WIZ_START, 0, 0, EV_WIZ_FIRE_L, 0, 0, 0, 0, EV_WIZ_FIRE_R, EV_WIZ_DONE) },
    [A_PAIN1]  = AN(FR_WIZARD_PAIN, 4, A_RUN, K_NONE, NONE, NONE),
    [A_DIE1]   = AN(FR_WIZARD_DEATH, 8, HOLD, K_NONE, NONE, E(EV_WIZ_DIE)),
};

static const anim_t zombie_anims[A_NUM] = {
    [A_STAND]  = AN(FR_ZOMBIE_STAND, 15, A_STAND, K_STAND, NONE, NONE),
    [A_RUN]    = AN(FR_ZOMBIE_RUN, 18, A_RUN, K_RUN, D(1, 1, 0, 1, 2, 3, 4, 4, 2, 0, 0, 0, 2, 4, 6, 7, 3, 8),
                    E(EV_ZOM_RUN, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_IDLE)),
    [A_ATTACK] = AN(FR_ZOMBIE_ATTA, 13, A_RUN, K_FACE, NONE,
                    E(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_THROW_A)),
    [A_ATTACK2] = { FR_ZOMBIE_ATTB, 14, A_RUN, K_FACE, F(FR_ZOMBIE_ATTB, FR_ZOMBIE_ATTB + 1, FR_ZOMBIE_ATTB + 2, FR_ZOMBIE_ATTB + 3,
                      FR_ZOMBIE_ATTB + 4, FR_ZOMBIE_ATTB + 5, FR_ZOMBIE_ATTB + 6, FR_ZOMBIE_ATTB + 7,
                      FR_ZOMBIE_ATTB + 8, FR_ZOMBIE_ATTB + 9, FR_ZOMBIE_ATTB + 10, FR_ZOMBIE_ATTB + 11,
                      FR_ZOMBIE_ATTB + 12, FR_ZOMBIE_ATTB + 12), NONE, E(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_THROW_B) },
    [A_ATTACK3] = AN(FR_ZOMBIE_ATTC, 12, A_RUN, K_FACE, NONE,
                     E(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_THROW_C)),
    [A_PAIN1]  = AN(FR_ZOMBIE_PAINA, 12, A_RUN, K_MOVE, D(0, 3, 1, -1, -3, -1), E(EV_ZOM_PAIN)),
    [A_PAIN2]  = AN(FR_ZOMBIE_PAINB, 28, A_RUN, K_MOVE,
                    D(0, -2, -8, -6, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1),
                    E(EV_ZOM_PAIN1, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_FALL)),
    [A_PAIN3]  = AN(FR_ZOMBIE_PAINC, 18, A_RUN, K_MOVE, D(0, 0, -3, -1, 0, 0, 0, 0, 0, 0, 1, 1), E(EV_ZOM_PAIN1)),
    [A_PAIN4]  = AN(FR_ZOMBIE_PAIND, 13, A_RUN, K_MOVE, D(0, 0, 0, 0, 0, 0, 0, 0, -1), E(EV_ZOM_PAIN)),
    /* down on the ground for five seconds, then up again */
    [A_PAIN5]  = AN(FR_ZOMBIE_PAINE, 30, A_RUN, K_MOVE,
                    D(0, -8, -5, -3, -1, -2, -1, -1, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 3, 1, -1),
                    E(EV_ZOM_PAIN, 0, 0, 0, 0, 0, 0, 0, 0, EV_ZOM_DOWN, EV_ZOM_WAIT, EV_ZOM_UP)),
};

/* the classes, in CL_ order from CL_ARMY */
typedef struct {
    uint8_t  model, head, hull, flags;
    int16_t  health, gib_health;    /* gibbed when health falls below gib_health */
    int8_t   size, height;          /* half width; maxs z */
    int16_t  sight, idle, idle_chance, pain, death;
    uint8_t  gib[3];
    const anim_t *anims;
} mclass_t;

#define MC_FLY 1

static const mclass_t mclasses[] = {
    /* army */   { MDL_SOLDIER, MDL_H_GUARD, 1, 0, 30, -35, 16, 40, SND_SOLDIER_SIGHT1, SND_SOLDIER_IDLE, 6554,
                   SND_SOLDIER_PAIN1, SND_SOLDIER_DEATH1, { MDL_GIB1, MDL_GIB2, MDL_GIB3 }, army_anims },
    /* dog */    { MDL_DOG, MDL_H_DOG, 2, 0, 25, -35, 32, 40, SND_DOG_DSIGHT, SND_DOG_IDLE, 6554,
                   SND_DOG_DPAIN1, SND_DOG_DDEATH, { MDL_GIB3, MDL_GIB3, MDL_GIB3 }, dog_anims },
    /* ogre */   { MDL_OGRE, MDL_H_OGRE, 2, 0, 200, -80, 32, 64, SND_OGRE_OGWAKE, SND_OGRE_OGIDLE2, 6554,
                   SND_OGRE_OGPAIN1, SND_OGRE_OGDTH, { MDL_GIB3, MDL_GIB3, MDL_GIB3 }, ogre_anims },
    /* knight */ { MDL_KNIGHT, MDL_H_KNIGHT, 1, 0, 75, -40, 16, 40, SND_KNIGHT_KSIGHT, SND_KNIGHT_IDLE, 6554,
                   SND_KNIGHT_KHURT, SND_KNIGHT_KDEATH, { MDL_GIB1, MDL_GIB2, MDL_GIB3 }, knight_anims },
    /* demon */  { MDL_DEMON, MDL_H_DEMON, 2, 0, 300, -80, 32, 64, SND_DEMON_SIGHT2, SND_DEMON_IDLE1, 6554,
                   SND_DEMON_DPAIN1, SND_DEMON_DDEATH, { MDL_GIB1, MDL_GIB1, MDL_GIB1 }, demon_anims },
    /* wizard */ { MDL_WIZARD, MDL_H_WIZARD, 1, MC_FLY, 80, -40, 16, 40, SND_WIZARD_WSIGHT, SND_WIZARD_WIDLE1, 0,
                   SND_WIZARD_WPAIN, SND_WIZARD_WDEATH, { MDL_GIB2, MDL_GIB2, MDL_GIB2 }, wizard_anims },
    /* zombie */ { MDL_ZOMBIE, MDL_H_ZOMBIE, 1, 0, 60, 1000, 16, 40, SND_ZOMBIE_Z_IDLE, SND_ZOMBIE_Z_IDLE, 6554,
                   SND_ZOMBIE_Z_PAIN, SND_ZOMBIE_Z_GIB, { MDL_GIB1, MDL_GIB2, MDL_GIB3 }, zombie_anims },
};

static inline const mclass_t *mc(const ent_t *e)
{
    return &mclasses[e->cls - CL_ARMY];
}

static int anim_changed;

static void set_anim(ent_t *e, int a)
{
    e->anim = &mc(e)->anims[a];
    e->animpos = 0;
    e->frame = e->anim->frames ? e->anim->frames[0] : e->anim->first;
    anim_changed = 1;
}

static int anim_is(const ent_t *e, int a)
{
    return e->anim == &mc(e)->anims[a];
}

GCODE int m_hull(const ent_t *e)
{
    if (e->cls < CL_ARMY || e->cls > CL_ZOMBIE)
        return 1;
    return mc(e)->hull;
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

static void voice(const ent_t *e, int snd, int att)
{
    fixed_t c[3];
    eye(e, c);
    snd_play(snd, c, 255, att);
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

static uint16_t yaw_to_player(const ent_t *e)
{
    return vectoyaw(player.pos[0] - e->org[0], player.pos[1] - e->org[1]);
}

static void hunt_target(ent_t *e)
{
    e->hunting = 1;
    e->ideal_yaw = yaw_to_player(e);
    e->attack_finished = level_time + TICKS(1);
    set_anim(e, A_RUN);
}

static void found_target(ent_t *e)
{
    voice(e, mc(e)->sight, ATTN_NORM);
    hunt_target(e);
}

/* FindTarget: the player in range and in sight (and in front, unless very
 * close); not while he is invisible */
static int find_target(ent_t *e)
{
    int r;
    if (ps.dead || !LEAF_VISIBLE(e->leaf) || ps.invis_finished > level_time)
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

static int blocked_by_others(const ent_t *e, const fixed_t at[3])
{
    int i, nx = UNITS(at[0]), ny = UNITS(at[1]);
    int r = e->maxs[0];
    int px = UNITS(player.pos[0]), py = UNITS(player.pos[1]), pz = UNITS(player.pos[2]);
    int ez = UNITS(at[2]);
    if (!ps.dead && nx - r < px + 16 && nx + r > px - 16 && ny - r < py + 16 && ny + r > py - 16
        && ez + e->mins[2] < pz + 32 && ez + e->maxs[2] > pz - 24)
        return 1;
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *o = &ents[i];
        int ox, oy, orr;
        if (o == e || (o->type != ET_MONSTER && o->type != ET_BOX) || (o->flags & EF_NOTSOLID))
            continue;
        ox = UNITS(o->org[0]) + ((o->mins[0] + o->maxs[0]) >> 1);
        oy = UNITS(o->org[1]) + ((o->mins[1] + o->maxs[1]) >> 1);
        orr = (o->maxs[0] - o->mins[0]) >> 1;
        if (nx - r < ox + orr && nx + r > ox - orr && ny - r < oy + orr && ny + r > oy - orr) {
            int oz = UNITS(o->org[2]);
            if (ez + e->mins[2] < oz + o->maxs[2] && ez + e->maxs[2] > oz + o->mins[2])
                return 1;
        }
    }
    return 0;
}

/* SV_movestep: move by (dx, dy), stepping up and down stairs; monsters do
 * not walk off ledges. Flyers rise or sink toward the player as they go */
static int movestep(ent_t *e, fixed_t dx, fixed_t dy)
{
    fixed_t s[3], end[3];
    trace_t tr;
    int hull = m_hull(e);
    if (mc(e)->flags & MC_FLY) {
        int i;
        for (i = 0; i < 2; i++) {
            fixed_t dz = 0;
            end[0] = e->org[0] + dx;
            end[1] = e->org[1] + dy;
            end[2] = e->org[2];
            if (i == 0 && e->hunting) {
                int d = UNITS(e->org[2] - player.pos[2]);
                if (d > 40) dz = -FX(8);
                if (d < 30) dz = FX(8);
                end[2] += dz;
            }
            g_trace(hull, e->org, end, &tr);
            if (tr.frac == 0x10000 && !tr.startsolid && !blocked_by_others(e, end)) {
                e->org[0] = end[0];
                e->org[1] = end[1];
                e->org[2] = end[2];
                return 1;
            }
            if (!e->hunting)
                break;
        }
        return 0;
    }
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
    if (blocked_by_others(e, tr.end))
        return 0;
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
    if (!e->onground && !(mc(e)->flags & MC_FLY))
        return;
    if (close_enough(e, dist))
        return;
    if ((g_rand() & 3) == 1 || !step_direction(e, e->ideal_yaw, dist))
        new_chase_dir(e, dist);
}

static void ai_face(ent_t *e)
{
    e->ideal_yaw = yaw_to_player(e);
    change_yaw(e);
}

/* ai_charge_side: swing past the player's left */
static void ai_charge_side(ent_t *e)
{
    fixed_t t[3];
    e->ideal_yaw = yaw_to_player(e);
    change_yaw(e);
    /* the player's position, 30 units to the knight's left */
    t[0] = player.pos[0] - fsin(e->yaw) * 30;
    t[1] = player.pos[1] + fcos(e->yaw) * 30;
    walkmove(e, vectoyaw(t[0] - e->org[0], t[1] - e->org[1]), 20);
}

/* ------------------------------------------------------------ attacks */

static void hurt_player(ent_t *e, int dmg)
{
    if (dmg > 0)
        g_damage_player(dmg, e->org);
}

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
    hurt_player(e, dmg);
}

static int dist_to_player(const ent_t *e)
{
    return vlen_units(e->org, player.pos);
}

static void dog_bite(ent_t *e)
{
    voice(e, SND_DOG_DATTACK1, ATTN_NORM);
    if (!can_see_player(e) || dist_to_player(e) > 100)
        return;
    hurt_player(e, ((g_rand() + g_rand() + g_rand()) * 8) >> 15);
}

static void leap(ent_t *e, int fwd, int up)
{
    e->vel[0] = fcos(e->yaw) * fwd;
    e->vel[1] = fsin(e->yaw) * fwd;
    e->vel[2] = FX(up);
    e->org[2] += FX(1);
    e->onground = 0;
    e->leaping = 1;
    e->died = level_time;           /* (when it set off: the fiend tries again after 3 s) */
}

/* chainsaw(): close in, then 4 x (0..3) points within 100 units */
static void chainsaw(ent_t *e)
{
    fixed_t c[3];
    if (!can_see_player(e) || dist_to_player(e) > 100)
        return;
    hurt_player(e, ((g_rand() + g_rand() + g_rand()) * 4) >> 15);
    c[0] = e->org[0] + fcos(e->yaw) * 16;
    c[1] = e->org[1] + fsin(e->yaw) * 16;
    c[2] = e->org[2] + FX(16);
    g_particles(c, NULL, 73, 10);
}

/* OgreFireGrenade */
static void ogre_grenade(ent_t *e)
{
    fixed_t v[3];
    snd_start(SND_GRENADE, e->org, 255);
    v[0] = player.pos[0] - e->org[0];
    v[1] = player.pos[1] - e->org[1];
    v[2] = player.pos[2] - e->org[2];
    g_normalize(v);
    v[0] *= 600;
    v[1] *= 600;
    v[2] = FX(200);
    g_missile(MK_OGRE_GRENADE, e->org, v, e - ents);
}

/* ai_melee: (0..3) x 3 points within 60 units */
static void knight_melee(ent_t *e)
{
    if (dist_to_player(e) > 60 || !can_see_player(e))
        return;
    hurt_player(e, ((g_rand() + g_rand() + g_rand()) * 3) >> 15);
}

/* Demon_Melee: a claw, 10..15 points within 100 units */
static void demon_melee(ent_t *e)
{
    fixed_t c[3];
    ai_face(e);
    walkmove(e, e->ideal_yaw, 12);
    if (dist_to_player(e) > 100 || !can_see_player(e))
        return;
    snd_start(SND_DEMON_DHIT2, e->org, 255);
    hurt_player(e, 10 + ((g_rand() * 5) >> 15));
    c[0] = e->org[0] + fcos(e->yaw) * 16;
    c[1] = e->org[1] + fsin(e->yaw) * 16;
    c[2] = e->org[2] + FX(16);
    g_particles(c, NULL, 73, 10);
}

/* Wiz_FastFire: a spit at the player from beside the scrag */
static void wizard_fire(ent_t *e, int side)
{
    fixed_t org[3], v[3];
    int k;
    snd_start(SND_WIZARD_WATTACK, e->org, 255);
    for (k = 0; k < 2; k++)
        org[k] = e->org[k] + (k ? fsin(e->yaw) : fcos(e->yaw)) * 14
                 + (k ? -fcos(e->yaw) : fsin(e->yaw)) * 14 * side;
    org[2] = e->org[2] + FX(30);
    v[0] = player.pos[0] - org[0];
    v[1] = player.pos[1] - org[1];
    v[2] = player.pos[2] - org[2];
    g_normalize(v);
    for (k = 0; k < 3; k++)
        v[k] *= 600;
    g_missile(MK_WIZSPIT, org, v, e - ents);
}

/* ZombieFireGrenade: a lump of flesh, from the hand (forward, right, up) */
static void zombie_throw(ent_t *e, int f, int r, int u)
{
    fixed_t org[3], v[3];
    fixed_t cy = fcos(e->yaw), sy = fsin(e->yaw);
    int k;
    snd_start(SND_ZOMBIE_Z_SHOT1, e->org, 255);
    org[0] = e->org[0] + cy * f + sy * r;
    org[1] = e->org[1] + sy * f - cy * r;
    org[2] = e->org[2] + FX(u - 24);
    v[0] = player.pos[0] - org[0];
    v[1] = player.pos[1] - org[1];
    v[2] = player.pos[2] - org[2];
    g_normalize(v);
    for (k = 0; k < 2; k++)
        v[k] *= 600;
    v[2] = FX(200);
    g_missile(MK_ZOMGIB, org, v, e - ents);
}

/* WizardAttackFinished: straight at the player from afar or unseen, else
   sideways */
static void wizard_done(ent_t *e)
{
    int r = range_of(e);
    e->attack_finished = level_time + TICKS(2);
    if (r >= RANGE_MID || !can_see_player(e)) {
        e->attack_state = AS_STRAIGHT;
        set_anim(e, A_RUN);
    } else {
        e->attack_state = AS_SLIDING;
        set_anim(e, A_SIDE);
    }
}

/* the decision to attack (CheckAttack and the classes' own) */
static int check_attack(ent_t *e, int range, int visible)
{
    int chance, c = e->cls;
    if (!visible)
        return 0;
    switch (c) {
    case CL_DOG: {
        int dz, d;
        fixed_t a[3], b[3];
        if (range == RANGE_MELEE) {
            e->attack_state = AS_MELEE;
            return 1;
        }
        /* CheckDogJump: player roughly level, 80..150 units away */
        dz = UNITS(e->org[2] - player.pos[2]);
        if (dz - 24 > -24 + 42 || dz + 40 < -24 + 14)
            return 0;
        a[0] = e->org[0]; a[1] = e->org[1]; a[2] = 0;
        b[0] = player.pos[0]; b[1] = player.pos[1]; b[2] = 0;
        d = vlen_units(a, b);
        if (d < 80 || d > 150)
            return 0;
        e->attack_state = AS_MISSILE;
        return 1;
    }
    case CL_OGRE:
        /* OgreCheckAttack: the chainsaw up close, else a grenade whenever it can */
        if (range == RANGE_MELEE) {
            e->attack_state = AS_MELEE;
            return 1;
        }
        if (level_time < e->attack_finished || range == RANGE_FAR)
            return 0;
        e->attack_state = AS_MISSILE;
        e->attack_finished = level_time + TICKS(1) + ((g_rand() * 120) >> 15);
        return 1;
    case CL_DEMON1: {
        /* DemonCheckAttack: claws up close, else a leap from 100..200 units */
        int pz0 = UNITS(player.pos[2]) - 24, d;
        int ez = UNITS(e->org[2]);
        fixed_t a[3], b[3];
        if (range == RANGE_MELEE) {
            e->attack_state = AS_MELEE;
            return 1;
        }
        if (ez - 24 > pz0 + 42 || ez + 64 < pz0 + 14)
            return 0;
        a[0] = e->org[0]; a[1] = e->org[1]; a[2] = 0;
        b[0] = player.pos[0]; b[1] = player.pos[1]; b[2] = 0;
        d = vlen_units(a, b);
        if (d < 100 || (d > 200 && g_rand() < 29491))
            return 0;
        e->attack_state = AS_MISSILE;
        voice(e, SND_DEMON_DJUMP, ATTN_NORM);
        return 1;
    }
    case CL_WIZARD:
        /* WizardCheckAttack: often up close; otherwise drift sideways or
           straight in */
        if (level_time < e->attack_finished)
            return 0;
        if (range == RANGE_FAR) {
            if (e->attack_state != AS_STRAIGHT) {
                e->attack_state = AS_STRAIGHT;
                set_anim(e, A_RUN);
            }
            return 0;
        }
        chance = range == RANGE_MELEE ? 29491 : range == RANGE_NEAR ? 19661 : 6554;
        if (g_rand() < chance) {
            e->attack_state = AS_MISSILE;
            return 1;
        }
        if (range == RANGE_MID) {
            if (e->attack_state != AS_STRAIGHT) {
                e->attack_state = AS_STRAIGHT;
                set_anim(e, A_RUN);
            }
        } else if (e->attack_state != AS_SLIDING) {
            e->attack_state = AS_SLIDING;
            set_anim(e, A_SIDE);
        }
        return 0;
    case CL_ARMY:
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
    default:
        /* CheckAttack: the knight's sword, the zombie's flesh */
        if (range == RANGE_MELEE && c == CL_KNIGHT) {
            /* knight_attack: a standing swing up close, else one in passing */
            set_anim(e, dist_to_player(e) < 80 ? A_MELEE2 : A_MELEE);
            return 1;
        }
        if (c != CL_ZOMBIE || level_time < e->attack_finished || range == RANGE_FAR)
            return 0;
        chance = range == RANGE_MELEE ? 29491 : range == RANGE_NEAR ? 13107 : 3277;
        if (g_rand() < chance) {
            int r = g_rand();
            set_anim(e, r < 9830 ? A_ATTACK : r < 19661 ? A_ATTACK2 : A_ATTACK3);
            e->attack_finished = level_time + ((g_rand() * 120) >> 15);
            return 1;
        }
        return 0;
    }
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
    enemy_yaw = yaw_to_player(e);
    if (e->attack_state == AS_MISSILE || e->attack_state == AS_MELEE) {
        /* ai_run_missile / ai_run_melee: turn, then attack */
        e->ideal_yaw = enemy_yaw;
        change_yaw(e);
        if (facing_ideal(e)) {
            int a = e->attack_state == AS_MISSILE ? A_ATTACK : A_MELEE;
            if (e->cls == CL_OGRE && a == A_MELEE && (g_rand() & 1))
                a = A_MELEE2;
            set_anim(e, a);
            e->attack_state = AS_STRAIGHT;
        }
        return;
    }
    if (check_attack(e, range, visible))
        return;
    if (e->attack_state == AS_SLIDING) {
        /* ai_run_slide: strafe at about the same range */
        e->ideal_yaw = enemy_yaw;
        change_yaw(e);
        if (!walkmove(e, enemy_yaw + (e->lefty ? ANG(90) : -ANG(90)), dist)) {
            e->lefty ^= 1;
            walkmove(e, enemy_yaw - (e->lefty ? -ANG(90) : ANG(90)), dist);
        }
        return;
    }
    movetogoal(e, dist);
}

/* ------------------------------------------------------------ entry points */

GCODE void m_spawn(ent_t *e, int cls, int hunting)
{
    const mclass_t *c;
    fixed_t s[3], end[3];
    trace_t tr;
    e->type = ET_MONSTER;
    e->cls = cls;
    c = mc(e);
    e->model = c->model;
    e->health = c->health;
    e->mins[0] = e->mins[1] = -c->size;
    e->maxs[0] = e->maxs[1] = c->size;
    e->mins[2] = -24;
    e->maxs[2] = c->height;
    e->spawned = level_time;
    if (!(c->flags & MC_FLY)) {
        /* droptofloor */
        s[0] = e->org[0]; s[1] = e->org[1]; s[2] = e->org[2] + FX(1);
        end[0] = s[0]; end[1] = s[1]; end[2] = s[2] - FX(256);
        g_trace(m_hull(e), s, end, &tr);
        if (!tr.allsolid && !tr.startsolid && tr.frac < 0x10000) {
            e->org[2] = tr.end[2];
            e->onground = 1;
        }
    }
    e->nextthink = level_time + 1 + (g_rand() % 30);
    g_relink(e);
    if (hunting)
        found_target(e);
    else
        set_anim(e, A_STAND);
}

/* th_pain for each class */
GCODE void m_pain(ent_t *e, int dmg)
{
    int r;
    if (!e->hunting)
        found_target(e);
    switch (e->cls) {
    case CL_DOG:
        voice(e, SND_DOG_DPAIN1, ATTN_NORM);
        set_anim(e, g_rand() & 1 ? A_PAIN1 : A_PAIN2);
        break;
    case CL_ARMY:
        if (e->pain_finished > level_time)
            return;
        r = g_rand();
        if (r < 6554) {
            e->pain_finished = level_time + TICKS(0.6);
            set_anim(e, A_PAIN1);
            voice(e, SND_SOLDIER_PAIN1, ATTN_NORM);
        } else {
            e->pain_finished = level_time + TICKS(1.1);
            set_anim(e, r < 19661 ? A_PAIN2 : A_PAIN3);
            voice(e, SND_SOLDIER_PAIN2, ATTN_NORM);
        }
        break;
    case CL_OGRE:
        if (e->pain_finished > level_time)
            return;
        voice(e, SND_OGRE_OGPAIN1, ATTN_NORM);
        r = g_rand();
        if (r < 8192) set_anim(e, A_PAIN1);
        else if (r < 16384) set_anim(e, A_PAIN2);
        else if (r < 24576) set_anim(e, A_PAIN3);
        else if (r < 28836) set_anim(e, A_PAIN4);
        else set_anim(e, A_PAIN5);
        e->pain_finished = level_time + (r < 24576 ? TICKS(1) : TICKS(2));
        break;
    case CL_KNIGHT:
        if (e->pain_finished > level_time)
            return;
        voice(e, SND_KNIGHT_KHURT, ATTN_NORM);
        set_anim(e, g_rand() < 27853 ? A_PAIN1 : A_PAIN2);
        e->pain_finished = level_time + TICKS(1);
        break;
    case CL_DEMON1:
        if (e->leaping || e->pain_finished > level_time)
            return;
        e->pain_finished = level_time + TICKS(1);
        voice(e, SND_DEMON_DPAIN1, ATTN_NORM);
        if (((g_rand() * 200) >> 15) > dmg)
            return;                 /* didn't flinch */
        set_anim(e, A_PAIN1);
        break;
    case CL_WIZARD:
        voice(e, SND_WIZARD_WPAIN, ATTN_NORM);
        if (((g_rand() * 70) >> 15) > dmg)
            return;
        set_anim(e, A_PAIN1);
        break;
    case CL_ZOMBIE:
        /* zombie_pain: only a big enough hit at once kills (g_damage);
           25 or more, or a second hit soon after, puts it on the ground */
        e->health = 60;
        if (dmg < 9 || e->inpain == 2)
            return;
        if (dmg >= 25) {
            e->inpain = 2;
            set_anim(e, A_PAIN5);
            break;
        }
        if (e->inpain) {
            e->pain_finished = level_time + TICKS(3);
            return;
        }
        if (e->pain_finished > level_time) {
            e->inpain = 2;
            set_anim(e, A_PAIN5);
            break;
        }
        e->inpain = 1;
        r = g_rand();
        set_anim(e, r < 8192 ? A_PAIN1 : r < 16384 ? A_PAIN2 : r < 24576 ? A_PAIN3 : A_PAIN4);
        break;
    default:
        return;
    }
    e->nextthink = level_time + 6;
}

/* the corpse: only the most recent few stay */
static void keep_corpses(void)
{
    int i, corpses = 0, oldest = -1;
    for (i = 0; i < MAX_ENTS; i++)
        if (ents[i].type == ET_CORPSE || (ents[i].type == ET_GIB && ents[i].cls == 1)) {
            corpses++;
            if (oldest < 0 || ents[i].died < ents[oldest].died)
                oldest = i;
        }
    if (corpses > MAX_CORPSES && oldest >= 0)
        g_free(&ents[oldest]);
}

/* th_die: gibs if the blow was big enough, else a death animation */
GCODE void m_die(ent_t *e)
{
    const mclass_t *c = mc(e);
    fixed_t org[3];
    int k;
    e->flags &= ~EF_NOTSOLID;
    if (e->health < c->gib_health || e->cls == CL_ZOMBIE) {
        voice(e, e->cls == CL_ZOMBIE ? SND_ZOMBIE_Z_GIB : SND_PLAYER_UDEATH, ATTN_NORM);
        org[0] = e->org[0]; org[1] = e->org[1]; org[2] = e->org[2];
        for (k = 0; k < 3; k++)
            g_throw_gib(c->gib[k], org, e->health);
        g_throw_head(e, c->head, e->health);
        keep_corpses();
        return;
    }
    if (e->cls != CL_DEMON1 && e->cls != CL_WIZARD)
        voice(e, c->death, ATTN_NORM);
    set_anim(e, e->cls == CL_DEMON1 || e->cls == CL_WIZARD || (g_rand() & 1) ? A_DIE1 : A_DIE2);
    if (!c->anims[A_DIE2].len)
        set_anim(e, A_DIE1);
    e->type = ET_CORPSE;
    e->died = level_time;
    e->nextthink = level_time + 6;
    e->leaping = 0;
    keep_corpses();
}

/* DropBackpack: grunts drop shells, ogres rockets */
static void drop_backpack(ent_t *e)
{
    ent_t *b = g_spawn();
    if (!b)
        return;
    b->type = ET_ITEM;
    b->cls = CL_BACKPACK;
    b->variant = e->cls == CL_OGRE;     /* 0: 5 shells, 1: 2 rockets */
    b->model = MDL_BACKPACK;
    b->org[0] = e->org[0];
    b->org[1] = e->org[1];
    b->org[2] = e->org[2] - FX(24);
    b->mins[0] = b->mins[1] = -16; b->mins[2] = 0;
    b->maxs[0] = b->maxs[1] = 16; b->maxs[2] = 56;
    g_relink(b);
}

static void event(ent_t *e, int ev)
{
    switch (ev) {
    case EV_IDLE:
        if (e->cls == CL_WIZARD) {
            /* Wiz_idlesound: at most every two seconds */
            if (level_time > e->attack_finished + TICKS(2) || (g_rand() & 3) == 0) {
                int r = g_rand();
                if (r > 29491) voice(e, SND_WIZARD_WIDLE1, ATTN_IDLE);
                else if (r < 9830) voice(e, SND_WIZARD_WIDLE2, ATTN_IDLE);
            }
        } else if (g_rand() < mc(e)->idle_chance)
            voice(e, mc(e)->idle, ATTN_IDLE);
        break;
    case EV_ARMY_FIRE: army_fire(e); break;
    case EV_DOG_BITE: dog_bite(e); break;
    case EV_DOG_LEAP: leap(e, 300, 200); break;
    case EV_SAW_SND: voice(e, SND_OGRE_OGSAWATK, ATTN_NORM); break;
    case EV_SAW: chainsaw(e); break;
    case EV_OGRE_GRENADE: ogre_grenade(e); break;
    case EV_SWORD_SND: voice(e, g_rand() & 1 ? SND_KNIGHT_SWORD2 : SND_KNIGHT_SWORD1, ATTN_NORM); break;
    case EV_SWORD1_SND: voice(e, SND_KNIGHT_SWORD1, ATTN_NORM); break;
    case EV_KNIGHT_MELEE: knight_melee(e); break;
    case EV_DEMON_LEAP: leap(e, 600, 250); break;
    case EV_DEMON_MELEE_R: case EV_DEMON_MELEE_L: demon_melee(e); break;
    case EV_WIZ_START: voice(e, SND_WIZARD_WATTACK, ATTN_NORM); break;
    case EV_WIZ_FIRE_L: wizard_fire(e, -1); break;
    case EV_WIZ_FIRE_R: wizard_fire(e, 1); break;
    case EV_WIZ_DONE: wizard_done(e); break;
    case EV_ZOM_RUN: e->inpain = 0; break;
    case EV_ZOM_IDLE:
        if (g_rand() < 6554) voice(e, SND_ZOMBIE_Z_IDLE, ATTN_IDLE);
        else if (g_rand() > 26214) voice(e, SND_ZOMBIE_Z_IDLE1, ATTN_IDLE);
        break;
    case EV_ZOM_THROW_A: zombie_throw(e, -10, -22, 30); break;
    case EV_ZOM_THROW_B: zombie_throw(e, -10, -24, 29); break;
    case EV_ZOM_THROW_C: zombie_throw(e, -12, -19, 29); break;
    case EV_ZOM_PAIN: voice(e, SND_ZOMBIE_Z_PAIN, ATTN_NORM); break;
    case EV_ZOM_PAIN1: voice(e, SND_ZOMBIE_Z_PAIN1, ATTN_NORM); break;
    case EV_ZOM_FALL: voice(e, SND_ZOMBIE_Z_FALL, ATTN_NORM); break;
    case EV_ZOM_DOWN:
        voice(e, SND_ZOMBIE_Z_FALL, ATTN_NORM);
        e->flags |= EF_NOTSOLID;
        break;
    case EV_ZOM_WAIT:
        e->nextthink += TICKS(5);
        e->health = 60;
        break;
    case EV_ZOM_UP: {
        /* up again, if there is room */
        fixed_t p[3];
        e->health = 60;
        voice(e, SND_ZOMBIE_Z_IDLE, ATTN_IDLE);
        p[0] = e->org[0]; p[1] = e->org[1]; p[2] = e->org[2];
        if (g_contents(m_hull(e), p) == CONTENTS_SOLID || blocked_by_others(e, p)) {
            e->animpos = 10;            /* back down, try again later */
            e->frame = e->anim->first + 10;
            e->nextthink = level_time + TICKS(1);
            anim_changed = 1;
            break;
        }
        e->flags &= ~EF_NOTSOLID;
        break;
    }
    case EV_WIZ_DIE:
        e->vel[0] = FX(-200) + ((g_rand() * 400) >> 15) * 65536;
        e->vel[1] = FX(-200) + ((g_rand() * 400) >> 15) * 65536;
        e->vel[2] = FX(100) + ((g_rand() * 100) >> 15) * 65536;
        e->onground = 0;
        voice(e, SND_WIZARD_WDEATH, ATTN_NORM);
        break;
    case EV_DEMON_DIE:
        voice(e, SND_DEMON_DDEATH, ATTN_NORM);
        break;
    case EV_DROP:
        if (e->cls == CL_ARMY || e->cls == CL_OGRE)
            drop_backpack(e);
        break;
    }
}

GCODE void m_think(ent_t *e)
{
    const anim_t *a = e->anim;
    int pos = e->animpos, d = a->dist[pos & 31];
    e->nextthink = level_time + 6;
    e->frame = a->frames ? a->frames[pos] : a->first + pos;
    anim_changed = 0;

    if (e->type == ET_CORPSE) {
        if (a->ev[pos & 31])
            event(e, a->ev[pos & 31]);
        if (a->kind == K_MOVE && d)
            walkmove(e, d > 0 ? e->yaw : e->yaw + 32768, d > 0 ? d : -d);
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
    case K_CHARGE_SIDE:
        ai_charge_side(e);
        break;
    case K_MOVE:
        if (d) walkmove(e, d > 0 ? e->ideal_yaw : e->yaw + 32768, d > 0 ? d : -d);
        break;
    }
    if (!anim_changed && a->ev[pos & 31])
        event(e, a->ev[pos & 31]);
    if (anim_changed || e->type != ET_MONSTER)
        return;
    if (pos + 1 < a->len)
        e->animpos++;
    else if (a->next != HOLD)
        set_anim(e, a->next);
    else if (e->cls == CL_DEMON1 && e->leaping && level_time - e->died > TICKS(3)) {
        /* stuck in a leap for three seconds: leap again */
        e->leaping = 0;
        set_anim(e, A_ATTACK);
    }
}

/* gravity and leaps (MOVETYPE_STEP and the dog's and fiend's jumps), every
 * tick; scrags fly until they die */
GCODE void m_physics(ent_t *e)
{
    fixed_t end[3];
    trace_t tr;
    int k, fly = e->type == ET_MONSTER && (mc(e)->flags & MC_FLY);
    if (fly)
        return;
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
    /* a leaping dog bites, a leaping fiend hits (if fast), what it lands on */
    if (e->leaping == 1 && e->type == ET_MONSTER && !ps.dead) {
        int dx = UNITS(e->org[0] - player.pos[0]), dy = UNITS(e->org[1] - player.pos[1]);
        int dz = UNITS(e->org[2] - player.pos[2]), r = e->maxs[0] + 16;
        if (dx > -r && dx < r && dy > -r && dy < r && dz > -64 && dz < 56) {
            if (e->cls == CL_DEMON1) {
                fixed_t v2 = fixmul(e->vel[0] >> 4, e->vel[0] >> 4) + fixmul(e->vel[1] >> 4, e->vel[1] >> 4)
                             + fixmul(e->vel[2] >> 4, e->vel[2] >> 4);
                if (v2 > FX(400 * 400 / 256))
                    g_damage_player(40 + ((g_rand() * 10) >> 15), e->org);
            } else
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
                    if (e->cls == CL_DEMON1)
                        voice(e, SND_DEMON_DLAND2, ATTN_NORM);
                    set_anim(e, e->cls == CL_DEMON1 ? A_LAND : A_RUN);
                    e->nextthink = level_time + 6;
                }
            }
        } else {
            e->vel[0] = e->vel[1] = 0;      /* hit a wall: drop */
        }
    }
}

/* Doors, secret doors, plats (lifts), buttons, trains and triggers, after
 * id's QuakeC (doors.qc, plats.qc, buttons.qc, triggers.qc, subs.qc).
 *
 * A mover is a brush model with an offset from where the map built it.
 * It moves at a constant speed between points of its path (SUB_CalcMove),
 * carries whatever stands on it, and stops or turns back when something
 * is in the way. Its collision uses its own clip hulls; the renderer draws
 * it through keyframed BSP fragments (tools/movers.py, mv_render_state). */
#include "g_local.h"

const lmover_t *lmovers;
int nmovers;
fixed_t mover_off[MAX_MOVERS][3];
static uint8_t rs_valid[MAX_MOVERS];    /* view.mkey/mres hold this mover's position */
static const ltrigger_t *ltrigs;
static int ntrigs;
static const int16_t *mkeys;
static const uint32_t *strtab;
static int nstrings;

/* what to do when a move ends or a timer fires */
enum {
    A_NONE, A_DOOR_TOP, A_DOOR_BOTTOM, A_DOOR_DOWN, A_PLAT_TOP, A_PLAT_BOTTOM, A_PLAT_DOWN,
    A_BTN_WAIT, A_BTN_RETURN, A_BTN_DONE, A_SEC1, A_SEC2, A_SEC3, A_SEC4, A_SEC5, A_SEC6, A_SEC_DONE,
    A_TRAIN_CORNER, A_TRAIN_NEXT
};

typedef struct {
    fixed_t  vel[3], dest[3];
    uint16_t ticks;                  /* left in the current move */
    uint8_t  state, done;            /* MS_*, action when the move ends */
    uint8_t  think_fn, dead, nomsg, shootable;
    uint32_t think;                  /* tick for think_fn, 0 none */
    uint32_t touch_finished, block_finished;
    uint8_t  corner, unlocked;       /* trains: the corner reached; key doors: opened */
    uint16_t pad;
} mstate_t;

typedef struct {
    uint32_t next;                   /* armed again at this tick */
    uint32_t touch_finished;
    int16_t  count;
    uint8_t  dead, shootable;
} tstate_t;

/* the movers' and triggers' state; while the attract demo plays none of
 * it runs, and the demo player has the memory (playdemo.c) */
static union {
    struct { mstate_t ms[MAX_MOVERS]; tstate_t ts[MAX_TRIGGERS]; } g;
    uint32_t align;
} mv_mem;
#define ms mv_mem.g.ms
#define ts mv_mem.g.ts
void *const mv_scratch = &mv_mem;
const int mv_scratch_size = sizeof(mv_mem);

#define TR_NOTOUCH 1
#define SECRET_OPEN_ONCE 1
#define SECRET_YES_SHOOT 16

/* ------------------------------------------------------------ helpers */

GCODE const char *mv_string(int id)
{
    if (id <= 0 || id >= nstrings)
        return NULL;
    return (const char *)(lvl.base + strtab[id]);
}

static void show_message(const char *m)
{
    if (!m)
        return;
    ps.msg = m;
    ps.msg_until = level_time + TICKS(3);
    snd_start(SND_TALK, NULL, 255);
}

static void center(int m, fixed_t c[3])
{
    const lmover_t *l = &lmovers[m];
    int k;
    for (k = 0; k < 3; k++)
        c[k] = (FX(l->mins[k] + l->maxs[k]) >> 1) + mover_off[m][k];
}

enum { NZ_MOVE, NZ_STOP, NZ_SEC1, NZ_SEC2, NZ_SEC3 };

/* the sounds each kind of mover makes (noise, noise1.. in the QuakeC) */
static void mover_sound(int m, int which)
{
    const lmover_t *l = &lmovers[m];
    int s = -1;
    fixed_t c[3];
    switch (l->cls) {
    case MV_DOOR: {
        static const int16_t mv[4] = { -1, SND_DOORMV1, SND_HYDRO1, SND_STNDR1 };
        static const int16_t st[4] = { -1, SND_DRCLOS4, SND_HYDRO2, SND_STNDR2 };
        if (l->sounds < 4)
            s = which == NZ_MOVE ? mv[l->sounds] : st[l->sounds];
        break;
    }
    case MV_DOOR_SECRET: {
        static const int16_t n1[4] = { -1, SND_LATCH2, SND_AIRDOOR2, SND_BASESEC2 };
        static const int16_t n2[4] = { -1, SND_WINCH2, SND_AIRDOOR1, SND_BASESEC1 };
        static const int16_t n3[4] = { -1, SND_DRCLOS4, SND_AIRDOOR2, SND_BASESEC2 };
        if (l->sounds < 4)
            s = which == NZ_SEC1 ? n1[l->sounds] : which == NZ_SEC2 ? n2[l->sounds] : n3[l->sounds];
        break;
    }
    case MV_PLAT:
        if (l->sounds == 1) s = which == NZ_MOVE ? SND_PLAT1 : SND_PLAT2;
        else s = which == NZ_MOVE ? SND_MEDPLAT1 : SND_MEDPLAT2;
        break;
    case MV_BUTTON: {
        static const int16_t b[4] = { SND_AIRBUT1, SND_SWITCH21, SND_SWITCH02, SND_SWITCH04 };
        s = b[l->sounds & 3];
        break;
    }
    case MV_TRAIN:
        if (l->sounds == 1)
            s = which == NZ_MOVE ? SND_TRAIN1 : SND_TRAIN2;
        break;
    }
    if (s < 0)
        return;
    center(m, c);
    snd_start(s, c, 255);
}

/* SUB_CalcMove: to an offset at the mover's speed, then `done` */
static void calc_move(int m, const int16_t *to, int done)
{
    mstate_t *s = &ms[m];
    int k, len, ticks;
    fixed_t d[3];
    for (k = 0; k < 3; k++) {
        s->dest[k] = FX(to[k]);
        d[k] = s->dest[k] - mover_off[m][k];
    }
    len = vlen_units(s->dest, mover_off[m]);
    ticks = hw_div(len * 60 + lmovers[m].speed - 1, lmovers[m].speed);
    if (ticks < 6)
        ticks = 6;                  /* Quake moves for at least 0.1 s */
    for (k = 0; k < 3; k++)
        s->vel[k] = hw_div(d[k], ticks);
    s->ticks = ticks;
    s->done = done;
}

static void set_think(int m, int ticks, int fn)
{
    ms[m].think = level_time + (ticks > 0 ? ticks : 1);
    ms[m].think_fn = fn;
}

static int wait_ticks(int m)
{
    return lmovers[m].wait10 * 6;   /* tenths of a second */
}

/* ------------------------------------------------------------ use */

static void use_targets(int target, int killtarget, int msg, int by_player);

static void door_go_up(int m)
{
    mstate_t *s = &ms[m];
    if (s->state == MS_UP)
        return;
    if (s->state == MS_TOP) {
        if (lmovers[m].wait10 >= 0)
            set_think(m, wait_ticks(m), A_DOOR_DOWN);   /* reset the timer */
        return;
    }
    mover_sound(m, NZ_MOVE);
    s->state = MS_UP;
    s->think = 0;
    calc_move(m, lmovers[m].pos[1], A_DOOR_TOP);
    use_targets(lmovers[m].target, 0, 0, 0);
}

static void door_go_down(int m)
{
    mstate_t *s = &ms[m];
    mover_sound(m, NZ_MOVE);
    s->state = MS_DOWN;
    s->think = 0;
    calc_move(m, lmovers[m].pos[0], A_DOOR_BOTTOM);
}

/* door_use / door_fire: the whole linked group opens */
static void door_use(int m)
{
    int lead = lmovers[m].group, i;
    ms[lead].nomsg = 1;
    for (i = 0; i < nmovers; i++)
        if (lmovers[i].cls == MV_DOOR && lmovers[i].group == lead && !ms[i].dead)
            door_go_up(i);
}

static void plat_go_up(int m)
{
    mover_sound(m, NZ_MOVE);
    ms[m].state = MS_UP;
    ms[m].think = 0;
    calc_move(m, lmovers[m].pos[0], A_PLAT_TOP);
}

static void plat_go_down(int m)
{
    mover_sound(m, NZ_MOVE);
    ms[m].state = MS_DOWN;
    ms[m].think = 0;
    calc_move(m, lmovers[m].pos[1], A_PLAT_BOTTOM);
}

static void button_fire(int m)
{
    mstate_t *s = &ms[m];
    if (s->state == MS_UP || s->state == MS_TOP)
        return;
    mover_sound(m, NZ_MOVE);
    s->state = MS_UP;
    calc_move(m, lmovers[m].pos[1], A_BTN_WAIT);
}

static void secret_use(int m)
{
    mstate_t *s = &ms[m];
    if (mover_off[m][0] || mover_off[m][1] || mover_off[m][2] || s->ticks)
        return;                     /* still moving around */
    s->nomsg = 1;
    use_targets(lmovers[m].target, 0, 0, 0);
    s->shootable = 0;
    mover_sound(m, NZ_SEC1);
    calc_move(m, lmovers[m].pos[1], A_SEC1);
    mover_sound(m, NZ_SEC2);
}

/* train_next: on to the next path corner (a train stops for good at the
   corner that waits -1, the last one kept) */
static void train_next(int m)
{
    mstate_t *s = &ms[m];
    if (s->corner + 1 >= lmovers[m].npos)
        return;
    s->corner++;
    mover_sound(m, NZ_MOVE);
    s->state = MS_UP;
    calc_move(m, lmovers[m].pos[s->corner], A_TRAIN_CORNER);
}

static void mover_use(int m)
{
    if (ms[m].dead)
        return;
    switch (lmovers[m].cls) {
    case MV_DOOR: door_use(m); break;
    case MV_DOOR_SECRET: secret_use(m); break;
    case MV_PLAT:
        if (ms[m].state == MS_UP && !ms[m].ticks)
            plat_go_down(m);        /* a targeted plat waits at the top */
        break;
    case MV_BUTTON: button_fire(m); break;
    case MV_TRAIN:
        /* train_use: only a train still at its first corner starts */
        if (ms[m].corner == 0 && !ms[m].ticks)
            train_next(m);
        break;
    }
}

static void multi_trigger(int t);

/* trigger_counter: counts uses, then fires */
static void counter_use(int t)
{
    static const char *const left[4] = {
        "Only 1 more to go...", "Only 2 more to go...", "Only 3 more to go...", "There are more to go..."
    };
    tstate_t *s = &ts[t];
    if (--s->count < 0)
        return;
    if (s->count) {
        if (!(ltrigs[t].flags & 1))
            show_message(left[s->count > 4 ? 3 : s->count - 1]);
        return;
    }
    if (!(ltrigs[t].flags & 1))
        show_message("Sequence completed!");
    multi_trigger(t);
}

static void trigger_use(int t)
{
    if (ts[t].dead)
        return;
    switch (ltrigs[t].cls) {
    case TR_ONCE: case TR_MULTIPLE: case TR_SECRET: multi_trigger(t); break;
    case TR_COUNTER: counter_use(t); break;
    default: break;
    }
}

/* SUB_UseTargets: the message, kill targets, then use targets */
static void use_targets(int target, int killtarget, int msg, int by_player)
{
    int i;
    if (msg && by_player)
        show_message(mv_string(msg));
    if (killtarget) {
        for (i = 0; i < ntrigs; i++)
            if (ltrigs[i].targetname == killtarget)
                ts[i].dead = 1;
        for (i = 0; i < nmovers; i++)
            if (lmovers[i].targetname == killtarget)
                ms[i].dead = 1;
    }
    if (target) {
        for (i = 0; i < nmovers; i++)
            if (lmovers[i].targetname == target)
                mover_use(i);
        for (i = 0; i < ntrigs; i++)
            if (ltrigs[i].targetname == target)
                trigger_use(i);
        g_use_entities(target);     /* spike shooters */
    }
}

/* SUB_UseTargets for the game's entities (a key fires its target) */
GCODE void mv_use_targets(int target)
{
    use_targets(target, 0, 0, 0);
}

static void trigger_noise(int t)
{
    static const int16_t n[4] = { -1, SND_SECRET, SND_TALK, SND_TRIGGER1 };
    int s = ltrigs[t].sounds;
    if (s > 0 && s < 4)
        snd_start(n[s], NULL, 255);
}

static void multi_trigger(int t)
{
    const ltrigger_t *l = &ltrigs[t];
    tstate_t *s = &ts[t];
    if (s->dead || s->next > level_time)
        return;
    if (l->cls == TR_SECRET)
        ps.secrets++;
    trigger_noise(t);
    s->shootable = 0;
    use_targets(l->target, l->killtarget, l->msg, 1);
    if (l->wait10 > 0)
        s->next = level_time + l->wait10 * 6;
    else
        s->dead = 1;
}

/* ------------------------------------------------------------ moving */

static void arrive(int m, int a)
{
    const lmover_t *l = &lmovers[m];
    mstate_t *s = &ms[m];
    switch (a) {
    case A_DOOR_TOP:
        mover_sound(m, NZ_STOP);
        s->state = MS_TOP;
        if (l->wait10 >= 0)
            set_think(m, wait_ticks(m), A_DOOR_DOWN);
        break;
    case A_DOOR_BOTTOM:
        mover_sound(m, NZ_STOP);
        s->state = MS_BOTTOM;
        break;
    case A_DOOR_DOWN: door_go_down(m); break;
    case A_PLAT_TOP:
        mover_sound(m, NZ_STOP);
        s->state = MS_TOP;
        set_think(m, TICKS(3), A_PLAT_DOWN);
        break;
    case A_PLAT_BOTTOM:
        mover_sound(m, NZ_STOP);
        s->state = MS_BOTTOM;
        break;
    case A_PLAT_DOWN: plat_go_down(m); break;
    case A_BTN_WAIT:
        s->state = MS_TOP;
        if (l->wait10 >= 0)
            set_think(m, wait_ticks(m), A_BTN_RETURN);
        use_targets(l->target, 0, l->msg, 1);
        break;
    case A_BTN_RETURN:
        s->state = MS_DOWN;
        calc_move(m, l->pos[0], A_BTN_DONE);
        break;
    case A_BTN_DONE:
        s->state = MS_BOTTOM;
        break;
    /* the secret door: aside, wait a second, back, wait, and return */
    case A_SEC1:
        mover_sound(m, NZ_SEC3);
        set_think(m, TICKS(1), A_SEC2);
        break;
    case A_SEC2:
        mover_sound(m, NZ_SEC2);
        calc_move(m, l->pos[2], A_SEC3);
        break;
    case A_SEC3:
        mover_sound(m, NZ_SEC3);
        if (!(l->flags & SECRET_OPEN_ONCE))
            set_think(m, wait_ticks(m), A_SEC4);
        break;
    case A_SEC4:
        mover_sound(m, NZ_SEC2);
        calc_move(m, l->pos[1], A_SEC5);
        break;
    case A_SEC5:
        mover_sound(m, NZ_SEC3);
        set_think(m, TICKS(1), A_SEC6);
        break;
    case A_SEC6: {
        static const int16_t zero[3] = { 0, 0, 0 };
        mover_sound(m, NZ_SEC2);
        calc_move(m, zero, A_SEC_DONE);
        break;
    }
    case A_SEC_DONE:
        s->shootable = l->health > 0;
        mover_sound(m, NZ_SEC3);
        break;
    /* trains: at a corner, on to the next one a tenth of a second later */
    case A_TRAIN_CORNER:
        s->state = MS_TOP;
        if (s->corner + 1 < l->npos)
            set_think(m, 6, A_TRAIN_NEXT);
        else
            mover_sound(m, NZ_STOP);
        break;
    case A_TRAIN_NEXT:
        train_next(m);
        break;
    }
}

/* hull and box of what might stand on or be hit by mover m */
static int ent_hull(const ent_t *e)
{
    return m_hull(e);
}

/* the mover is in the way: crush, and doors and plats turn back */
static void blocked(int m, ent_t *who)
{
    const lmover_t *l = &lmovers[m];
    mstate_t *s = &ms[m];
    if (level_time >= s->block_finished) {
        s->block_finished = level_time + (l->cls == MV_DOOR_SECRET || l->cls == MV_TRAIN ? TICKS(0.5) : 6);
        if (who)
            g_hurt(who, l->dmg);
        else
            g_damage_player(l->dmg, NULL);
    }
    if (l->cls == MV_DOOR && l->wait10 >= 0) {
        if (s->state == MS_DOWN) door_go_up(m);
        else door_go_down(m);
    } else if (l->cls == MV_PLAT) {
        if (s->state == MS_UP) plat_go_down(m);
        else if (s->state == MS_DOWN) plat_go_up(m);
    }
}

static int standing_on(int m, int hull, const fixed_t p[3])
{
    fixed_t below[3];
    below[0] = p[0];
    below[1] = p[1];
    below[2] = p[2] - FX(2);
    return g_mover_solid(m, hull, below) && !g_mover_solid(m, hull, p);
}

static void move_one(int m)
{
    mstate_t *s = &ms[m];
    fixed_t d[3], old[3];
    int k, i, ride_player, rides[MAX_ENTS], nr = 0;
    if (!s->ticks)
        return;
    for (k = 0; k < 3; k++) {
        old[k] = mover_off[m][k];
        d[k] = s->ticks == 1 ? s->dest[k] - old[k] : s->vel[k];
    }
    /* who rides it: standing on top before the move */
    ride_player = !ps.dead && standing_on(m, 1, player.pos);
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        if ((e->type == ET_MONSTER || e->type == ET_CORPSE) && e->onground
            && standing_on(m, ent_hull(e), e->org))
            rides[nr++] = i;
    }
    for (k = 0; k < 3; k++)
        mover_off[m][k] += d[k];
    /* carry the riders */
    if (ride_player) {
        fixed_t p[3];
        for (k = 0; k < 3; k++)
            p[k] = player.pos[k] + d[k];
        if (!g_point_solid(1, p))
            for (k = 0; k < 3; k++)
                player.pos[k] = p[k];
    }
    for (i = 0; i < nr; i++) {
        ent_t *e = &ents[rides[i]];
        for (k = 0; k < 3; k++)
            e->org[k] += d[k];
        g_relink(e);
    }
    /* anyone left inside it is in the way */
    if (!ps.dead && g_mover_solid(m, 1, player.pos)) {
        for (k = 0; k < 3; k++)
            mover_off[m][k] = old[k];
        blocked(m, NULL);
        return;
    }
    for (i = 0; i < MAX_ENTS; i++) {
        ent_t *e = &ents[i];
        if (e->type == ET_MONSTER && g_mover_solid(m, ent_hull(e), e->org)) {
            for (k = 0; k < 3; k++)
                mover_off[m][k] = old[k];
            blocked(m, e);
            return;
        }
    }
    if (--s->ticks == 0)
        arrive(m, s->done);
}

GCODE void mv_tick(void)
{
    int m;
    for (m = 0; m < nmovers; m++) {
        mstate_t *s = &ms[m];
        if (s->ticks)
            move_one(m);
        if (s->think && level_time >= s->think) {
            int fn = s->think_fn;
            s->think = 0;
            arrive(m, fn);
        }
    }
}

/* ------------------------------------------------------------ touching */

static int box_touch(const fixed_t lo[3], const fixed_t hi[3], const int16_t *mins, const int16_t *maxs,
                     const fixed_t *off)
{
    int k;
    for (k = 0; k < 3; k++) {
        fixed_t o = off ? off[k] : 0;
        if (lo[k] > FX(maxs[k]) + o || hi[k] < FX(mins[k]) + o)
            return 0;
    }
    return 1;
}

static void player_box(fixed_t lo[3], fixed_t hi[3])
{
    lo[0] = player.pos[0] - FX(16); hi[0] = player.pos[0] + FX(16);
    lo[1] = player.pos[1] - FX(16); hi[1] = player.pos[1] + FX(16);
    lo[2] = player.pos[2] - FX(24); hi[2] = player.pos[2] + FX(32);
}

static void teleport_player(int t)
{
    const ltrigger_t *l = &ltrigs[t];
    fixed_t c[3];
    c[0] = player.pos[0]; c[1] = player.pos[1]; c[2] = player.pos[2];
    g_teleport_fx(c);
    player.pos[0] = FX(l->dest[0]);
    player.pos[1] = FX(l->dest[1]);
    player.pos[2] = FX(l->dest[2]);
    player.yaw = ANG(l->dest_yaw);
    player.pitch = 0;
    player.vz = 0;
    g_teleport_fx(player.pos);
    snd_start(SND_R_TELE1 + (g_rand() % 5), NULL, 255);
}

GCODE void mv_touch_player(void)
{
    fixed_t lo[3], hi[3];
    int i;
    if (ps.dead)
        return;
    player_box(lo, hi);
    for (i = 0; i < ntrigs; i++) {
        const ltrigger_t *l = &ltrigs[i];
        tstate_t *s = &ts[i];
        if (s->dead || !box_touch(lo, hi, l->mins, l->maxs, NULL))
            continue;
        switch (l->cls) {
        case TR_ONCE: case TR_MULTIPLE: case TR_SECRET:
            if (l->health || (l->flags & TR_NOTOUCH))
                break;
            if (l->facing != -1000) {
                /* only when facing the way the trigger points */
                uint16_t d = (uint16_t)player.yaw - ANG(l->facing);
                if (d > ANG(90) && d < (uint16_t)ANG(270))
                    break;
            }
            multi_trigger(i);
            break;
        case TR_DOOR_FIELD:
            if (level_time >= s->touch_finished) {
                s->touch_finished = level_time + TICKS(1);
                door_use(l->owner);
            }
            break;
        case TR_PLAT_FIELD: {
            int m = l->owner;
            if (ms[m].state == MS_BOTTOM && !ms[m].ticks)
                plat_go_up(m);
            else if (ms[m].state == MS_TOP)
                set_think(m, TICKS(1), A_PLAT_DOWN);       /* not while someone is on it */
            break;
        }
        case TR_TELEPORT:
            teleport_player(i);
            break;
        }
    }
    /* buttons, and the messages doors and secret doors show when touched */
    lo[0] -= FX(1); lo[1] -= FX(1); lo[2] -= FX(1);
    hi[0] += FX(1); hi[1] += FX(1); hi[2] += FX(1);
    for (i = 0; i < nmovers; i++) {
        const lmover_t *l = &lmovers[i];
        mstate_t *s = &ms[i];
        if (s->dead || !box_touch(lo, hi, l->mins, l->maxs, mover_off[i]))
            continue;
        if (l->cls == MV_DOOR && (lmovers[l->group].flags & (MVF_SILVER_KEY | MVF_GOLD_KEY))
            && !ms[l->group].unlocked) {
            /* door_touch: a key door opens for its key, which it takes */
            int lead = l->group;
            int key = lmovers[lead].flags & MVF_GOLD_KEY ? IT_KEY2 : IT_KEY1;
            if (ps.items & key) {
                /* the key goes, and opens this door and any other locked
                   for it the player is touching (the halves of a double
                   door are separate doors) */
                int j, flag = lmovers[lead].flags & (MVF_SILVER_KEY | MVF_GOLD_KEY);
                ps.items &= ~key;
                for (j = 0; j < nmovers; j++) {
                    const lmover_t *o = &lmovers[j];
                    if (o->cls == MV_DOOR && o->group == j && (o->flags & flag) && !ms[j].unlocked
                        && (j == lead || box_touch(lo, hi, o->mins, o->maxs, mover_off[j]))) {
                        ms[j].unlocked = 1;
                        door_use(j);
                    }
                }
            } else if (level_time >= ms[lead].touch_finished) {
                fixed_t c[3];
                ms[lead].touch_finished = level_time + TICKS(2);
                ps.msg = key == IT_KEY2 ? "You need the gold key" : "You need the silver key";
                ps.msg_until = level_time + TICKS(2);
                center(i, c);
                snd_start(lvl.hdr->worldtype == 1 ? SND_RUNETRY : lvl.hdr->worldtype == 2 ? SND_BASETRY
                          : SND_MEDTRY, c, 255);
            }
        } else if (l->cls == MV_BUTTON && !l->health) {
            button_fire(i);
        } else if (l->cls == MV_DOOR || l->cls == MV_DOOR_SECRET) {
            int lead = l->cls == MV_DOOR ? l->group : i;
            const lmover_t *ll = &lmovers[lead];
            if (ll->msg && !ms[lead].nomsg && level_time >= ms[lead].touch_finished) {
                ms[lead].touch_finished = level_time + TICKS(2);
                show_message(mv_string(ll->msg));
            }
        }
    }
}

/* monsters open doors too */
GCODE void mv_touch_monster(ent_t *e)
{
    fixed_t lo[3], hi[3];
    int i, k;
    for (k = 0; k < 3; k++) {
        lo[k] = e->org[k] + FX(e->mins[k]);
        hi[k] = e->org[k] + FX(e->maxs[k]);
    }
    for (i = 0; i < ntrigs; i++) {
        const ltrigger_t *l = &ltrigs[i];
        tstate_t *s = &ts[i];
        if (l->cls != TR_DOOR_FIELD || s->dead || level_time < s->touch_finished)
            continue;
        if (box_touch(lo, hi, l->mins, l->maxs, NULL)) {
            s->touch_finished = level_time + TICKS(1);
            door_use(l->owner);
        }
    }
}

/* ------------------------------------------------------------ shooting */

GCODE void mv_damage(int m, int dmg)
{
    (void)dmg;
    if (m < 0 || m >= nmovers || ms[m].dead || !ms[m].shootable)
        return;
    if (lmovers[m].cls == MV_DOOR_SECRET)
        secret_use(m);
    else if (lmovers[m].cls == MV_BUTTON)
        button_fire(m);
    else if (lmovers[m].cls == MV_DOOR)
        door_use(m);
}

/* triggers with health are shot to fire them; returns 1 if one was hit */
GCODE int mv_shoot_triggers(const fixed_t src[3], const fixed_t dir[3], fixed_t maxlen)
{
    int i, k, hit = 0;
    for (i = 0; i < ntrigs; i++) {
        const ltrigger_t *l = &ltrigs[i];
        fixed_t mn[3], mx[3], t;
        if (!l->health || ts[i].dead || !ts[i].shootable)
            continue;
        for (k = 0; k < 3; k++) {
            mn[k] = FX(l->mins[k]);
            mx[k] = FX(l->maxs[k]);
        }
        if (g_ray_box(src, dir, maxlen, mn, mx, &t)) {
            multi_trigger(i);
            hit = 1;
        }
    }
    return hit;
}

/* ------------------------------------------------------------ setup */

GCODE int mv_state(int m)
{
    return ms[m].ticks ? (ms[m].state == MS_DOWN ? MS_DOWN : MS_UP) : ms[m].state;
}

GCODE void mv_init(void)
{
    int m, t, k;
    lmovers = (const lmover_t *)(lvl.base + lvl.hdr->lump[LVL_MOVERS].ofs);
    nmovers = lvl.hdr->lump[LVL_MOVERS].count;
    mkeys = (const int16_t *)(lvl.base + lvl.hdr->lump[LVL_MKEYS].ofs);
    ltrigs = (const ltrigger_t *)(lvl.base + lvl.hdr->lump[LVL_TRIGGERS].ofs);
    ntrigs = lvl.hdr->lump[LVL_TRIGGERS].count;
    strtab = (const uint32_t *)(lvl.base + lvl.hdr->lump[LVL_STRINGS].ofs);
    nstrings = lvl.hdr->lump[LVL_STRINGS].count;
    if (nmovers > MAX_MOVERS) nmovers = MAX_MOVERS;
    if (ntrigs > MAX_TRIGGERS) ntrigs = MAX_TRIGGERS;
    memset(ms, 0, sizeof(ms));
    memset(ts, 0, sizeof(ts));
    memset(rs_valid, 0, sizeof(rs_valid));
    for (m = 0; m < nmovers; m++) {
        const lmover_t *l = &lmovers[m];
        for (k = 0; k < 3; k++)
            mover_off[m][k] = FX(l->pos[l->startpos][k]);
        ms[m].state = MS_BOTTOM;
        if (l->cls == MV_PLAT && l->startpos == 0)
            ms[m].state = MS_UP;    /* waits at the top to be used */
        ms[m].shootable = l->health > 0;
        /* a train with no name sets off by itself */
        if (l->cls == MV_TRAIN && !l->targetname)
            set_think(m, 6, A_TRAIN_NEXT);
    }
    ps.total_secrets = 0;
    for (t = 0; t < ntrigs; t++) {
        ts[t].count = ltrigs[t].count;
        ts[t].shootable = ltrigs[t].health > 0;
        if (ltrigs[t].cls == TR_SECRET)
            ps.total_secrets++;
    }
}

/* the renderer draws each mover's nearest keyframe, shifted by the rest */
GCODE void mv_render_state(void)
{
    int m, k, i;
    for (m = 0; m < nmovers; m++) {
        const lmover_t *l = &lmovers[m];
        const int16_t *key = mkeys + l->firstkey * 3;
        int best = 0;
        uint32_t bestd = 0xFFFFFFFF;
        if (rs_valid[m]) {
            /* not moved since last frame: its keyframe and the rest stand */
            const int16_t *kk = key + view.mkey[m] * 3;
            if (mover_off[m][0] == FX(kk[0]) + view.mres[m][0] && mover_off[m][1] == FX(kk[1]) + view.mres[m][1]
                && mover_off[m][2] == FX(kk[2]) + view.mres[m][2])
                continue;
        }
        rs_valid[m] = 1;
        for (i = 0; i < l->nkeys; i++, key += 3) {
            int32_t dx = UNITS(mover_off[m][0]) - key[0], dy = UNITS(mover_off[m][1]) - key[1];
            int32_t dz = UNITS(mover_off[m][2]) - key[2];
            uint32_t d = dx * dx + dy * dy + dz * dz;
            if (d < bestd) {
                bestd = d;
                best = i;
            }
        }
        key = mkeys + (l->firstkey + best) * 3;
        view.mkey[m] = best;
        for (k = 0; k < 3; k++)
            view.mres[m][k] = mover_off[m][k] - FX(key[k]);
    }
    r_mover_cam(nmovers);
}

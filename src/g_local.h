/* Game logic: entities, monsters, items, weapons, particles, sound calls */
#ifndef G_LOCAL_H
#define G_LOCAL_H

#include "game.h"
#include "g_ids.h"

/* the game code is cold: it runs from cartridge ROM through the cache */
#ifndef HOST
#define GCODE __attribute__((section(".romtext")))
#else
#define GCODE
#endif

#define TICKS(s)    ((int)((s) * 60))          /* seconds -> 60 Hz ticks */
#define UNITS(f)    ((f) >> 16)
#define FX(u)       ((fixed_t)(u) << 16)
#define ANG(deg)    ((int)((deg) * 65536 / 360) & 0xFFFF)

/* level entity record (ROM), written by tools/mkents.py */
typedef struct {
    uint8_t cls, variant;
    int16_t angle;
    int16_t org[3];
    int16_t half[3];
    int16_t pad;
} lent_t;

/* model header (ROM) */
typedef struct {
    uint16_t numverts, numtris, numframes, numskins;
    fixed_t  scale[3], origin[3];
    int16_t  radius, flags;
    uint32_t tris_ofs, colors_ofs, frames_ofs, bbox_ofs;   /* from this header */
} lmodel_t;

#define MF_ROTATE 8

/* a door, secret door, plat or button (ROM), written by tools/mkents.py */
typedef struct {
    uint8_t  cls, sounds, targetname, target;
    uint8_t  msg, group, nkeys, startpos;   /* group: the linked doors' leader */
    uint16_t flags, firstkey;               /* keyframes in LVL_MKEYS */
    int16_t  speed, wait10, dmg, health;    /* wait in tenths, -1 = stay */
    int16_t  hull1, hull2;                  /* clip hull head nodes */
    int16_t  mins[3], maxs[3];              /* the brush at offset 0 */
    int16_t  pos[3][3];                     /* path points (offsets) */
    uint16_t pad;
} lmover_t;                                                         /* 56 */

/* a trigger volume (ROM) */
typedef struct {
    uint8_t  cls, sounds, targetname, target, killtarget, msg;
    uint16_t flags;
    int16_t  wait10, health, count, owner;  /* owner: the door/plat of a field */
    int16_t  mins[3], maxs[3];
    int16_t  facing;                        /* yaw the player must face, -1000 any */
    int16_t  dest[3], dest_yaw;             /* teleporters */
    uint16_t pad;
} ltrigger_t;                                                       /* 40 */

#define MAX_MOVERS   48
#define MAX_TRIGGERS 64

/* ------------------------------------------------------------------ entities */

enum { ET_FREE, ET_ITEM, ET_MONSTER, ET_CORPSE };

struct anim_s;

typedef struct ent_s {
    uint8_t  type, cls, model, skin;
    uint8_t  frame, variant, onground, leaping;
    int16_t  health;
    int16_t  queue;                  /* index of its level record, -1 if none */
    fixed_t  org[3];
    fixed_t  vel[3];
    int16_t  mins[3], maxs[3];       /* whole units, relative to org */
    uint16_t yaw, ideal_yaw;
    /* monsters */
    const struct anim_s *anim;
    uint8_t  animpos, attack_state, hunting, lefty;
    uint32_t nextthink;
    uint32_t attack_finished, pain_finished, died;
    uint32_t spawned;
    uint32_t seen;                   /* last tick it was near or in sight */
    int16_t  leaf;                   /* BSP leaf of org (g_relink) */
    uint8_t  light, lightdirty;      /* cached light probe for drawing */
} ent_t;

#define MAX_ENTS     72
#define MAX_CORPSES  4               /* older bodies are removed */

/* attack states (as in QuakeC) */
enum { AS_STRAIGHT, AS_SLIDING, AS_MELEE, AS_MISSILE };

/* ------------------------------------------------------------------ player */

#define IT_SHOTGUN      1
#define IT_SUPER_SHOTGUN 2
#define IT_NAILGUN      4

typedef struct {
    int16_t  health, armor;
    int16_t  armortype;              /* x100: 30, 60, 80 */
    int16_t  shells, nails;
    uint8_t  weapons, weapon;        /* bitmask, current IT_ */
    uint8_t  weaponframe, dead;
    uint32_t attack_finished, weapon_tick;
    uint32_t quad_finished, suit_finished;
    uint32_t pain_finished, died;
    int16_t  kills, total_monsters, items_got;
    int16_t  secrets, total_secrets;
    const char *msg;
    uint32_t msg_until;
} pstate_t;

/* ------------------------------------------------------------------ particles */

typedef struct {
    fixed_t  org[3], vel[3];
    uint16_t die;                    /* tick (low 16 bits) when it dies */
    uint8_t  color, type;            /* type: 0 static, 1 slow gravity, 2 gravity;
                                        PT_VISIBLE: in a potentially visible leaf */
    int16_t  leaf;                   /* where it was emitted */
} particle_t;

#define PT_VISIBLE 0x80

#define MAX_PARTICLES 160

/* ------------------------------------------------------------------ drawing */

/* something the renderer draws this frame (filled in by the master) */
typedef struct {
    const lmodel_t *mdl;
    int      frame, skin;
    fixed_t  org[3];
    uint16_t yaw, pitch;
    int16_t  rect[4];                /* x0, y0, x1, y1 in window pixels */
    uint8_t  light;                  /* inverted light 0 (bright) .. 255 */
    uint8_t  mode;                   /* DM_ below */
    uint16_t dist;                   /* distance in units, for sorting */
} drawent_t;

/* DM_DEPTH: tested against the world in the depth intervals. DM_SORTED:
 * too big for the depth budget (close up), its triangles drawn back to
 * front over everything. DM_GUN: the view weapon, sorted, drawn last. */
enum { DM_DEPTH, DM_SORTED, DM_GUN };

#define MAX_DRAW 24

typedef struct {
    int16_t x0, x1;                  /* depth-buffered interval of this row */
    uint16_t ofs;                    /* into zbuf */
} zrow_t;

#define ZBUF_MAX 6144                /* depth samples for the whole frame */

typedef struct {
    int       ndraw;
    drawent_t draw[MAX_DRAW];
    int       nparts;                /* particles drawn this frame */
    zrow_t    zrow[SCREENH];
    int       zused;
    uint16_t *zbuf;
} scene_t;

extern scene_t scene;
extern ent_t ents[MAX_ENTS];
extern pstate_t ps;
extern particle_t particles[MAX_PARTICLES];
extern uint32_t level_time;
extern const lent_t *lents;
extern int nlents;

/* g_main.c */
void g_init(void);
void g_tick(int buttons, int pressed);
void g_build_scene(void);
const lmodel_t *g_model(int i);
int  g_rand(void);                   /* 0..32767 */
fixed_t g_frand(void);               /* 0 .. 1 (16.16) */
fixed_t g_crand(void);               /* -1 .. 1 */
void g_damage_player(int dmg, const fixed_t *from);
void g_damage(ent_t *e, int dmg);
void g_hurt(ent_t *e, int dmg);
void g_particles(const fixed_t org[3], const fixed_t dir[3], int color, int count);
void g_teleport_fx(const fixed_t org[3]);
ent_t *g_spawn(void);
void g_free(ent_t *e);
int  g_monster_cap(void);
uint16_t vectoyaw(fixed_t dx, fixed_t dy);
int  vlen_units(const fixed_t a[3], const fixed_t b[3]);
int  g_box_blocked(fixed_t x, fixed_t y, fixed_t z);
int  g_shot_would_hit(const ent_t *e);
int  g_hud_text(char *buf);
extern int g_level_done;
extern uint32_t g_level_ticks;

/* g_world.c: collision */
typedef struct {
    fixed_t frac;                    /* 0..1 */
    fixed_t end[3];
    fixed_t normal[3];
    uint8_t allsolid, startsolid, inopen, inwater;
    int     mover;                   /* the mover hit, -1 none */
} trace_t;

int  g_contents(int hull, const fixed_t p[3]);
void g_trace(int hull, const fixed_t start[3], const fixed_t end[3], trace_t *tr);
int  g_ray_box(const fixed_t start[3], const fixed_t dir[3], fixed_t maxlen,
               const fixed_t mins[3], const fixed_t maxs[3], fixed_t *tout);
int  g_light_point(const fixed_t p[3]);
int  g_leaf_visible(const fixed_t p[3]);
void g_relink(ent_t *e);
#define LEAF_VISIBLE(l) ((l) > 0 && BIT_TEST(vis_leafs, (l)))

/* g_mover.c: doors, plats, buttons, triggers */
extern const lmover_t *lmovers;
extern int nmovers;
extern fixed_t mover_off[MAX_MOVERS][3];
void mv_init(void);
void mv_tick(void);                  /* move movers, fire timers */
void mv_touch_player(void);          /* triggers, buttons, door messages */
void mv_touch_monster(ent_t *e);     /* door fields */
void mv_damage(int mover, int dmg);  /* shot: secret doors */
int  mv_shoot_triggers(const fixed_t src[3], const fixed_t dir[3], fixed_t maxlen);
void mv_render_state(void);          /* keyframes and offsets for the renderer */
int  mv_state(int mover);            /* MS_ below */
const char *mv_string(int id);
enum { MS_TOP, MS_BOTTOM, MS_UP, MS_DOWN };
int  g_point_solid(int hull, const fixed_t p[3]);   /* world or a mover */
int  g_mover_solid(int m, int hull, const fixed_t p[3]);

/* g_monst.c */
void m_spawn(ent_t *e, int cls, int hunting);
void m_think(ent_t *e);
void m_physics(ent_t *e);
void m_pain(ent_t *e, int dmg);
void m_die(ent_t *e);
int  m_hull(const ent_t *e);

/* snd.c */
void snd_init(void);
void snd_start(int sound, const fixed_t *org, int vol);   /* vol 0..255 */
void snd_update(void);
void snd_init_slave(void);           /* on the slave: PWM, DMA, interrupt */

#endif

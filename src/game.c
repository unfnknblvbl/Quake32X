/* Player movement: walking with gravity, or flying, against the player clip hull */
#include "game.h"

player_t player;
settings_t cfg = { 1, 1, 6, 1, 0, 0, 0 };

const int far_levels[NUM_FAR] = { 256, 384, 512, 768, 1024, 1536, 2048, 3072, 8192 };
/* screen sizes, like Quake's viewsize: the same view, smaller (10:7 window) */
const viewsize_t view_sizes[NUM_SIZES] = {
    { 160, 112, 100 }, { 144, 100, 90 }, { 128, 90, 80 },
    { 112, 78, 70 }, { 96, 68, 60 }, { 80, 56, 50 },
};

#define GRAVITY   (800 * 65536 / 3600)     /* units / tick^2 */
#define JUMPV     (270 * 65536 / 60)
#define MAXFALL   (-1000 * 65536 / 60)
#define RUNSPEED  (300 * 65536 / 60)
#define FLYSPEED  (400 * 65536 / 60)
#define TURNSPEED (160 * 65536 / 360 / 60)
#define LOOKSPEED (120 * 65536 / 360 / 60)
#define STEPSIZE  (18 << 16)
#define VIEWHEIGHT (22 << 16)

static int hull_contents(const fixed_t p[3])
{
    int num = lvl.hdr->headnode1;
    while (num >= 0) {
        const lclipnode_t *c = &lvl.clipnodes[num];
        const lplane_t *pl = &lvl.planes[c->plane];
        fixed_t d = dot16(pl->n, p) - pl->dist;
        num = c->child[d < 0];
    }
    return num;
}

static int is_solid(fixed_t x, fixed_t y, fixed_t z)
{
    fixed_t p[3];
    p[0] = x; p[1] = y; p[2] = z;
    return hull_contents(p) == CONTENTS_SOLID;
}

void game_init(void)
{
    player.pos[0] = lvl.hdr->spawn[0];
    player.pos[1] = lvl.hdr->spawn[1];
    player.pos[2] = lvl.hdr->spawn[2];
    player.yaw = (lvl.hdr->yaw * 65536 / 360) & 0xFFFF;
    player.pitch = 0;
    player.vz = 0;
    player.onground = 0;
#ifdef START_POS
    {   /* benchmark builds: -DSTART_POS=x,y,z,yaw,pitch (units, degrees) */
        static const int sp[5] = { START_POS };
        player.pos[0] = sp[0] << 16;
        player.pos[1] = sp[1] << 16;
        player.pos[2] = sp[2] << 16;
        player.yaw = (sp[3] * 65536 / 360) & 0xFFFF;
        player.pitch = sp[4] * 65536 / 360;
    }
#endif
}

/* try a horizontal move, stepping up stairs when walking */
static int try_move(fixed_t dx, fixed_t dy)
{
    fixed_t *p = player.pos;
    fixed_t nx = p[0] + dx, ny = p[1] + dy;
    int k;

    if (!is_solid(nx, ny, p[2])) {
        p[0] = nx; p[1] = ny;
        return 1;
    }
    if (cfg.fly || !player.onground)
        return 0;
    if (is_solid(p[0], p[1], p[2] + STEPSIZE) || is_solid(nx, ny, p[2] + STEPSIZE))
        return 0;
    p[0] = nx; p[1] = ny;
    p[2] += STEPSIZE;
    for (k = 1; k <= 18; k++)
        if (is_solid(p[0], p[1], p[2] - (k << 16)))
            break;
    p[2] -= (k - 1) << 16;
    return 1;
}

static void move_horiz(fixed_t dx, fixed_t dy)
{
    if (!dx && !dy)
        return;
    if (try_move(dx, dy))
        return;
    if (dx && try_move(dx, 0))
        return;
    if (dy)
        try_move(0, dy);
}

/* move vertically; returns 0 if blocked */
static int move_vert(fixed_t dz)
{
    fixed_t *p = player.pos;
    fixed_t step;
    if (!is_solid(p[0], p[1], p[2] + dz)) {
        p[2] += dz;
        return 1;
    }
    step = dz < 0 ? -0x10000 : 0x10000;
    while ((dz < 0 ? dz <= step : dz >= step) && !is_solid(p[0], p[1], p[2] + step)) {
        p[2] += step;
        dz -= step;
    }
    return 0;
}

void game_tick(int buttons)
{
    fixed_t cy = fcos(player.yaw), sy = fsin(player.yaw);
    fixed_t speed = cfg.fly ? FLYSPEED : RUNSPEED;
    fixed_t fwd = 0, side = 0;

    if (buttons & BTN_A) {
        /* A held: left/right strafe, up/down look */
        if (buttons & BTN_LEFT)  side -= speed;
        if (buttons & BTN_RIGHT) side += speed;
        if (buttons & BTN_UP)    player.pitch -= LOOKSPEED;
        if (buttons & BTN_DOWN)  player.pitch += LOOKSPEED;
        if (player.pitch < -ANGLE_90 * 7 / 9) player.pitch = -ANGLE_90 * 7 / 9;
        if (player.pitch >  ANGLE_90 * 7 / 9) player.pitch =  ANGLE_90 * 7 / 9;
    } else {
        if (buttons & BTN_LEFT)  player.yaw += TURNSPEED;
        if (buttons & BTN_RIGHT) player.yaw -= TURNSPEED;
        if (buttons & BTN_UP)    fwd += speed;
        if (buttons & BTN_DOWN)  fwd -= speed;
        player.yaw &= 0xFFFF;
    }
    /* right vector is (sin, -cos) */
    move_horiz(fixmul(fwd, cy) + fixmul(side, sy), fixmul(fwd, sy) - fixmul(side, cy));

    if (cfg.fly) {
        fixed_t dz = 0;
        if (buttons & BTN_B) dz += speed;
        if (buttons & BTN_C) dz -= speed;
        if (dz) move_vert(dz);
        player.vz = 0;
        return;
    }
    if (player.onground && (buttons & BTN_B)) {
        player.vz = JUMPV;
        player.onground = 0;
    }
    player.vz -= GRAVITY;
    if (player.vz < MAXFALL) player.vz = MAXFALL;
    if (!move_vert(player.vz)) {
        if (player.vz < 0) player.onground = 1;
        player.vz = 0;
    } else {
        player.onground = is_solid(player.pos[0], player.pos[1], player.pos[2] - 0x10000);
        if (player.onground && player.vz < 0) player.vz = 0;
    }
}

void game_eye(fixed_t eye[3])
{
    eye[0] = player.pos[0];
    eye[1] = player.pos[1];
    eye[2] = player.pos[2] + VIEWHEIGHT;
}

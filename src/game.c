/* Player movement: walking with gravity, or flying, against the player clip hull */
#include "g_local.h"

player_t player;
settings_t cfg = { 1, 1, 6, 1, 0, 0, 0, 2, 0, { BTN_C, BTN_B, BTN_A, 0 }, 0,
                   0, 0, 3, 3, 0, 1, 0, { AN_RT, AN_LT, BTN_C } };
analog_t an_in;

/* the analogue pad's buttons -> actions, as set on its own page (its stick
   moves the player, so the d-pad bits it also sets are for the menus) */
uint16_t map_analog(uint16_t raw)
{
    uint16_t out = raw & BTN_START;
    if (raw & cfg.an_bind[BIND_FIRE]) out |= ACT_FIRE;
    if (raw & cfg.an_bind[BIND_JUMP]) out |= ACT_JUMP;
    if (raw & cfg.an_bind[2]) out |= ACT_WEAPON;
    return out;
}

/* degrees a second at full stick, per speed step */
static const uint16_t an_turn_dps[NUM_AN_SPEEDS] = { 90, 120, 150, 180, 210, 240, 300, 360 };
static const uint16_t an_look_dps[NUM_AN_SPEEDS] = { 60, 80, 100, 120, 150, 180, 210, 240 };

/* pad buttons -> the game's action bits, as set on the Controls page */
uint16_t map_pad(uint16_t raw)
{
    uint16_t out = raw & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_START);
    if (raw & cfg.bind[BIND_FIRE]) out |= ACT_FIRE;
    if (raw & cfg.bind[BIND_JUMP]) out |= ACT_JUMP;
    if (raw & cfg.bind[BIND_STRAFE]) out |= ACT_STRAFE;
    if (cfg.bind[BIND_WEAPON] && (raw & cfg.bind[BIND_WEAPON])) out |= ACT_WEAPON;
    return out;
}

const int far_levels[NUM_FAR] = { 256, 384, 512, 768, 1024, 1536, 2048, 3072, 8192 };
/* screen sizes, like Quake's viewsize: the same view, smaller (10:7 window) */
const viewsize_t view_sizes[NUM_SIZES] = {
    /* on the Mega Drive's 8-pixel grid: the border is made of its tiles */
    { 160, 104, 100 }, { 144, 96, 90 }, { 128, 80, 80 },
    { 112, 72, 70 }, { 96, 64, 60 }, { 80, 56, 50 },
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

/* the player's clip hull: the world and the doors, plats and buttons */
static int is_solid(fixed_t x, fixed_t y, fixed_t z)
{
    fixed_t p[3];
    p[0] = x; p[1] = y; p[2] = z;
    return g_point_solid(1, p);
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

    if (!cfg.fly && g_box_blocked(nx, ny, p[2]))
        return 0;                       /* a monster is in the way */
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

/* blocked: creep up to the obstacle in halving steps, so the player ends
   up flush against walls and buttons (like Quake's clip to the impact) */
static void creep(fixed_t dx, fixed_t dy)
{
    int k;
    for (k = 0; k < 4; k++) {
        dx /= 2;
        dy /= 2;
        if (!is_solid(player.pos[0] + dx, player.pos[1] + dy, player.pos[2])) {
            player.pos[0] += dx;
            player.pos[1] += dy;
        }
    }
}

static void move_horiz(fixed_t dx, fixed_t dy)
{
    if (!dx && !dy)
        return;
    if (try_move(dx, dy))
        return;
    /* up to the obstacle, then slide along it */
    if (cfg.fly || !g_box_blocked(player.pos[0] + dx, player.pos[1] + dy, player.pos[2]))
        creep(dx, dy);
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
    if (ps.dead)
        buttons = 0;                    /* dead: fall, but no control */
    fixed_t cy = fcos(player.yaw), sy = fsin(player.yaw);
    fixed_t speed = cfg.fly ? FLYSPEED : RUNSPEED;
    fixed_t fwd = 0, side = 0;

    if (buttons & ACT_STRAFE) {
        /* strafe held: left/right strafe, up/down look */
        int look = cfg.invert ? -LOOKSPEED : LOOKSPEED;
        if (buttons & BTN_LEFT)  side -= speed;
        if (buttons & BTN_RIGHT) side += speed;
        if (buttons & BTN_UP)    player.pitch -= look;
        if (buttons & BTN_DOWN)  player.pitch += look;
        if (player.pitch < -ANGLE_90 * 7 / 9) player.pitch = -ANGLE_90 * 7 / 9;
        if (player.pitch >  ANGLE_90 * 7 / 9) player.pitch =  ANGLE_90 * 7 / 9;
    } else {
        if (buttons & BTN_LEFT)  player.yaw += TURNSPEED;
        if (buttons & BTN_RIGHT) player.yaw -= TURNSPEED;
        if (buttons & BTN_UP)    fwd += speed;
        if (buttons & BTN_DOWN)  fwd -= speed;
        player.yaw &= 0xFFFF;
    }
    if (an_in.active && !ps.dead) {
        /* the analogue pad: as far as the sticks are pushed */
        fwd += fixmul(speed, an_in.fwd);
        side += fixmul(speed, an_in.side);
        player.yaw -= fixmul(an_turn_dps[cfg.an_turn] * 65536 / 360 / 60, an_in.turn);
        player.yaw &= 0xFFFF;
        player.pitch += fixmul(an_look_dps[cfg.an_look] * 65536 / 360 / 60, an_in.look);
        if (player.pitch < -ANGLE_90 * 7 / 9) player.pitch = -ANGLE_90 * 7 / 9;
        if (player.pitch >  ANGLE_90 * 7 / 9) player.pitch =  ANGLE_90 * 7 / 9;
        if (fwd > speed) fwd = speed;
        if (fwd < -speed) fwd = -speed;
        if (side > speed) side = speed;
        if (side < -speed) side = -speed;
    }
    /* right vector is (sin, -cos) */
    move_horiz(fixmul(fwd, cy) + fixmul(side, sy), fixmul(fwd, sy) - fixmul(side, cy));

    if (cfg.fly) {
        fixed_t dz = 0;
        if (buttons & ACT_JUMP) dz += speed;
        if (buttons & ACT_FIRE) dz -= speed;
        if (dz) move_vert(dz);
        player.vz = 0;
        return;
    }
    if (player.onground && (buttons & ACT_JUMP)) {
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
    eye[2] = player.pos[2] + (ps.dead ? (8 << 16) : VIEWHEIGHT);   /* dead: on the floor */
}

/* back to the start of the level */
void game_respawn(void)
{
    game_init();
}

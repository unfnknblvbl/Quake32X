/* The walkthrough demo's player: works the pause menu at the start, then
 * follows a planned route to the exit, stopping to fight whatever is in
 * sight. It plays through the pad, like a person would: the D-pad to turn
 * and walk, C to fire. Its shots get Quake's normal vertical auto-aim; its
 * only other help is cfg.god (it takes a quarter of the damage). */
#ifdef DEMO_BOT
#include "g_local.h"
#include "bot.h"

#define TURN_STEP  ANG(2.67)            /* game.c turns 160 degrees a second */

static int wp, stuck_ticks, last_x, last_y, fighting, phase;
static int target = -1, retarget, detour = -1, detour_t, unstick;
static uint32_t ignore_until[MAX_ENTS];  /* targets it could not hurt */
static int target_hp, target_since;

/* the menu: open it, switch to flat shading, show the monster setting */
static const struct { int t; uint16_t b; } menu_script[] = {
    { 40, BTN_START },                  /* open the pause menu */
    { 85, BTN_RIGHT },                  /* Textures: OFF (flat) */
    { 115, BTN_DOWN }, { 130, BTN_DOWN }, { 145, BTN_DOWN }, { 160, BTN_DOWN },
    { 175, BTN_DOWN },                  /* Monsters at once: 2 */
    { 290, BTN_START },                 /* resume */
    { -1, 0 }
};
static int menu_i;
int bot_skip_menu;
int bot_dbg_wp(void) { return wp | (detour + 1) << 8; }

void bot_init(void)
{
    wp = 0;
    menu_i = 0;
    cfg.god = 1;
    phase = 1;
#ifdef BOT_CAP
    cfg.monsters = BOT_CAP;             /* measurement builds: another cap */
#endif
}

static int visible_from_eye(const fixed_t c[3])
{
    fixed_t e[3];
    trace_t tr;
    game_eye(e);
    g_trace(0, e, c, &tr);
    return tr.frac == 0x10000;
}

static int pick_target(void)
{
    int i, best = -1, bestd = 550;          /* shotgun range */
    for (i = 0; i < MAX_ENTS; i++) {
        const ent_t *e = &ents[i];
        fixed_t c[3];
        int d;
        if (e->type != ET_MONSTER || e->health <= 0 || level_time < ignore_until[i])
            continue;
        c[0] = e->org[0]; c[1] = e->org[1]; c[2] = e->org[2] + FX(8);
        d = vlen_units(c, player.pos);
        if (d >= bestd || !visible_from_eye(c))
            continue;
        best = i;
        bestd = d;
    }
    return best;
}

/* A + up/down to look toward pitch (positive looks down) */
static void look_toward(int pitch, uint16_t *held)
{
    int err = pitch - player.pitch;
    if (err > ANG(3))
        *held |= BTN_A | BTN_DOWN;
    else if (err < -ANG(3))
        *held |= BTN_A | BTN_UP;
}

/* press left or right to turn toward yaw; returns the remaining error */
static int turn_toward(uint16_t yaw, uint16_t *held)
{
    int16_t err = (int16_t)(yaw - (uint16_t)player.yaw);
    if (err > TURN_STEP / 2)
        *held |= BTN_LEFT;
    else if (err < -TURN_STEP / 2)
        *held |= BTN_RIGHT;
    return err < 0 ? -err : err;
}

/* menu presses, once a frame (real time: the game is paused meanwhile) */
void bot_menu(uint32_t now, uint16_t *pressed)
{
    static uint32_t t0;
    if (bot_skip_menu) {
        while (menu_script[menu_i].t >= 0) menu_i++;
        return;
    }
    if (!t0)
        t0 = now ? now : 1;
    if (menu_script[menu_i].t >= 0 && (int)(now - t0) >= menu_script[menu_i].t) {
        *pressed |= menu_script[menu_i].b;
        menu_i++;
    }
}

void bot_tick(uint16_t *held, uint16_t *pressed)
{
    *held = 0;
    if (menu_script[menu_i].t >= 0)
        return;                             /* still working the menu */
    if (g_level_done) {
        phase = 4;
        MARS_SYS_COMM0 = phase;
        return;
    }
    if (ps.dead) {
        if (level_time - ps.died > TICKS(3))
            *pressed |= BTN_B;              /* start the level again */
        wp = 0;
        return;
    }

    /* fight: keep the current target while it is alive and in sight */
    if (--retarget <= 0 || target < 0 || ents[target].type != ET_MONSTER) {
        target = ps.shells > 0 ? pick_target() : -1;    /* no shells: just run */
        retarget = 10;
    }
    if (target >= 0) {
        const ent_t *e = &ents[target];
        fixed_t eye[3];
        uint16_t yaw = vectoyaw(e->org[0] - player.pos[0], e->org[1] - player.pos[1]);
        int d, err, pitch;
        fixed_t dxy[3];
        game_eye(eye);
        dxy[0] = e->org[0]; dxy[1] = e->org[1]; dxy[2] = eye[2];
        d = vlen_units(dxy, eye);
        pitch = (int16_t)vectoyaw(FX(d), eye[2] - e->org[2] - FX(8));
        err = turn_toward(yaw, held);
        if (target_since == 0 || e->health != target_hp) {
            target_hp = e->health;
            target_since = level_time;
        } else if (level_time - target_since > TICKS(5)) {
            /* five seconds without a hit: out of reach, leave it */
            ignore_until[target] = level_time + TICKS(10);
            target = -1;
            target_since = 0;
            return;
        }
        if (err < ANG(4)) {
            /* the auto-aim covers about 22 degrees up and down */
            int perr = pitch - player.pitch;
            if (perr > ANG(15) || perr < -ANG(15)) {
                *held &= ~(BTN_LEFT | BTN_RIGHT);   /* A + left/right would strafe */
                look_toward(pitch, held);
            } else if (level_time >= ps.attack_finished && d < 500 && ps.shells > 0
                       && g_shot_would_hit(e))
                *held |= BTN_C;
        }
        /* it stands its ground: walking at targets leads into pools */
        fighting = 1;
        MARS_SYS_COMM0 = phase = 3;
        return;
    }
    target_since = 0;
    fighting = 0;
    MARS_SYS_COMM0 = phase = 2;

    /* low on shells or health: fetch a useful item in sight */
    if (detour < 0 && (level_time & 15) == 0) {
        int i, bestd = 400;
        for (i = 0; i < MAX_ENTS; i++) {
            const ent_t *e = &ents[i];
            fixed_t c[3];
            int d, dz = UNITS(e->org[2] - player.pos[2]) + 24;
            if (e->type != ET_ITEM || dz < -40 || dz > 40 || level_time < ignore_until[i])
                continue;
            /* backpacks always; shells and health when short */
            if (!(e->cls == CL_BACKPACK || (ps.shells < 30 && e->cls == CL_I_SHELLS)
                  || (ps.health < 70 && e->cls == CL_I_HEALTH)))
                continue;
            c[0] = e->org[0] + (e->mins[0] ? 0 : FX(16));
            c[1] = e->org[1] + (e->mins[1] ? 0 : FX(16));
            c[2] = e->org[2] + FX(24);
            d = vlen_units(c, player.pos);
            if (d < bestd && visible_from_eye(c)) {
                bestd = d;
                detour = i;
            }
        }
        detour_t = level_time;
    }
    if (detour >= 0 && ents[detour].type != ET_ITEM)
        detour = -1;
    if (detour >= 0 && level_time - detour_t > TICKS(5)) {
        ignore_until[detour] = level_time + TICKS(60);     /* could not get there */
        detour = -1;
    }

    /* walk the route (or to the item) */
    if (wp < bot_nroute || detour >= 0) {
        fixed_t gx, gy;
        int jump = 0;
        if (detour >= 0) {
            const ent_t *e = &ents[detour];
            gx = e->org[0] + (e->mins[0] ? 0 : FX(16));
            gy = e->org[1] + (e->mins[1] ? 0 : FX(16));
        } else {
            const bot_wp_t *w = &bot_route[wp];
            fixed_t dx = FX(w->x) - player.pos[0], dy = FX(w->y) - player.pos[1];
            if (UNITS(dx < 0 ? -dx : dx) < 16 && UNITS(dy < 0 ? -dy : dy) < 16) {
                jump = w->flags & WP_JUMP;
                wp++;
                stuck_ticks = 0;
                if (jump)
                    *held |= BTN_B;
                return;
            }
            gx = FX(w->x);
            gy = FX(w->y);
        }
        if (player.pitch > ANG(3) || player.pitch < -ANG(3)) {
            look_toward(0, held);           /* level the view first */
            return;
        }
        if (unstick > 0) {
            /* sidestep out of a corner */
            unstick--;
            *held |= BTN_A | ((stuck_ticks / 100) & 1 ? BTN_RIGHT : BTN_LEFT);
            return;
        }
        {
            int err = turn_toward(vectoyaw(gx - player.pos[0], gy - player.pos[1]), held);
            if (err < ANG(20))
                *held |= BTN_UP;            /* turn in place first, then walk */
        }
        /* stuck: jump, sidestep, and in the end give up on this waypoint */
        if (UNITS(player.pos[0]) == last_x && UNITS(player.pos[1]) == last_y && (*held & BTN_UP)) {
            stuck_ticks++;
            if (stuck_ticks == 20)
                *held |= BTN_B;
            if (stuck_ticks % 50 == 40)
                unstick = 12;
            if (stuck_ticks > 300) {
                if (detour >= 0)
                    detour = -1;
                else if (wp + 1 < bot_nroute)
                    wp++;
                stuck_ticks = 0;
            }
        } else if (stuck_ticks && (level_time & 63) == 0)
            stuck_ticks = 0;
        last_x = UNITS(player.pos[0]);
        last_y = UNITS(player.pos[1]);
    }
}
#endif

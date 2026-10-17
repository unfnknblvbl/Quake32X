/* the demo bot: plays E1M1 to the exit (pause menu, Demo) */
#ifndef BOT_H
#define BOT_H
#include "hw.h"

typedef struct { int16_t x, y, z, flags; } bot_wp_t;

/* waypoint flags */
#define WP_JUMP   1                     /* jump when leaving this point */
#define WP_WAIT   2                     /* wait here until mover (flags >> 8) is at rest */
#define WP_TOUCH  4                     /* a button: walk right into it */
#define WP_LOOK   8                     /* stop and look at yaw (flags >> 8) * 2, down 40 degrees */

extern const bot_wp_t bot_route[];
extern const int bot_nroute;
extern int bot_active;

void bot_start(void);                   /* restart the level with the bot playing */
void bot_stop(void);                    /* hand the controls back */
void bot_tick(uint16_t *held, uint16_t *pressed);

#endif

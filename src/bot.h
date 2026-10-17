/* the demo bot's route (build/bot_data.c, from tools/make_bot.py) */
#ifndef BOT_H
#define BOT_H
#include "hw.h"

typedef struct { int16_t x, y, z, flags; } bot_wp_t;

#define WP_JUMP 1                /* jump when leaving this point */

extern const bot_wp_t bot_route[];
extern const int bot_nroute;

void bot_init(void);
void bot_tick(uint16_t *held, uint16_t *pressed);
void bot_menu(uint32_t now, uint16_t *pressed);

#endif

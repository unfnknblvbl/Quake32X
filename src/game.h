#ifndef GAME_H
#define GAME_H
#include "r_local.h"

typedef struct {
    fixed_t pos[3];
    fixed_t vz;
    int yaw, pitch;
    int onground;
} player_t;

#define NUM_FAR 9
#define NUM_SIZES 6

typedef struct {
    int textures;   /* 0 = flat shaded polygons */
    int lighting;
    int far;        /* index into far_levels */
    int showfps;
    int fly;
    int size;       /* index into view_sizes: 0 = full screen */
    int mesh;       /* liquids drawn as a see-through mesh */
    int monsters;   /* index into the "Monsters at once" values (g_main.c) */
    int god;        /* demo player: takes a quarter of the damage */
    uint16_t bind[4];   /* pad buttons for the actions below; next weapon 0 =
                           strafe + fire together */
    int invert;     /* look up/down inverted */
} settings_t;

enum { BIND_FIRE, BIND_JUMP, BIND_STRAFE, BIND_WEAPON, NUM_BINDS };

/* the game reads actions on these bits (map_pad translates the pad):
   strafe/look = A, jump = B, fire = C, next weapon = X */
#define ACT_STRAFE  BTN_A
#define ACT_JUMP    BTN_B
#define ACT_FIRE    BTN_C
#define ACT_WEAPON  BTN_X
uint16_t map_pad(uint16_t raw);

#define NUM_MONSTER_CAPS 8
#define SBAR_H   8               /* status bar rows at the bottom of the screen (16 lines) */

typedef struct { uint8_t w, h, pct, pad; } viewsize_t;

extern player_t player;
extern settings_t cfg;
extern const int far_levels[NUM_FAR];
extern const viewsize_t view_sizes[NUM_SIZES];

void game_init(void);
void game_tick(int buttons);
void game_eye(fixed_t eye[3]);
void game_respawn(void);

/* menu.c */
extern int menu_active;
void menu_input(int pressed);
int menu_page(void);
void menu_draw(void);
void hud_draw(int col, int row, int fps10);
void hint_draw(int row);
void sbar_draw(const char *buf, int hi, int message);
const char *button_name(uint16_t b);
void intermission_draw(int kills, int total, int secs, int secrets, int tsecrets);
void overlays_draw(int win_x, int win_y, int fps10, int hint);

#endif

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

typedef struct {
    int textures;   /* 0 = flat shaded polygons */
    int lighting;
    int far;        /* index into far_levels */
    int showfps;
    int fly;
} settings_t;

extern player_t player;
extern settings_t cfg;
extern const int far_levels[NUM_FAR];

void game_init(void);
void game_tick(int buttons);
void game_eye(fixed_t eye[3]);

/* menu.c */
extern int menu_active;
void menu_input(int pressed);
void menu_draw(void);
void hud_draw(int fps10, int split);
void draw_text(int x, int y, const char *s, int hi);

#endif

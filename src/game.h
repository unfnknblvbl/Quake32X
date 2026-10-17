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
} settings_t;

typedef struct { uint8_t w, h, pct, pad; } viewsize_t;

extern player_t player;
extern settings_t cfg;
extern const int far_levels[NUM_FAR];
extern const viewsize_t view_sizes[NUM_SIZES];

void game_init(void);
void game_tick(int buttons);
void game_eye(fixed_t eye[3]);

/* menu.c */
extern int menu_active;
void menu_input(int pressed);
void menu_draw(void);
void hud_draw(int x, int y, int fps10, int split);
void hint_draw(void);
void border_draw(int x0, int y0, int w, int h);
void draw_text(int x, int y, const char *s, int hi);

#endif

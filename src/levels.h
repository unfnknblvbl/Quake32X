/* Levels in the cartridge (src/level.c) and the order of play */
#ifndef LEVELS_H
#define LEVELS_H

#define LEVEL_WINDOW ((const void *)0x02080000)

int lvl_count(void);                /* levels in the cartridge, in the order of play */
const char *lvl_file(int n);        /* "e1m1" ... */
const void *lvl_map(int n);         /* map level n in; returns its blob */
int lvl_demo(void);                 /* the level with the attract demo, -1 none */

/* what the game asks the main loop to do between frames */
enum {
    REQ_NONE,
    REQ_NEXT_LEVEL,                 /* the exit: on to the next level (or the end) */
    REQ_RESTART,                    /* the player died: the level starts again */
    REQ_NEW_GAME,                   /* from the menu: the first level */
    REQ_DEMO,                       /* the attract demo */
    REQ_BOT,                        /* the demo bot: the first level, the bot playing */
};
extern int g_request;

/* how the game is being played */
enum { GM_PLAY, GM_DEMO, GM_FINALE };
extern int g_mode;
extern int cur_level;

#endif

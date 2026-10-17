/* Playing back the level's Quake demo: the attract mode (src/playdemo.c) */
#ifndef PLAYDEMO_H
#define PLAYDEMO_H
#include "g_local.h"

extern int pd_active;
int  pd_start(void);                /* 0: the level has no demo */
int  pd_tick(void);                 /* a 60 Hz tick; 0 when it is over */
void pd_update(void);               /* things, movers, view for this frame */
void pd_eye(fixed_t eye[3]);
int  pd_gun(int *frame);            /* the view weapon, -1 none */

#endif

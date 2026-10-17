/* scripted camera demo (DEMO=1 builds): see tools/make_demo.py */
#ifndef DEMO_H
#define DEMO_H
#include "hw.h"

typedef struct { int32_t x, y, z; uint16_t yaw; int16_t pitch; } demo_cam_t;
typedef struct { int16_t dt; uint16_t buttons; } demo_step_t;      /* dt < 0 ends */
typedef struct { int32_t tick; const demo_step_t *steps; } demo_macro_t;
typedef struct { int32_t tick, phase; } demo_phase_t;

extern const demo_cam_t demo_cam[];
extern const int demo_ncam;
extern const demo_macro_t demo_macros[];
extern const demo_phase_t demo_phases[];

#endif

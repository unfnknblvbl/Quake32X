/* Host build of the 32X renderer for debugging and statistics.
 *   q32x_host <level_le.lvl> <out.ppm> x y z yaw pitch [tex light far split]
 * angles in degrees, positions in world units.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../src/r_local.h"

int32_t host_div_result;
int st_reject, st_clipaway, st_occluded, st_vis, st_sphere;
volatile uint16_t *r_fb;
static uint16_t fbmem[SCREENW * SCREENH];

#define SURFCACHE (20 * 1024)

int main(int argc, char **argv)
{
    FILE *f;
    long sz;
    uint8_t *blob;
    static rcpu_t rc[2];
    static surfcache_t *hash[2][HASHSIZE];
    fixed_t org[3];
    int yaw, pitch, leaf, split = SCREENH / 2, frames = 1, k;

    if (argc < 8) {
        fprintf(stderr, "usage: %s level out.ppm x y z yaw pitch [tex light far split frames]\n", argv[0]);
        return 1;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    blob = malloc(sz);
    if (fread(blob, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);

    level_init(blob);
    r_init();
    r_span_init();
    r_fb = fbmem;
    for (k = 0; k < 2; k++) {
        rc[k].cpu = k;
        sc_init(&rc[k], malloc(SURFCACHE), SURFCACHE, hash[k]);
    }

    org[0] = (fixed_t)(atof(argv[3]) * 65536);
    org[1] = (fixed_t)(atof(argv[4]) * 65536);
    org[2] = (fixed_t)(atof(argv[5]) * 65536);
    yaw = (int)(atof(argv[6]) * 65536 / 360) & 0xFFFF;
    pitch = (int)(atof(argv[7]) * 65536 / 360);
    view.textures = argc > 8 ? atoi(argv[8]) : 1;
    view.lighting = argc > 9 ? atoi(argv[9]) : 1;
    view.far = (argc > 10 ? atoi(argv[10]) : 4096) << 16;
    if (argc > 11) split = atoi(argv[11]);
    if (argc > 12) frames = atoi(argv[12]);

    r_setup_view(org, yaw, pitch);
    leaf = r_pointleaf(org);
    r_markleaves(leaf);
    printf("leaf %d contents %d\n", leaf, lvl.leafs[leaf].contents);

    for (k = 0; k < frames; k++) {
        view.time = k;
        r_render(&rc[0], 0, split);
        if (getenv("DUMPGAPS")) { extern void dump_gaps(int,int); dump_gaps(0, split); printf("ncand %d\n", rc[0].ncand); }
        r_render(&rc[1], split, SCREENH);
    }
    for (k = 0; k < 2; k++)
        printf("cpu%d rows %3d-%3d: nodes %4d facecalls %4d faces %4d xforms %4d clips %4d spans %5d pixels %6d surfbuilds %3d texels %6d\n",
               k, rc[k].row0, rc[k].row1, rc[k].nodes, rc[k].facecalls, rc[k].faces, rc[k].vxforms, rc[k].clips,
               rc[k].spancount, rc[k].pixels, rc[k].surfbuilds, rc[k].texels);

    f = fopen(argv[2], "wb");
    fprintf(f, "P6\n320 224\n255\n");
    for (int y = 0; y < 224; y++)
        for (int x = 0; x < 320; x++) {
            int c = fbmem[(y >> 1) * SCREENW + (x >> 1)] & 0xFF;
            uint16_t p = lvl.palette[c];
            fputc((p & 31) << 3, f);
            fputc(((p >> 5) & 31) << 3, f);
            fputc(((p >> 10) & 31) << 3, f);
        }
    fclose(f);
    return 0;
}

#include <stdio.h>
#include "../src/r_local.h"
void dump_gaps(int r0, int r1) {
    for (int r = r0; r < r1; r++) if (gaps[r].n) {
        printf("row %d:", r);
        for (int k = 0; k < gaps[r].n; k++) printf(" [%d,%d)", gaps[r].g[2*k], gaps[r].g[2*k+1]);
        printf("\n");
    }
}

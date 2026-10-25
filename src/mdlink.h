/* The Mega Drive layer: text, the border around the view and the view
 * weapon, drawn by the 68000 (src-md/md.s) on the Mega Drive VDP, over the
 * 32X picture. Screen positions are in 8x8 cells: 40 columns, 28 rows.
 * Text (plane A) sits 4 lines lower than the cell grid. */
#ifndef MDLINK_H
#define MDLINK_H
#include "hw.h"

#define MD_COLS 40
#define MD_ROWS 28

/* plane B cells */
#define MB_NONE   0                 /* the 32X shows through */
#define MB_BLACK  1
#define MB_SHADE  2                 /* dither: darkens what is behind */
#define MB_BORDER 16                /* + 0..63: the border pattern */
#define MB_BORDER_SHADED 80         /* + 0..63: the same, darkened */

/* the 32X rows of the view the weapon hides completely */
typedef struct {
    int row0, nrows;
    const uint8_t *x;               /* nrows pairs: x0, x1 (x1 = 0: none) */
} md_cut_t;

void md_init(void);
void md_pump(void);                 /* feed queued words to the 68000 */
void md_wait(void);                 /* until everything queued is sent */
int  md_busy(void);                 /* words still queued */
void md_pump_for(int n);            /* wait for the 68000 to take up to n words */
void md_bank(int page);
void md_bank_set(int page, int n);     /* the shared sound bank: n pages from page */             /* map cartridge pages page .. page + 6 at 0x02080000 */
void md_sync(void);                 /* until the 68000 has carried out all sent so far */

/* immediate mode: put everything that should be on screen each frame;
 * md_end sends what changed and clears what was not put again */
void md_text(int col, int row, const char *s, int hi);
void md_char(int col, int row, int c);
void md_end_text(void);
/* plane B is rebuilt only when md_begin_back returns 1 (its key changed) */
int  md_begin_back(uint32_t key);
void md_back(int col, int row, int v);
void md_end_back(void);

/* the view weapons (tools/mkmd.py) */
enum { MDW_AXE, MDW_SHOTGUN, MDW_SUPER_SHOTGUN, MDW_NAILGUN, MDW_GRENADE_LAUNCHER };

/* the flat-shaded weapon renders (when the data has them) instead of the
 * textured ones: set while textures are off */
extern int md_gun_flat;

/* the view weapon (MDW_*; frame -1 = none), shown from the next vertical
 * blank */
void md_gun(int weapon, int size, int frame);
/* what the weapon frame hides in the 32X view; 0 if nothing */
int  md_gun_cut(int weapon, int size, int frame, md_cut_t *cut);

#endif

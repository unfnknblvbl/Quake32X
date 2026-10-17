/* Host stand-in for the Mega Drive layer (src/mdlink.c): keeps the text
 * and plane B cells and draws them over the host's RGB frames, with the
 * Quake palette (no weapon sprite, no 3-bit colour). */
#include "../src/g_local.h"
#include "../src/mdlink.h"

volatile uint8_t dbg_phase;        /* main.c's, for the logs */
int g_mode, cur_level;              /* main.c's: how the game is played */
int pad_kind;                       /* main.c's: the host has an ordinary pad */
static uint8_t cur[2][MD_ROWS][MD_COLS], next[MD_ROWS][MD_COLS];
static uint32_t back_key = 0xFFFFFFFF;

void md_init(void) {}
void md_pump(void) {}
void md_wait(void) {}
int md_busy(void) { return 0; }
void md_pump_for(int n) { (void)n; }

void md_char(int col, int row, int c)
{
    if ((c & 0x7F) == ' ')
        c = 0;
    if ((unsigned)col < MD_COLS && (unsigned)row < MD_ROWS)
        next[row][col] = c;
}

void md_text(int col, int row, const char *s, int hi)
{
    for (; *s; s++, col++)
        md_char(col, row, (uint8_t)*s | (hi ? 0x80 : 0));
}

void md_end_text(void)
{
    memcpy(cur[0], next, sizeof(next));
    memset(next, 0, sizeof(next));
}

int md_begin_back(uint32_t key)
{
    if (key == back_key)
        return 0;
    back_key = key;
    memset(cur[1], 0, sizeof(cur[1]));
    return 1;
}

void md_back(int col, int row, int v)
{
    if ((unsigned)col < MD_COLS && (unsigned)row < MD_ROWS)
        cur[1][row][col] = v;
}

void md_end_back(void) {}
void md_gun(int weapon, int size, int frame) { (void)weapon; (void)size; (void)frame; }
int md_gun_cut(int weapon, int size, int frame, md_cut_t *cut) { (void)weapon; (void)size; (void)frame; (void)cut; return 0; }

static void put(uint8_t *rgb, int x, int y, int c)
{
    uint16_t p = lvl.palette[c];
    uint8_t *d = rgb + (y * 320 + x) * 3;
    d[0] = (p & 31) << 3;
    d[1] = ((p >> 5) & 31) << 3;
    d[2] = ((p >> 10) & 31) << 3;
}

/* draw the layer over a 320x224 RGB frame */
void host_md_composite(uint8_t *rgb)
{
    int r, c, y, x;
    for (r = 0; r < MD_ROWS; r++)
        for (c = 0; c < MD_COLS; c++) {
            int v = cur[1][r][c];
            if (!v)
                continue;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++) {
                    int sx = c * 8 + x, sy = r * 8 + y, shade = ((x + y) & 1) == 0;
                    if (v == MB_BLACK || (v == MB_SHADE && shade) || (v >= MB_BORDER_SHADED && shade))
                        memset(rgb + (sy * 320 + sx) * 3, 0, 3);
                    else if (v >= MB_BORDER) {
                        int t = (v - MB_BORDER) & 63;
                        put(rgb, sx, sy, lvl.backtile[((t >> 3) * 8 + y) * 64 + (t & 7) * 8 + x]);
                    }
                }
        }
    for (r = 0; r < MD_ROWS; r++)
        for (c = 0; c < MD_COLS; c++) {
            int ch = cur[0][r][c];
            const uint8_t *src = lvl.conchars + ((ch >> 4) << 10) + ((ch & 15) << 3);
            if (!ch)
                continue;
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    if (src[y * 128 + x] && r * 8 + y + 4 < 224)
                        put(rgb, c * 8 + x, r * 8 + y + 4, src[y * 128 + x]);
        }
}

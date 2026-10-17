/* The Mega Drive layer (see mdlink.h and src-md/md.s).
 *
 * The master SH-2 queues 16-bit words and hands them to the 68000 one at a
 * time: the word in COMM0, its number in COMM10's low byte; the 68000
 * answers with the number in COMM14's high byte. md_pump is called while
 * the master renders, so the queue drains without waiting.
 *
 * Text and plane B are kept as the cells the Mega Drive shows; each frame
 * the game puts what it wants and only the differences are sent. */
#include "hw.h"
#include "mdlink.h"

extern const uint8_t md_data[];     /* tools/mkmd.py, in cartridge ROM */

#define QSIZE 256
static uint16_t queue[QSIZE];
static int qhead, qtail;
static uint8_t seq;

static uint8_t have[2][MD_ROWS][MD_COLS];
static uint32_t touched[2][MD_ROWS][2], nonblank[2][MD_ROWS][2];   /* bit per column */
static int last_cell = -1;          /* plane << 12 | cell of the last tile sent */
static uint32_t back_key = 0xFFFFFFFF;
static int gun_set = -1, gun_word = -1;

#define FONT_TILE   16
#define BORDER_TILE 272
#define SHADE_TILE  336

static inline uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static inline int ack(void) { return (MARS_SYS_COMM14 >> 8) & 0xFF; }

void md_pump(void)
{
    while (qtail != qhead && ack() == seq) {
        MARS_SYS_COMM0 = queue[qtail];
        seq++;
        MARS_SYS_COMM10 = seq;
        qtail = (qtail + 1) & (QSIZE - 1);
    }
}

void md_wait(void)
{
    while (qtail != qhead)
        md_pump();
}

int md_busy(void)
{
    return qtail != qhead;
}

/* keep feeding the 68000 for about n of its answers */
void md_pump_for(int n)
{
    while (qtail != qhead && n > 0) {
        if (ack() == seq) {
            MARS_SYS_COMM0 = queue[qtail];
            seq++;
            MARS_SYS_COMM10 = seq;
            qtail = (qtail + 1) & (QSIZE - 1);
            n--;
        }
    }
}

static void put_word(uint16_t w)
{
    int next = (qhead + 1) & (QSIZE - 1);
    while (next == qtail)
        md_pump();                  /* full: wait for the 68000 */
    queue[qhead] = w;
    qhead = next;
}

/* the cartridge mapper: pages p .. p + 6 into the window's pages 1 .. 7 */
void md_bank(int page)
{
    put_word(0xC000 | (page & 0x3F));
}

/* until the 68000 has carried out everything sent so far: it takes the
   next word only when it is done with the one before, so once it has
   taken a final no-op, the rest is done */
void md_sync(void)
{
    put_word(0xF000);
    md_wait();
    while (ack() != seq)
        ;
}

void md_init(void)
{
    qhead = qtail = 0;
    seq = ack();
    last_cell = -1;
}

static uint16_t tile_word(int plane, int v)
{
    if (plane == 0)
        return v ? FONT_TILE + v : 0;
    if (v < MB_BORDER)
        return v;
    if (v < MB_BORDER_SHADED)
        return BORDER_TILE + v - MB_BORDER;     /* palette line 0, with the font */
    return SHADE_TILE + v - MB_BORDER_SHADED;
}

static void put_cell(int plane, int col, int row, int v)
{
    int cell = (plane << 12) | (row << 6) | col;
    uint32_t bit = 1u << (col & 31);
    touched[plane][row][col >> 5] |= bit;
    if (have[plane][row][col] == v)
        return;
    have[plane][row][col] = v;
    if (v)
        nonblank[plane][row][col >> 5] |= bit;
    else
        nonblank[plane][row][col >> 5] &= ~bit;
    if (cell != last_cell + 1)
        put_word(0x8000 | (plane << 12) | (cell & 0x7FF));
    put_word(tile_word(plane, v));
    last_cell = cell;
}

/* clear what was not put this frame */
static void end_plane(int plane)
{
    int r, h, c;
    for (r = 0; r < MD_ROWS; r++)
        for (h = 0; h < 2; h++) {
            uint32_t stale = nonblank[plane][r][h] & ~touched[plane][r][h];
            for (c = 0; stale; c++, stale >>= 1)
                if (stale & 1)
                    put_cell(plane, h * 32 + c, r, 0);
            touched[plane][r][h] = 0;
        }
}

void md_char(int col, int row, int c)
{
    if ((c & 0x7F) == ' ')
        c = 0;
    if ((unsigned)col < MD_COLS && (unsigned)row < MD_ROWS)
        put_cell(0, col, row, c & 0xFF);
}

void md_text(int col, int row, const char *s, int hi)
{
    for (; *s; s++, col++)
        md_char(col, row, (uint8_t)*s | (hi ? 0x80 : 0));
}

void md_end_text(void)
{
    end_plane(0);
}

int md_begin_back(uint32_t key)
{
    if (key == back_key)
        return 0;
    back_key = key;
    return 1;
}

void md_back(int col, int row, int v)
{
    if ((unsigned)col < MD_COLS && (unsigned)row < MD_ROWS)
        put_cell(1, col, row, v);
}

void md_end_back(void)
{
    end_plane(1);
}

/* ------------------------------------------------------------ the weapon */

static const uint8_t *set_entry(int set)
{
    if (set < 0 || set >= (int)rd32(md_data + 16))
        return 0;
    return md_data + rd32(md_data + 20) + set * 32;
}

void md_gun(int weapon, int size, int frame)
{
    int set = weapon * (int)rd32(md_data + 24) + size, w;
    if (frame < 0) {
        w = 0xA0FF;
    } else {
        if (set != gun_set) {
            put_word(0xB000 | set);
            gun_set = set;
            gun_word = -1;
        }
        w = 0xA000 | frame;
    }
    if (w != gun_word) {
        put_word(w);
        gun_word = w;
    }
}

int md_gun_cut(int weapon, int size, int frame, md_cut_t *cut)
{
    const uint8_t *e = set_entry(weapon * (int)rd32(md_data + 24) + size), *c;
    if (!e || frame < 0 || frame >= ((e[6] << 8) | e[7]))
        return 0;
    c = md_data + rd32(md_data + rd32(e + 16) + frame * 4);
    cut->row0 = c[0];
    cut->nrows = c[1];
    cut->x = c + 2;
    return cut->nrows > 0;
}

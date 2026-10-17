/* Pause menu and HUD text, drawn straight into the 32X framebuffer.
 * Text uses Quake's 8x8 console font at full 320-pixel width; rows are doubled
 * by the line table, so glyphs come out 8 pixels wide and 16 lines tall. */
#include "game.h"

int menu_active;
static int cursor;

enum { M_TEXTURES, M_LIGHTING, M_WATER, M_FAR, M_SIZE, M_MONSTERS, M_FPS, M_MOVE, M_RESUME, M_NUM };

static const char *const cap_names[NUM_MONSTER_CAPS] = {
    "OFF", "1", "2", "3", "4", "6", "8", "UNLIMITED"
};

static inline volatile uint8_t *fb8(void) { return (volatile uint8_t *)r_screen; }

static void draw_char(int x, int y, int c)
{
    const uint8_t *src = lvl.conchars + ((c >> 4) << 10) + ((c & 15) << 3);
    volatile uint8_t *dst = fb8() + y * (SCREENW * 2) + x;
    int r, i;
    if ((c & 127) == ' ')
        return;
    for (r = 0; r < 8; r++) {
        for (i = 0; i < 8; i++)
            if (src[i])
                dst[i] = src[i];
        src += 128;
        dst += SCREENW * 2;
    }
}

void draw_text(int x, int y, const char *s, int hi)
{
    while (*s) {
        draw_char(x, y, (uint8_t)*s | (hi ? 0x80 : 0));
        x += 8;
        s++;
    }
}

static void shade_box(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y < y1; y++) {
        volatile uint8_t *d = fb8() + y * (SCREENW * 2);
        for (x = x0 + (y & 1); x < x1; x += 2)
            d[x] = 0;
    }
}

static char *fmt_int(char *p, int v)
{
    char tmp[12];
    int n = 0;
    if (v < 0) { *p++ = '-'; v = -v; }
    do { tmp[n++] = '0' + (v - hw_div(v, 10) * 10); v = hw_div(v, 10); } while (v);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static char *cat(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

void hud_draw(int x, int y, int fps10, int split)
{
    char buf[32], *p = buf;
    p = fmt_int(p, hw_div(fps10, 10));
    *p++ = '.';
    p = fmt_int(p, fps10 - hw_div(fps10, 10) * 10);
    p = cat(p, " FPS");
    draw_text(x + 4, y + 2, buf, 0);
    (void)split;
}

/* Quake's backtile around a reduced view window; each framebuffer word
 * holds two tile pixels, so the tile keeps its full horizontal detail */
void border_draw(int x0, int y0, int w, int h)
{
    int r, c;
    for (r = 0; r < SCREENH; r++) {
        volatile uint16_t *d = r_screen + r * SCREENW;
        const uint8_t *row = lvl.backtile + ((r << 1) & 63) * 64;
        int inside = r >= y0 && r < y0 + h;
        for (c = 0; c < SCREENW; c++) {
            int tx;
            if (inside && c == x0) {
                c = x0 + w - 1;
                continue;
            }
            tx = (c << 1) & 63;
            d[c] = (uint16_t)((row[tx] << 8) | row[tx + 1]);
        }
    }
}

/* controls reminder shown at start-up */
void hint_draw(void)
{
    shade_box(0, SCREENH - SBAR_H - 25, 320, SCREENH - SBAR_H);
    draw_text(8, SCREENH - SBAR_H - 23, "D-PAD: MOVE/TURN  A+D-PAD: STRAFE/LOOK", 0);
    draw_text(8, SCREENH - SBAR_H - 12, "C:FIRE  B:JUMP  A+C:WEAPON  START:MENU", 0);
}

void menu_input(int pressed)
{
    int delta = 0;
    if (pressed & BTN_UP)   cursor = cursor ? cursor - 1 : M_NUM - 1;
    if (pressed & BTN_DOWN) cursor = cursor == M_NUM - 1 ? 0 : cursor + 1;
    if (pressed & BTN_LEFT)  delta = -1;
    if (pressed & BTN_RIGHT) delta = 1;
    if (pressed & (BTN_A | BTN_B | BTN_C)) delta = 2;
    if (!delta)
        return;
    switch (cursor) {
    case M_TEXTURES: cfg.textures ^= 1; break;
    case M_LIGHTING: cfg.lighting ^= 1; break;
    case M_WATER:    cfg.mesh ^= 1; break;
    case M_SIZE:
        /* the slider grows to the right: right = bigger screen */
        if (delta == 2) delta = 1;
        cfg.size -= delta;
        if (cfg.size < 0) cfg.size = 0;
        if (cfg.size >= NUM_SIZES) cfg.size = NUM_SIZES - 1;
        break;
    case M_FAR:
        if (delta == 2) delta = 1;
        cfg.far += delta;
        if (cfg.far < 0) cfg.far = 0;
        if (cfg.far >= NUM_FAR) cfg.far = NUM_FAR - 1;
        break;
    case M_MONSTERS:
        if (delta == 2) delta = cfg.monsters == NUM_MONSTER_CAPS - 1 ? -(NUM_MONSTER_CAPS - 1) : 1;
        cfg.monsters += delta;
        if (cfg.monsters < 0) cfg.monsters = 0;
        if (cfg.monsters >= NUM_MONSTER_CAPS) cfg.monsters = NUM_MONSTER_CAPS - 1;
        break;
    case M_FPS:  cfg.showfps ^= 1; break;
    case M_MOVE: cfg.fly ^= 1; break;
    case M_RESUME:
        if (delta == 2) menu_active = 0;
        break;
    }
}

void menu_draw(void)
{
    static const char *const names[M_NUM] = {
        "Textures", "Lighting", "Water", "Draw distance", "Screen size",
        "Monsters at once", "Show FPS", "Movement", "Resume"
    };
    char buf[48], *p;
    int i, y0 = 4;

    /* rows 0..101: clear of the status bar */
    shade_box(16, 0, 304, SCREENH - SBAR_H);
    draw_text(160 - 6 * 8 / 2, y0, "PAUSED", 1);
    for (i = 0; i < M_NUM; i++) {
        int y = y0 + 12 + i * 8;
        p = buf;
        switch (i) {
        case M_TEXTURES: p = cat(p, cfg.textures ? "ON" : "OFF (flat)"); break;
        case M_LIGHTING: p = cat(p, cfg.lighting ? "ON" : "OFF"); break;
        case M_WATER:    p = cat(p, cfg.mesh ? "SEE-THROUGH" : "SOLID"); break;
        case M_SIZE: {
            int k;
            *p++ = '[';
            for (k = 0; k < NUM_SIZES - 1; k++)
                *p++ = k < NUM_SIZES - 1 - cfg.size ? '=' : '-';
            *p++ = ']';
            *p++ = ' ';
            p = fmt_int(p, view_sizes[cfg.size].pct);
            *p++ = '%';
            break;
        }
        case M_FAR: {
            int k;
            *p++ = '[';
            for (k = 0; k < NUM_FAR - 1; k++)
                *p++ = k < cfg.far ? '=' : '-';
            *p++ = ']';
            *p++ = ' ';
            if (cfg.far == NUM_FAR - 1) p = cat(p, "max");
            else p = fmt_int(p, far_levels[cfg.far]);
            break;
        }
        case M_MONSTERS: p = cat(p, cap_names[cfg.monsters]); break;
        case M_FPS:  p = cat(p, cfg.showfps ? "ON" : "OFF"); break;
        case M_MOVE: p = cat(p, cfg.fly ? "FLY" : "WALK"); break;
        default: break;
        }
        *p = 0;
        if (i == cursor)
            draw_char(28, y, 13);   /* arrow glyph */
        draw_text(40, y, names[i], i == cursor);
        draw_text(184, y, buf, 0);
    }
    draw_text(40, y0 + 12 + M_NUM * 8 + 3, "Up/Down select  Left/Right set", 0);
}

/* end of the level: Quake's intermission figures */
void intermission_draw(int kills, int total, int secs)
{
    char buf[32], *p;
    int y = 30;
    shade_box(64, y - 6, 256, y + 50);
    draw_text(160 - 13 * 4, y, "LEVEL COMPLETE", 1);
    p = cat(buf, "Time   ");
    p = fmt_int(p, hw_div(secs, 60));
    *p++ = ':';
    secs -= hw_div(secs, 60) * 60;
    *p++ = '0' + hw_div(secs, 10);
    *p++ = '0' + secs - hw_div(secs, 10) * 10;
    *p = 0;
    draw_text(96, y + 16, buf, 0);
    p = cat(buf, "Kills  ");
    p = fmt_int(p, kills);
    *p++ = '/';
    fmt_int(p, total);
    draw_text(96, y + 26, buf, 0);
}

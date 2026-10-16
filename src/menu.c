/* Pause menu and HUD text, drawn straight into the 32X framebuffer.
 * Text uses Quake's 8x8 console font at full 320-pixel width; rows are doubled
 * by the line table, so glyphs come out 8 pixels wide and 16 lines tall. */
#include "game.h"

int menu_active;
static int cursor;

enum { M_TEXTURES, M_LIGHTING, M_FAR, M_FPS, M_MOVE, M_RESUME, M_NUM };

static inline volatile uint8_t *fb8(void) { return (volatile uint8_t *)r_fb; }

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

void hud_draw(int fps10, int split)
{
    char buf[32], *p = buf;
    p = fmt_int(p, hw_div(fps10, 10));
    *p++ = '.';
    p = fmt_int(p, fps10 - hw_div(fps10, 10) * 10);
    p = cat(p, " FPS");
    draw_text(4, 2, buf, 0);
    p = buf;
    p = cat(p, "split ");
    fmt_int(p, split);
    draw_text(4, 11, buf, 0);
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
    case M_FAR:
        if (delta == 2) delta = 1;
        cfg.far += delta;
        if (cfg.far < 0) cfg.far = 0;
        if (cfg.far >= NUM_FAR) cfg.far = NUM_FAR - 1;
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
        "Textures", "Lighting", "Draw distance", "Show FPS", "Movement", "Resume"
    };
    char buf[48], *p;
    int i, y0 = 20;

    shade_box(24, y0 - 6, 296, y0 + 8 * 11);
    draw_text(160 - 6 * 8 / 2, y0, "PAUSED", 1);
    for (i = 0; i < M_NUM; i++) {
        int y = y0 + 16 + i * 10;
        p = buf;
        switch (i) {
        case M_TEXTURES: p = cat(p, cfg.textures ? "ON" : "OFF (flat)"); break;
        case M_LIGHTING: p = cat(p, cfg.lighting ? "ON" : "OFF"); break;
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
        case M_FPS:  p = cat(p, cfg.showfps ? "ON" : "OFF"); break;
        case M_MOVE: p = cat(p, cfg.fly ? "FLY" : "WALK"); break;
        default: break;
        }
        *p = 0;
        if (i == cursor)
            draw_char(48, y, 13);   /* arrow glyph */
        draw_text(60, y, names[i], i == cursor);
        draw_text(172, y, buf, 0);
    }
    draw_text(40, y0 + 16 + M_NUM * 10 + 4, "Up/Down select  Left/Right set", 0);
}

/* Pause menu and HUD text, drawn straight into the 32X framebuffer.
 * Text uses Quake's 8x8 console font at full 320-pixel width; rows are doubled
 * by the line table, so glyphs come out 8 pixels wide and 16 lines tall. */
#include "game.h"
#include "bot.h"

int menu_active;
static int cursor;

enum { M_TEXTURES, M_LIGHTING, M_WATER, M_FAR, M_SIZE, M_MONSTERS, M_FPS, M_MOVE, M_CONTROLS, M_DEMO,
       M_RESUME, M_NUM };

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

/* names of pad buttons for the controls page and the hint */
const char *button_name(uint16_t b)
{
    switch (b) {
    case BTN_A: return "A";
    case BTN_B: return "B";
    case BTN_C: return "C";
    case BTN_X: return "X";
    case BTN_Y: return "Y";
    case BTN_Z: return "Z";
    case BTN_MODE: return "MODE";
    default: return "?";
    }
}

/* controls reminder shown at start-up, with the current buttons */
void hint_draw(void)
{
    char buf[48], *p;
    shade_box(0, SCREENH - SBAR_H - 25, 320, SCREENH - SBAR_H);
    p = cat(buf, "D-PAD: MOVE/TURN  ");
    p = cat(p, button_name(cfg.bind[BIND_STRAFE]));
    cat(p, "+D-PAD: STRAFE/LOOK");
    draw_text(8, SCREENH - SBAR_H - 23, buf, 0);
    p = cat(buf, button_name(cfg.bind[BIND_FIRE]));
    p = cat(p, ":FIRE  ");
    p = cat(p, button_name(cfg.bind[BIND_JUMP]));
    p = cat(p, ":JUMP  ");
    if (cfg.bind[BIND_WEAPON])
        p = cat(p, button_name(cfg.bind[BIND_WEAPON]));
    else {
        p = cat(p, button_name(cfg.bind[BIND_STRAFE]));
        p = cat(p, "+");
        p = cat(p, button_name(cfg.bind[BIND_FIRE]));
    }
    cat(p, ":WEAPON  START:MENU");
    draw_text(8, SCREENH - SBAR_H - 12, buf, 0);
}

/* ------------------------------------------------------------ the menu */

static int page;                    /* 0 the pause menu, 1 controls */
enum { C_FIRE, C_JUMP, C_STRAFE, C_WEAPON, C_LOOK, C_DEFAULTS, C_BACK, C_NUM };

static const uint16_t pad_buttons[7] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_MODE };

/* next button for an action; fire, jump and strafe swap with whichever
   action had it, next weapon only takes free buttons (or the combination) */
static void cycle_bind(int act, int dir)
{
    int i, cur = -1, k;
    for (i = 0; i < 7; i++)
        if (pad_buttons[i] == cfg.bind[act])
            cur = i;
    if (act == BIND_WEAPON) {
        /* positions: -1 = strafe + fire, then the buttons the others don't use */
        for (k = 0; k < 8; k++) {
            cur += dir;
            if (cur > 6) cur = -1;
            if (cur < -1) cur = 6;
            if (cur < 0)
                break;
            if (pad_buttons[cur] != cfg.bind[BIND_FIRE] && pad_buttons[cur] != cfg.bind[BIND_JUMP]
                && pad_buttons[cur] != cfg.bind[BIND_STRAFE])
                break;
        }
        cfg.bind[BIND_WEAPON] = cur < 0 ? 0 : pad_buttons[cur];
        return;
    }
    cur = (cur + dir + 7) % 7;
    for (i = 0; i < NUM_BINDS; i++)
        if (i != act && cfg.bind[i] == pad_buttons[cur]) {
            /* taken: the other action gets this one's old button */
            cfg.bind[i] = i == BIND_WEAPON ? 0 : cfg.bind[act];
        }
    cfg.bind[act] = pad_buttons[cur];
}

static void controls_input(int pressed, int delta)
{
    static const uint16_t defaults[NUM_BINDS] = { BTN_C, BTN_B, BTN_A, 0 };
    int i;
    (void)pressed;
    switch (cursor) {
    case C_FIRE: case C_JUMP: case C_STRAFE: case C_WEAPON:
        cycle_bind(cursor - C_FIRE + BIND_FIRE, delta < 0 ? -1 : 1);
        break;
    case C_LOOK: cfg.invert ^= 1; break;
    case C_DEFAULTS:
        if (delta == 2) {
            for (i = 0; i < NUM_BINDS; i++)
                cfg.bind[i] = defaults[i];
            cfg.invert = 0;
        }
        break;
    case C_BACK:
        if (delta == 2) {
            page = 0;
            cursor = M_CONTROLS;
        }
        break;
    }
}

void menu_input(int pressed)
{
    int delta = 0, n = page ? C_NUM : M_NUM;
    if (pressed & BTN_UP)   cursor = cursor ? cursor - 1 : n - 1;
    if (pressed & BTN_DOWN) cursor = cursor == n - 1 ? 0 : cursor + 1;
    if (pressed & BTN_LEFT)  delta = -1;
    if (pressed & BTN_RIGHT) delta = 1;
    if (pressed & (BTN_A | BTN_B | BTN_C)) delta = 2;
    if (!delta)
        return;
    if (page) {
        controls_input(pressed, delta);
        return;
    }
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
    case M_CONTROLS:
        if (delta == 2 || delta == 1) {
            page = 1;
            cursor = 0;
        }
        break;
    case M_DEMO:
        /* the demo bot plays from the start of the level; switching it
           off hands the controls back where it is */
        if (bot_active) bot_stop();
        else {
            bot_start();
            menu_active = 0;
        }
        break;
    case M_RESUME:
        if (delta == 2) menu_active = 0;
        break;
    }
}

static void item(int i, int y, const char *name, const char *value)
{
    if (i == cursor)
        draw_char(28, y, 13);       /* arrow glyph */
    draw_text(40, y, name, i == cursor);
    draw_text(184, y, value, 0);
}

static void controls_draw(void)
{
    static const char *const names[C_NUM] = {
        "Fire", "Jump", "Strafe / look", "Next weapon", "Look up/down", "Defaults", "Back"
    };
    char buf[32], *p;
    int i, y0 = 2;
    shade_box(16, 0, 304, SCREENH);
    draw_text(160 - 8 * 8 / 2, y0, "CONTROLS", 1);
    for (i = 0; i < C_NUM; i++) {
        p = buf;
        *p = 0;
        if (i <= C_STRAFE)
            cat(p, button_name(cfg.bind[i - C_FIRE + BIND_FIRE]));
        else if (i == C_WEAPON) {
            if (cfg.bind[BIND_WEAPON])
                cat(p, button_name(cfg.bind[BIND_WEAPON]));
            else {
                p = cat(p, button_name(cfg.bind[BIND_STRAFE]));
                p = cat(p, " + ");
                cat(p, button_name(cfg.bind[BIND_FIRE]));
            }
        } else if (i == C_LOOK)
            cat(p, cfg.invert ? "INVERTED" : "NORMAL");
        item(i, y0 + 12 + i * 8, names[i], buf);
    }
    draw_text(40, y0 + 12 + C_NUM * 8 + 4, "Left/Right picks a button.", 0);
    draw_text(40, y0 + 12 + C_NUM * 8 + 13, "X Y Z MODE: 6-button pads.", 0);
}

void menu_draw(void)
{
    static const char *const names[M_NUM] = {
        "Textures", "Lighting", "Water", "Draw distance", "Screen size",
        "Monsters at once", "Show FPS", "Movement", "Controls...", "Demo (bot plays)", "Resume"
    };
    char buf[48], *p;
    int i, y0 = 1;

    if (page) {
        controls_draw();
        return;
    }
    /* the whole screen: the status bar is redrawn when the menu closes */
    shade_box(16, 0, 304, SCREENH);
    draw_text(160 - 6 * 8 / 2, y0, "PAUSED", 1);
    for (i = 0; i < M_NUM; i++) {
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
        case M_DEMO: p = cat(p, bot_active ? "PLAYING" : "OFF"); break;
        default: break;
        }
        *p = 0;
        item(i, y0 + 10 + i * 8, names[i], buf);
    }
    draw_text(40, y0 + 10 + M_NUM * 8 + 2, "Up/Down select  Left/Right set", 0);
}

/* end of the level: Quake's intermission figures */
void intermission_draw(int kills, int total, int secs, int secrets, int tsecrets)
{
    char buf[32], *p;
    int y = 30;
    shade_box(64, y - 6, 256, y + 60);
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
    p = cat(buf, "Secrets ");
    p = fmt_int(p, secrets);
    *p++ = '/';
    fmt_int(p, tsecrets);
    draw_text(96, y + 36, buf, 0);
}

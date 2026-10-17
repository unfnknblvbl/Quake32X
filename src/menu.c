/* Pause menu and HUD text, drawn on the Mega Drive text layer (mdlink.c):
 * Quake's 8x8 console font on a 40 x 28 cell grid. The shading behind the
 * menu and the hint is on the Mega Drive's other plane (main.c). */
#include "g_local.h"
#include "bot.h"
#include "mdlink.h"

int menu_active;
static int cursor;

enum { M_TEXTURES, M_LIGHTING, M_WATER, M_FAR, M_SIZE, M_MONSTERS, M_FPS, M_MOVE, M_CONTROLS, M_DEMO,
       M_RESUME, M_NUM };

static const char *const cap_names[NUM_MONSTER_CAPS] = {
    "OFF", "1", "2", "3", "4", "6", "8", "UNLIMITED"
};

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

static int len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* text centred on the 40 columns */
static void centre(int row, const char *s, int hi)
{
    md_text((MD_COLS - len(s)) >> 1, row, s, hi);
}

void hud_draw(int col, int row, int fps10)
{
    char buf[32], *p = buf;
    p = fmt_int(p, hw_div(fps10, 10));
    *p++ = '.';
    p = fmt_int(p, fps10 - hw_div(fps10, 10) * 10);
    cat(p, " FPS");
    md_text(col, row, buf, 0);
}

/* the status bar line: figures, or a message centred in its place */
void sbar_draw(const char *buf, int hi, int message)
{
    if (message)
        centre(MD_ROWS - 2, buf, hi);
    else
        md_text(0, MD_ROWS - 2, buf, hi);
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

/* controls reminder shown at start-up, with the current buttons, on the
 * last two rows of the view (main.c shades them) */
void hint_draw(int row)
{
    char buf[48], *p;
    p = cat(buf, "D-PAD: MOVE/TURN  ");
    p = cat(p, button_name(cfg.bind[BIND_STRAFE]));
    cat(p, "+D-PAD: STRAFE/LOOK");
    md_text(1, row, buf, 0);
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
    md_text(1, row + 1, buf, 0);
}

/* ------------------------------------------------------------ the menu */

static int page;                    /* 0 the pause menu, 1 controls */

int menu_page(void) { return page; }
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

static void item(int i, int row, const char *name, const char *value)
{
    if (i == cursor)
        md_char(3, row, 13);        /* arrow glyph */
    md_text(5, row, name, i == cursor);
    md_text(23, row, value, 0);
}

static void controls_draw(void)
{
    static const char *const names[C_NUM] = {
        "Fire", "Jump", "Strafe / look", "Next weapon", "Look up/down", "Defaults", "Back"
    };
    char buf[32], *p;
    int i;
    centre(0, "CONTROLS", 1);
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
        item(i, 3 + i * 2, names[i], buf);
    }
    md_text(5, 3 + C_NUM * 2 + 1, "Left/Right picks a button.", 0);
    md_text(5, 3 + C_NUM * 2 + 3, "X Y Z MODE: 6-button pads.", 0);
}

/* the menu covers columns 2-37 of the whole screen (main.c shades them) */
void menu_draw(void)
{
    static const char *const names[M_NUM] = {
        "Textures", "Lighting", "Water", "Draw distance", "Screen size",
        "Monsters at once", "Show FPS", "Movement", "Controls...", "Demo (bot plays)", "Resume"
    };
    char buf[48], *p;
    int i;

    if (page) {
        controls_draw();
        return;
    }
    centre(0, "PAUSED", 1);
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
        item(i, 2 + i * 2, names[i], buf);
    }
    md_text(5, 2 + M_NUM * 2 + 1, "Up/Down select  Left/Right set", 0);
}

/* end of the level: Quake's intermission figures, in a box at columns
 * 8-31, rows 6-17 (main.c shades it) */
void intermission_draw(int kills, int total, int secs, int secrets, int tsecrets)
{
    char buf[32], *p;
    centre(7, "LEVEL COMPLETE", 1);
    p = cat(buf, "Time    ");
    p = fmt_int(p, hw_div(secs, 60));
    *p++ = ':';
    secs -= hw_div(secs, 60) * 60;
    *p++ = '0' + hw_div(secs, 10);
    *p++ = '0' + secs - hw_div(secs, 10) * 10;
    *p = 0;
    md_text(12, 10, buf, 0);
    p = cat(buf, "Kills   ");
    p = fmt_int(p, kills);
    *p++ = '/';
    fmt_int(p, total);
    md_text(12, 12, buf, 0);
    p = cat(buf, "Secrets ");
    p = fmt_int(p, secrets);
    *p++ = '/';
    fmt_int(p, tsecrets);
    md_text(12, 14, buf, 0);
}

/* the Mega Drive layer for this frame: the border, the status bar and the
 * shading behind overlays on plane B (rebuilt only when they change), and
 * all the text */
void overlays_draw(int win_x, int win_y, int fps10, int hint)
{
    int vx0 = win_x >> 2, vx1 = (win_x + view.vw) >> 2;     /* in 8x8 cells */
    int vy0 = win_y >> 2, vy1 = (win_y + view.vh) >> 2;
    int sbar = MD_ROWS - 2;
    uint32_t key = cfg.size | menu_active << 4 | menu_page() << 5 | (hint != 0) << 6
                   | g_level_done << 7;
    if (md_begin_back(key)) {
        int r, c;
        for (r = 0; r < MD_ROWS; r++)
            for (c = 0; c < MD_COLS; c++) {
                int v = MB_NONE, shade = 0;
                if (r >= sbar)
                    v = MB_BLACK;
                else if (c < vx0 || c >= vx1 || r < vy0 || r >= vy1)
                    v = MB_BORDER + ((r & 7) << 3) + (c & 7);
                if (menu_active)
                    shade = c >= 2 && c < 38;
                else if (g_level_done)
                    shade = c >= 8 && c < 32 && r >= 6 && r < 17;
                else if (hint)
                    shade = r >= sbar - 4 && r < sbar;
                if (shade && v == MB_NONE)
                    v = MB_SHADE;
                else if (shade && v >= MB_BORDER)
                    v += MB_BORDER_SHADED - MB_BORDER;
                md_back(c, r, v);
            }
        md_end_back();
    }
    if (!menu_active) {
        char buf[48];
        int hi = g_hud_text(buf);
        sbar_draw(buf, hi, ps.dead || (ps.msg && level_time < ps.msg_until));
    }
    if (g_level_done)
        intermission_draw(ps.kills, ps.total_monsters, hw_div(g_level_ticks, 60), ps.secrets, ps.total_secrets);
    if (menu_active)
        menu_draw();
    if (cfg.showfps)
        hud_draw(menu_active ? 1 : vx0 + 1, menu_active ? 0 : vy0, fps10);
    if (hint && !menu_active)
        hint_draw(sbar - 3);
    md_end_text();
}

/* Headless libretro frontend used to test the 32X ROM.
 *
 *   harness <core.so> <rom> <script> <outdir>
 *
 * Script lines (frames are 0-based, NTSC 60 Hz):
 *   hold <from> <to> <buttons>   buttons: any of U D L R A B C S (S = start)
 *   shot <frame> <name>          write <outdir>/<name>.ppm
 *   comm <frame>                 print the 32X COMM registers
 *   peek <frame> <addr> <n>      print n longs of SH-2 SDRAM at addr
 *   end  <frame>
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include "libretro.h"

static void *core;
#define SYM(rt, n, args) rt (*n) args = (rt (*) args)dlsym(core, #n)

static unsigned pixfmt = RETRO_PIXEL_FORMAT_0RGB1555;
static const void *last_frame;
static unsigned last_w, last_h;
static size_t last_pitch;
static uint16_t frame_copy[640 * 480];
static uint32_t frame_copy32[640 * 480];
static uint16_t cur_buttons;

typedef struct {
    void *fn[8];
    uint32_t dreg[8], areg[8], asp, pc;
    uint8_t interrupts[8];
    uint16_t sr, execinfo;
    int io_cycle_counter;
    unsigned Opcode;
    int cycles_needed;
    uint16_t *PC;
    uintptr_t BasePC;
} fame_ctx;

struct cmd { char op[8]; int a, b; char s[64]; unsigned addr; };
static struct cmd cmds[4096];
static int ncmds;

static void core_log(enum retro_log_level level, const char *fmt, ...)
{
    va_list ap;
    if (level < RETRO_LOG_INFO && !getenv("VERBOSE")) return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static bool env_cb(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        pixfmt = *(const enum retro_pixel_format *)data;
        return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = "/tmp";
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = core_log;
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = data;
        const char *drc = getenv("PICO_DRC");
        if (!strcmp(v->key, "picodrive_region")) { v->value = "US"; return true; }
        if (drc && !strcmp(v->key, "picodrive_drc")) { v->value = drc; return true; }
        v->value = NULL;
        return false;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    default:
        return false;
    }
}

static void video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (!data) return;
    last_frame = data; last_w = w; last_h = h; last_pitch = pitch;
    if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
        for (unsigned y = 0; y < h; y++)
            memcpy(frame_copy32 + y * w, (const uint8_t *)data + y * pitch, w * 4);
    } else {
        for (unsigned y = 0; y < h; y++)
            memcpy(frame_copy + y * w, (const uint8_t *)data + y * pitch, w * 2);
    }
}
static FILE *audio_out;             /* AUDIO=file.raw: s16le stereo at the core's rate */
static void audio_cb(int16_t l, int16_t r)
{
    int16_t s[2] = { l, r };
    if (audio_out) fwrite(s, 2, 2, audio_out);
}
static size_t audio_batch_cb(const int16_t *d, size_t n)
{
    if (audio_out) fwrite(d, 4, n, audio_out);
    return n;
}
static void input_poll_cb(void) {}

static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    (void)index;
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return cur_buttons;
    return (cur_buttons >> id) & 1;
}

static uint16_t parse_buttons(const char *s)
{
    uint16_t b = 0;
    for (; *s; s++) switch (*s) {
        case 'U': b |= 1 << RETRO_DEVICE_ID_JOYPAD_UP; break;
        case 'D': b |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN; break;
        case 'L': b |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT; break;
        case 'R': b |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
        case 'A': b |= 1 << RETRO_DEVICE_ID_JOYPAD_Y; break;   /* MD A */
        case 'B': b |= 1 << RETRO_DEVICE_ID_JOYPAD_B; break;   /* MD B */
        case 'C': b |= 1 << RETRO_DEVICE_ID_JOYPAD_A; break;   /* MD C */
        case 'S': b |= 1 << RETRO_DEVICE_ID_JOYPAD_START; break;
        case 'X': b |= 1 << RETRO_DEVICE_ID_JOYPAD_X; break;   /* MD Y */
        case 'Y': b |= 1 << RETRO_DEVICE_ID_JOYPAD_L; break;   /* MD X */
        case 'Z': b |= 1 << RETRO_DEVICE_ID_JOYPAD_R; break;
        case 'M': b |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT; break;
    }
    return b;
}

static void save_ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%u %u\n255\n", last_w, last_h);
    for (unsigned i = 0; i < last_w * last_h; i++) {
        unsigned r, g, b;
        if (pixfmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            uint32_t p = frame_copy32[i];
            r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
        } else if (pixfmt == RETRO_PIXEL_FORMAT_RGB565) {
            uint16_t p = frame_copy[i];
            r = (p >> 11) << 3; g = ((p >> 5) & 63) << 2; b = (p & 31) << 3;
        } else {
            uint16_t p = frame_copy[i];
            r = ((p >> 10) & 31) << 3; g = ((p >> 5) & 31) << 3; b = (p & 31) << 3;
        }
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

static uint8_t *sdram(void)
{
    uint8_t **pm = dlsym(core, "Pico32xMem");
    return pm && *pm ? *pm : NULL;
}

static uint32_t sdram_long(unsigned addr)
{
    uint8_t *m = sdram();
    uint16_t hi, lo;
    if (!m) return 0;
    addr &= 0x3FFFC;
    memcpy(&hi, m + addr, 2);
    memcpy(&lo, m + addr + 2, 2);
    return ((uint32_t)hi << 16) | lo;
}

int main(int argc, char **argv)
{
    struct retro_game_info gi;
    FILE *f;
    char line[256];
    long sz;
    void *rom;
    int endframe = 600;

    if (argc < 5) {
        fprintf(stderr, "usage: harness core.so rom script outdir\n");
        return 1;
    }
    core = dlopen(argv[1], RTLD_NOW);
    if (!core) { fprintf(stderr, "%s\n", dlerror()); return 1; }

    f = fopen(argv[3], "r");
    if (!f) { perror(argv[3]); return 1; }
    while (fgets(line, sizeof(line), f) && ncmds < 4096) {
        struct cmd *c = &cmds[ncmds];
        memset(c, 0, sizeof(*c));
        if (line[0] == '#' || sscanf(line, "%7s", c->op) != 1) continue;
        if (!strcmp(c->op, "hold")) sscanf(line, "%*s %d %d %63s", &c->a, &c->b, c->s);
        else if (!strcmp(c->op, "shot")) sscanf(line, "%*s %d %63s", &c->a, c->s);
        else if (!strcmp(c->op, "peek")) sscanf(line, "%*s %d %x %d", &c->a, &c->addr, &c->b);
        else sscanf(line, "%*s %d", &c->a);
        if (!strcmp(c->op, "end")) endframe = c->a;
        ncmds++;
    }
    fclose(f);

    f = fopen(argv[2], "rb");
    if (!f) { perror(argv[2]); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 1;
    fclose(f);

    {
        SYM(void, retro_set_environment, (retro_environment_t));
        SYM(void, retro_set_video_refresh, (retro_video_refresh_t));
        SYM(void, retro_set_audio_sample, (retro_audio_sample_t));
        SYM(void, retro_set_audio_sample_batch, (retro_audio_sample_batch_t));
        SYM(void, retro_set_input_poll, (retro_input_poll_t));
        SYM(void, retro_set_input_state, (retro_input_state_t));
        SYM(void, retro_init, (void));
        SYM(bool, retro_load_game, (const struct retro_game_info *));
        SYM(void, retro_run, (void));
        SYM(void, retro_set_controller_port_device, (unsigned, unsigned));

        retro_set_environment(env_cb);
        retro_init();
        retro_set_video_refresh(video_cb);
        if (getenv("AUDIO")) audio_out = fopen(getenv("AUDIO"), "wb");
        retro_set_audio_sample(audio_cb);
        retro_set_audio_sample_batch(audio_batch_cb);
        retro_set_input_poll(input_poll_cb);
        retro_set_input_state(input_state_cb);

        memset(&gi, 0, sizeof(gi));
        gi.path = argv[2];
        gi.data = rom;
        gi.size = sz;
        if (!retro_load_game(&gi)) { fprintf(stderr, "load failed\n"); return 1; }
        retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

        for (int fr = 0; fr <= endframe; fr++) {
            cur_buttons = 0;
            for (int i = 0; i < ncmds; i++)
                if (!strcmp(cmds[i].op, "hold") && fr >= cmds[i].a && fr <= cmds[i].b)
                    cur_buttons |= parse_buttons(cmds[i].s);
            retro_run();
            for (int i = 0; i < ncmds; i++) {
                struct cmd *c = &cmds[i];
                if (c->a != fr) continue;
                if (!strcmp(c->op, "shot")) save_ppm(argv[4], c->s);
                else if (!strcmp(c->op, "comm")) {
                    uint16_t *regs = dlsym(core, "Pico32x");
                    printf("frame %d COMM:", fr);
                    for (int k = 0; k < 8; k++) printf(" %04x", regs[0x10 + k]);
                    printf("  adapter %04x", regs[0]);
                    {
                        fame_ctx *cx = dlsym(core, "PicoCpuFM68k");
                        printf("  68k pc %06lx sr %04x a7 %08x a5 %08x", (unsigned long)((uintptr_t)cx->PC - cx->BasePC), cx->sr, cx->areg[7], cx->areg[5]);
                    }
                    {
                        SYM(void *, retro_get_memory_data, (unsigned));
                        uint8_t *ram = retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
                        if (ram) { printf("  mdram[0000..]:"); for (int k = 0x0000; k < 0x20; k += 2) printf(" %02x%02x", ram[k+1], ram[k]); }
                    }
                    printf("\n");
                } else if (!strcmp(c->op, "peek")) {
                    printf("frame %d %08x:", fr, c->addr);
                    for (int k = 0; k < c->b; k++) printf(" %08x", sdram_long(c->addr + k * 4));
                    printf("\n");
                }
            }
        }
    }
    fflush(stdout);
    _exit(0);
}

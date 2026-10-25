/* Levels in the cartridge: the directory tools/mkrom.py fills in, and
 * mapping a level's pages into the window the SH-2s read it through.
 *
 * The cartridge is bigger than the 32X's 4 MB window, so it has Sega's
 * standard mapper: the window is eight 512 KB pages, the first (this
 * program) fixed, the other seven chosen by registers only the 68000 can
 * write (src-md/md.s). A level's blob starts on a page boundary; mapped
 * from the second page on, it sits at 0x02080000. Its offsets are all
 * from its own start, so where it lies in the cartridge does not matter. */
#include "g_local.h"
#include "mdlink.h"
#include "levels.h"

typedef struct {
    uint8_t  page, npages;
    uint16_t flags;
    char     name[12];
} ldirent_t;

typedef struct {
    char     magic[8];
    uint32_t count;
    ldirent_t lv[8];
    uint8_t  bank_page, bank_npages;    /* the shared sound bank (0 pages: none) */
    uint16_t flags;                     /* LD_ */
} leveldir_t;

/* a cartridge of 4 MB or less needs no mapper: the SH-2s see all of it at
   0x02000000, and the directory's pages are 64 KB steps of it */
#define LD_LINEAR 1
#define LINEAR_STEP 0x10000

#ifndef HOST
extern const leveldir_t level_dir;

#ifdef ONCHIP
#define CCR_PURGE 0x19
#else
#define CCR_PURGE 0x11
#endif

GCODE int lvl_count(void)
{
    return (int)level_dir.count;
}

GCODE const char *lvl_file(int n)
{
    return level_dir.lv[n].name;
}

/* the level whose blob has the attract demo, -1 none */
GCODE int lvl_demo(void)
{
    int i;
    for (i = 0; i < (int)level_dir.count; i++)
        if (level_dir.lv[i].flags & 1)
            return i;
    return -1;
}

/* the level's pages into the window: the 68000 writes the mapper; when it
 * has, the master's cache forgets what it held of the old pages (the
 * slave's does at the start of its next frame) */
GCODE const void *lvl_map(int n)
{
    if (level_dir.flags & LD_LINEAR)
        return (const void *)(0x02000000 + level_dir.lv[n].page * LINEAR_STEP);
    if (level_dir.bank_npages)
        md_bank_set(level_dir.bank_page, level_dir.bank_npages);
    md_bank(level_dir.lv[n].page);
    md_sync();
    SH2_CCR = CCR_PURGE;
    return LEVEL_WINDOW;
}
/* the shared sound bank, mapped in after the level's pages (NULL: none) */
GCODE const uint8_t *lvl_bank(void)
{
    if (!level_dir.bank_npages)
        return NULL;
    if (level_dir.flags & LD_LINEAR)
        return (const uint8_t *)(0x02000000 + level_dir.bank_page * LINEAR_STEP);
    return (const uint8_t *)LEVEL_WINDOW + (7 - level_dir.bank_npages) * 0x80000;
}
#endif

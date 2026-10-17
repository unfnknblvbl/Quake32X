/* 32X hardware definitions (SH-2 side) */
#ifndef HW_H
#define HW_H

#ifdef HOST
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#else
typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef int                int32_t;
typedef unsigned int       uint32_t;
typedef long long          int64_t;
typedef unsigned long long uint64_t;
#define NULL ((void*)0)
void *memcpy(void *dst, const void *src, unsigned n);
void *memset(void *dst, int c, unsigned n);
#endif
typedef int32_t            fixed_t;     /* 16.16 */

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))

#ifndef HOST
/* 32X system registers */
#define MARS_SYS_INTMSK     REG16(0x20004000)
#define MARS_SYS_COMM0      REG16(0x20004020)
#define MARS_SYS_COMM2      REG16(0x20004022)
#define MARS_SYS_COMM4      REG16(0x20004024)
#define MARS_SYS_COMM6      REG16(0x20004026)
#define MARS_SYS_COMM8      REG16(0x20004028)
#define MARS_SYS_COMM10     REG16(0x2000402A)
#define MARS_SYS_COMM12     REG16(0x2000402C)
#define MARS_SYS_COMM14     REG16(0x2000402E)

/* 32X VDP */
#define MARS_VDP_DISPMODE   REG16(0x20004100)
#define MARS_VDP_SCRSHFT    REG16(0x20004102)
#define MARS_VDP_FILLEN     REG16(0x20004104)
#define MARS_VDP_FILADR     REG16(0x20004106)
#define MARS_VDP_FILDAT     REG16(0x20004108)
#define MARS_VDP_FBCTL      REG16(0x2000410A)
#define MARS_CRAM           ((volatile uint16_t *)0x20004200)
#define MARS_FRAMEBUFFER    ((volatile uint16_t *)0x24000000)

#define MARS_VDP_MODE_PACKED 0x0001
#define MARS_VDP_VBLK       0x8000
#define MARS_VDP_HBLK       0x4000
#define MARS_VDP_PEN        0x2000
#define MARS_VDP_FEN        0x0002
#define MARS_VDP_FS         0x0001

/* SH-2 on-chip division unit (one per CPU) */
#define SH2_DIVU_DVSR       REG32(0xFFFFFF00)
#define SH2_DIVU_DVDNT      REG32(0xFFFFFF04)
#define SH2_DIVU_DVDNTH     REG32(0xFFFFFF10)
#define SH2_DIVU_DVDNTL     REG32(0xFFFFFF14)

#define SH2_CCR             (*(volatile uint8_t *)0xFFFFFE92)

/* cache-through alias of an SDRAM address */
#define UNCACHED(p) ((void *)((uint32_t)(p) | 0x20000000))

extern volatile uint32_t vblank_count;  /* bumped by the master's VBlank IRQ */
#endif

/* joypad bits as delivered by the 68000 in COMM12 */
#define BTN_UP      0x0001
#define BTN_DOWN    0x0002
#define BTN_LEFT    0x0004
#define BTN_RIGHT   0x0008
#define BTN_B       0x0010
#define BTN_C       0x0020
#define BTN_A       0x0040
#define BTN_START   0x0080
#define BTN_Z       0x0100
#define BTN_Y       0x0200
#define BTN_X       0x0400
#define BTN_MODE    0x0800
#define BTN_NOPAD   0xF000

/* rendered resolution; every pixel is shown 2x2 on the 320x224 screen */
#define SCREENW 160
#define SCREENH 112

#define ROMDATA __attribute__((section(".romdata")))

#ifdef HOST
static inline int32_t hw_div(int32_t a, int32_t b) { return b ? a / b : 0; }
static inline int32_t sat32(int64_t q) { return q > 0x7FFFFFFF ? 0x7FFFFFFF : q < -0x7FFFFFFF - 1 ? (int32_t)0x80000000 : (int32_t)q; }
static inline fixed_t hw_fixdiv(fixed_t a, fixed_t b) { return b ? sat32(((int64_t)a << 16) / b) : 0; }
static inline int32_t hw_div64(int64_t a, int32_t b) { return b ? sat32(a / b) : 0; }
#define hw_div64_start(a, b) (host_div_result = hw_div64((a), (b)))
#define hw_div64_result() host_div_result
extern int32_t host_div_result;
extern uint16_t host_comm0;
#define MARS_SYS_COMM0 host_comm0
#else
static inline uint32_t get_vblank_count(void)
{
    return *(volatile uint32_t *)UNCACHED(&vblank_count);
}

/* 32/32 signed divide through the hardware divider */
static inline int32_t hw_div(int32_t a, int32_t b)
{
    SH2_DIVU_DVSR = b;
    SH2_DIVU_DVDNT = a;
    return SH2_DIVU_DVDNT;
}

/* (a << 16) / b using the 64/32 divider */
static inline fixed_t hw_fixdiv(fixed_t a, fixed_t b)
{
    SH2_DIVU_DVSR = b;
    SH2_DIVU_DVDNTH = a >> 16;
    SH2_DIVU_DVDNTL = (uint32_t)a << 16;
    return SH2_DIVU_DVDNTL;
}

/* 64/32 signed divide */
static inline int32_t hw_div64(int64_t a, int32_t b)
{
    SH2_DIVU_DVSR = b;
    SH2_DIVU_DVDNTH = (int32_t)(a >> 32);
    SH2_DIVU_DVDNTL = (uint32_t)a;
    return SH2_DIVU_DVDNTL;
}
/* start a 64/32 divide and collect it later (runs in parallel, ~39 cycles) */
#define hw_div64_start(a, b) do { int64_t _a = (a); SH2_DIVU_DVSR = (b); \
    SH2_DIVU_DVDNTH = (int32_t)(_a >> 32); SH2_DIVU_DVDNTL = (uint32_t)_a; } while (0)
#define hw_div64_result() ((int32_t)SH2_DIVU_DVDNTL)
#endif

static inline fixed_t fixmul(fixed_t a, fixed_t b)
{
    return (fixed_t)(((int64_t)a * b) >> 16);
}

#endif

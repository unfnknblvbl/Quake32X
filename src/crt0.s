!-----------------------------------------------------------------------
! Quake 32X - ROM header and SH-2 startup
! Header layout, Mars header and the standard 32X 68000 boot code follow
! Chilly Willy's public 32X devkit startup (as used by Doom 32X
! Resurrection).
!-----------------------------------------------------------------------

        .section .romhdr,"ax"

! 68000 exception vectors at 0x000 - everything points at the boot code
        .long   0x01000000,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0
        .long   0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0,0x000003F0

! Mega Drive ROM header at 0x100
        .ascii  "SEGA 32X        "
        .ascii  "(C)HOME 2026.OCT"
        .ascii  "QUAKE 32X TECH D"
        .ascii  "EMO - E1M1      "
        .ascii  "                "
        .ascii  "QUAKE 32X TECH D"
        .ascii  "EMO - E1M1      "
        .ascii  "                "
        .ascii  "GM Q32X0001-00"
        .word   0x0000                  /* checksum - patched by tools/fixrom.py */
        .ascii  "J6              "
        .long   0x00000000,0x003FFFFF   /* ROM start, end */
        .long   0x00FF0000,0x00FFFFFF   /* RAM start, end */
        .ascii  "            "          /* 0x1B0 no SRAM */
        .ascii  "            "          /* 0x1BC modem */
        .ascii  "                                        " /* 0x1C8 memo */
        .ascii  "JUE             "      /* 0x1F0 region */

! Mars 68000 exception jump table at 0x200
        .macro  jump address
        .word   0x4EF9,\address>>16,\address&0xFFFF
        .endm
        .macro  call address
        .word   0x4EB9,\address>>16,\address&0xFFFF
        .endm

        jump    0x880800    /* reset = hot start */
        call    0x880840    /* EX_BusError */
        call    0x880840    /* EX_AddrError */
        call    0x880840    /* EX_IllInstr */
        call    0x880840    /* EX_DivByZero */
        call    0x880840    /* EX_CHK */
        call    0x880840    /* EX_TrapV */
        call    0x880840    /* EX_Priviledge */
        call    0x880840    /* EX_Trace */
        call    0x880840    /* EX_LineA */
        call    0x880840    /* EX_LineF */
        .space  72          /* reserved */
        call    0x880840    /* EX_Spurious */
        call    0x880840    /* EX_Level1 */
        jump    0x880900    /* EX_Level2 EXT */
        call    0x880840    /* EX_Level3 */
        jump    0x880880    /* EX_Level4 HBlank */
        call    0x880840    /* EX_Level5 */
        jump    0x8808C0    /* EX_Level6 VBlank */
        call    0x880840    /* EX_Level7 */
        call    0x880840    /* EX_Trap0 */
        call    0x880840    /* EX_Trap1 */
        call    0x880840    /* EX_Trap2 */
        call    0x880840    /* EX_Trap3 */
        call    0x880840    /* EX_Trap4 */
        call    0x880840    /* EX_Trap5 */
        call    0x880840    /* EX_Trap6 */
        call    0x880840    /* EX_Trap7 */
        call    0x880840    /* EX_Trap8 */
        call    0x880840    /* EX_Trap9 */
        call    0x880840    /* EX_TrapA */
        call    0x880840    /* EX_TrapB */
        call    0x880840    /* EX_TrapC */
        call    0x880840    /* EX_TrapD */
        call    0x880840    /* EX_TrapE */
        call    0x880840    /* EX_TrapF */
        .space  166         /* reserved */

! Mars header at 0x3C0
        .ascii  "QUAKE32X        "      /* module name */
        .long   0x00000000              /* version */
        .long   __sdram_load-0x02000000 /* source (offset in ROM) */
        .long   0x00000000              /* destination (offset in SDRAM) */
        .long   __sdram_size            /* size */
        .long   pri_start               /* master SH-2 entry */
        .long   sec_start               /* slave SH-2 entry */
        .long   pri_vbr                 /* master VBR */
        .long   sec_vbr                 /* slave VBR */

! Standard 32X startup code for the 68000 at 0x3F0 (Sega ICD_MARS)
        .word   0x287C,0xFFFF,0xFFC0,0x23FC,0x0000,0x0000,0x00A1,0x5128
        .word   0x46FC,0x2700,0x4BF9,0x00A1,0x0000,0x7001,0x0CAD,0x4D41
        .word   0x5253,0x30EC,0x6600,0x03E6,0x082D,0x0007,0x5101,0x67F8
        .word   0x4AAD,0x0008,0x6710,0x4A6D,0x000C,0x670A,0x082D,0x0000
        .word   0x5101,0x6600,0x03B8,0x102D,0x0001,0x0200,0x000F,0x6706
        .word   0x2B78,0x055A,0x4000,0x7200,0x2C41,0x4E66,0x41F9,0x0000
        .word   0x04D4,0x6100,0x0152,0x6100,0x0176,0x47F9,0x0000,0x04E8
        .word   0x43F9,0x00A0,0x0000,0x45F9,0x00C0,0x0011,0x3E3C,0x0100
        .word   0x7000,0x3B47,0x1100,0x3B47,0x1200,0x012D,0x1100,0x66FA
        .word   0x7425,0x12DB,0x51CA,0xFFFC,0x3B40,0x1200,0x3B40,0x1100
        .word   0x3B47,0x1200,0x149B,0x149B,0x149B,0x149B,0x41F9,0x0000
        .word   0x04C0,0x43F9,0x00FF,0x0000,0x22D8,0x22D8,0x22D8,0x22D8
        .word   0x22D8,0x22D8,0x22D8,0x22D8,0x41F9,0x00FF,0x0000,0x4ED0
        .word   0x1B7C,0x0001,0x5101,0x41F9,0x0000,0x06BC,0xD1FC,0x0088
        .word   0x0000,0x4ED0,0x0404,0x303C,0x076C,0x0000,0x0000,0xFF00
        .word   0x8137,0x0002,0x0100,0x0000,0xAF01,0xD91F,0x1127,0x0021
        .word   0x2600,0xF977,0xEDB0,0xDDE1,0xFDE1,0xED47,0xED4F,0xD1E1
        .word   0xF108,0xD9C1,0xD1E1,0xF1F9,0xF3ED,0x5636,0xE9E9,0x9FBF
        .word   0xDFFF,0x4D41,0x5253,0x2049,0x6E69,0x7469,0x616C,0x2026
        .word   0x2053,0x6563,0x7572,0x6974,0x7920,0x5072,0x6F67,0x7261
        .word   0x6D20,0x2020,0x2020,0x2020,0x2020,0x2043,0x6172,0x7472
        .word   0x6964,0x6765,0x2056,0x6572,0x7369,0x6F6E,0x2020,0x2020
        .word   0x436F,0x7079,0x7269,0x6768,0x7420,0x5345,0x4741,0x2045
        .word   0x4E54,0x4552,0x5052,0x4953,0x4553,0x2C4C,0x5444,0x2E20
        .word   0x3139,0x3934,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020
        .word   0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020,0x2020
        .word   0x2020,0x2020,0x2020,0x524F,0x4D20,0x5665,0x7273,0x696F
        .word   0x6E20,0x312E,0x3000,0x48E7,0xC040,0x43F9,0x00C0,0x0004
        .word   0x3011,0x303C,0x8000,0x323C,0x0100,0x3E3C,0x0012,0x1018
        .word   0x3280,0xD041,0x51CF,0xFFF8,0x4CDF,0x0203,0x4E75,0x48E7
        .word   0x81C0,0x41F9,0x0000,0x063E,0x43F9,0x00C0,0x0004,0x3298
        .word   0x3298,0x3298,0x3298,0x3298,0x3298,0x3298,0x2298,0x3341
        .word   0xFFFC,0x3011,0x0800,0x0001,0x66F8,0x3298,0x3298,0x7000
        .word   0x22BC,0xC000,0x0000,0x7E0F,0x3340,0xFFFC,0x3340,0xFFFC
        .word   0x3340,0xFFFC,0x3340,0xFFFC,0x51CF,0xFFEE,0x22BC,0x4000
        .word   0x0010,0x7E09,0x3340,0xFFFC,0x3340,0xFFFC,0x3340,0xFFFC
        .word   0x3340,0xFFFC,0x51CF,0xFFEE,0x4CDF,0x0381,0x4E75,0x8114
        .word   0x8F01,0x93FF,0x94FF,0x9500,0x9600,0x9780,0x4000,0x0080
        .word   0x8104,0x8F02,0x48E7,0xC140,0x43F9,0x00A1,0x5180,0x08A9
        .word   0x0007,0xFF80,0x66F8,0x3E3C,0x00FF,0x7000,0x7200,0x337C
        .word   0x00FF,0x0004,0x3341,0x0006,0x3340,0x0008,0x4E71,0x0829
        .word   0x0001,0x000B,0x66F8,0x0641,0x0100,0x51CF,0xFFE8,0x4CDF
        .word   0x0283,0x4E75,0x48E7,0x8180,0x41F9,0x00A1,0x5200,0x08A8
        .word   0x0007,0xFF00,0x66F8,0x3E3C,0x001F,0x20C0,0x20C0,0x20C0
        .word   0x20C0,0x51CF,0xFFF6,0x4CDF,0x0181,0x4E75,0x41F9,0x00FF
        .word   0x0000,0x3E3C,0x07FF,0x7000,0x20C0,0x20C0,0x20C0,0x20C0
        .word   0x20C0,0x20C0,0x20C0,0x20C0,0x51CF,0xFFEE,0x3B7C,0x0000
        .word   0x1200,0x7E0A,0x51CF,0xFFFE,0x43F9,0x00A1,0x5100,0x7000
        .word   0x2340,0x0020,0x2340,0x0024,0x1B7C,0x0003,0x5101,0x2E79
        .word   0x0088,0x0000,0x0891,0x0007,0x66FA,0x7000,0x3340,0x0002
        .word   0x3340,0x0004,0x3340,0x0006,0x2340,0x0008,0x2340,0x000C
        .word   0x3340,0x0010,0x3340,0x0030,0x3340,0x0032,0x3340,0x0038
        .word   0x3340,0x0080,0x3340,0x0082,0x08A9,0x0000,0x008B,0x66F8
        .word   0x6100,0xFF12,0x08E9,0x0000,0x008B,0x67F8,0x6100,0xFF06
        .word   0x08A9,0x0000,0x008B,0x6100,0xFF3C,0x303C,0x0040,0x2229
        .word   0x0020,0x0C81,0x5351,0x4552,0x6700,0x0092,0x303C,0x0080
        .word   0x2229,0x0020,0x0C81,0x5344,0x4552,0x6700,0x0080,0x21FC
        .word   0x0088,0x02A2,0x0070,0x303C,0x0002,0x7200,0x122D,0x0001
        .word   0x1429,0x0080,0xE14A,0x8242,0x0801,0x000F,0x660A,0x0801
        .word   0x0006,0x6700,0x0058,0x6008,0x0801,0x0006,0x6600,0x004E
        .word   0x7020,0x41F9,0x0088,0x0000,0x3C28,0x018E,0x4A46,0x6700
        .word   0x0010,0x3429,0x0028,0x0C42,0x0000,0x67F6,0xB446,0x662C
        .word   0x7000,0x2340,0x0028,0x2340,0x002C,0x3E14,0x2C7C,0xFFFF
        .word   0xFFC0,0x4CD6,0x7FF9,0x44FC,0x0000,0x6014,0x43F9,0x00A1
        .word   0x5100,0x3340,0x0006,0x303C,0x8000,0x6004,0x44FC,0x0001

! 0x800: all 68000 code, linked to run at 0x880800
        .incbin "build/m68k.bin"
        .balign 16

!-----------------------------------------------------------------------
! Everything below lives in SDRAM (copied there by the 32X boot ROM)
!-----------------------------------------------------------------------
        .section .vectors,"ax"

        .ifdef  ONCHIP
        /* fast build: each CPU's stack lives in its own 2 KB of on-chip RAM
           (two-way cache mode) */
        .equ    pri_stack, 0xC0000800
        .equ    sec_stack, 0xC0000800
        .equ    CCR_ON, 0x19
        .else
        /* compatible build: stacks in SDRAM, normal 4-way cache */
        .equ    pri_stack, 0x06040000
        .equ    sec_stack, 0x0603F000
        .equ    CCR_ON, 0x11
        .endif

        .align  4
pri_vbr:
        .long   pri_start, pri_stack, pri_start, pri_stack
        .long   pri_err, 0, pri_err, 0, 0, pri_err, pri_err, pri_err, pri_err
        .space  76
        .rept   32
        .long   pri_err
        .endr
        .long   pri_irq, pri_irq, pri_irq, pri_irq, pri_irq, pri_irq, pri_irq, pri_irq

sec_vbr:
        .long   sec_start, sec_stack, sec_start, sec_stack
        .long   sec_err, 0, sec_err, 0, 0, sec_err, sec_err, sec_err, sec_err
        .space  76
        .rept   32
        .long   sec_err
        .endr
        .long   sec_irq, sec_irq, sec_irq, sec_irq, sec_irq, sec_irq, sec_irq, sec_irq

!-----------------------------------------------------------------------
! Master SH-2
!-----------------------------------------------------------------------
        .global pri_start
pri_start:
        mov.l   _p_int_clr,r1
        mov.w   r0,@-r1                 /* PWM INT clear */
        mov.w   r0,@r1
        mov.w   r0,@-r1                 /* CMD INT clear */
        mov.w   r0,@r1
        mov.w   r0,@-r1                 /* H INT clear */
        mov.w   r0,@r1
        mov.w   r0,@-r1                 /* V INT clear */
        mov.w   r0,@r1
        mov.w   r0,@-r1                 /* VRES INT clear */
        mov.w   r0,@r1

        mov.l   _p_frt,r1               /* free run timer: used to ack ints */
        mov     #0x00,r0
        mov.b   r0,@(0x00,r1)
        mov     #0xE2,r0
        mov.b   r0,@(0x07,r1)
        mov     #0x00,r0
        mov.b   r0,@(0x04,r1)
        mov     #0x01,r0
        mov.b   r0,@(0x05,r1)
        mov     #0,r0
        mov.b   r0,@(0x06,r1)
        mov     #1,r0
        mov.b   r0,@(0x01,r1)
        mov     #0x00,r0
        mov.b   r0,@(0x03,r1)
        mov.b   r0,@(0x02,r1)

        mov.l   _p_stk,r15

        mov.l   _p_cctl,r0              /* purge + disable cache */
        mov     #0x10,r1
        mov.b   r1,@r0

        mov     #0,r0                   /* clear bss */
        mov.l   _p_bss_start,r1
        mov.l   _p_bss_end,r2
0:
        cmp/hs  r2,r1
        bt      1f
        mov.l   r0,@r1
        bra     0b
        add     #4,r1
1:
        mov.l   _p_comm0,r0             /* wait for the 68000 to clear "M_OK" */
        mov.l   _p_mok,r1
2:
        mov.l   @r0,r2
        nop
        nop
        cmp/eq  r1,r2
        bt      2b

        mov     #0,r1
        mov.l   r1,@(4,r0)              /* release slave (clear COMM4) */

        mov     #0x80,r0
        mov.l   _p_adapter,r1
        mov.b   r0,@r1                  /* FM = 1 */
        mov     #0x08,r0                /* V int enabled */
        mov.b   r0,@(1,r1)
        mov     #0x10,r0
        ldc     r0,sr                   /* allow ints (level > 1) */

        mov.l   _p_cctl,r0              /* purge, two-way mode + enable cache */
        mov     #CCR_ON,r1
        mov.b   r1,@r0

        mov.l   _p_main,r0
        jmp     @r0
        nop

        .align  2
_p_int_clr:     .long   0x2000401E
_p_stk:         .long   pri_stack
_p_comm0:       .long   0x20004020
_p_frt:         .long   0xFFFFFE10
_p_mok:         .ascii  "M_OK"
_p_adapter:     .long   0x20004000
_p_cctl:        .long   0xFFFFFE92
_p_main:        .long   _pri_main
_p_bss_start:   .long   __bss_start
_p_bss_end:     .long   __bss_end

pri_err:
        bra     pri_err
        nop

! master IRQ: only the V blank interrupt is enabled
pri_irq:
        mov.l   r0,@-r15
        mov.l   r1,@-r15
        mov.l   r2,@-r15

        /* interrupt "bump" (as in Doom 32X Resurrection): FTOA low now,
           high again after the work below, so the pin actually toggles on
           a compare match before the V interrupt is cleared */
        mov.l   _pi_frt,r1
        mov     #0xE0,r0                /* TOCR: output 0 on compare match */
        mov.b   r0,@(0x07,r1)
        mov.b   @(0x07,r1),r0

        mov.l   _pi_vbcount,r1          /* ++vblank_count (cache-through) */
        mov.l   @r1,r0
        add     #1,r0
        mov.l   r0,@r1

        mov.l   _pi_comm12,r1           /* pad_latch |= pad (so short taps */
        mov.w   @r1,r0                  /* between frames are not lost)    */
        mov.l   _pi_padlatch,r1
        mov.w   @r1,r2
        or      r0,r2
        mov.w   r2,@r1

        mov.l   _pi_frt,r1
        mov     #0xE2,r0                /* TOCR: output 1 on compare match */
        mov.b   r0,@(0x07,r1)
        mov.b   @(0x07,r1),r0

        mov.l   _pi_adapter,r1
        mov.w   r0,@(0x16,r1)           /* clear V IRQ */
        mov.w   r0,@(0x14,r1)           /* clear VRES IRQ */

        mov.l   @r15+,r2
        mov.l   @r15+,r1
        mov.l   @r15+,r0
        rte
        nop

        .align  2
_pi_frt:        .long   0xFFFFFE10
_pi_adapter:    .long   0x20004000
_pi_vbcount:    .long   _vblank_count+0x20000000
_pi_comm12:     .long   0x2000402C
_pi_padlatch:   .long   _pad_latch+0x20000000

!-----------------------------------------------------------------------
! Slave SH-2
!-----------------------------------------------------------------------
sec_start:
        mov.l   _s_int_clr,r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1
        mov.w   r0,@-r1
        mov.w   r0,@r1

        mov.l   _s_stk,r15

        mov.l   _s_cctl,r0              /* purge + disable cache */
        mov     #0x10,r1
        mov.b   r1,@r0

        mov.l   _s_comm4,r0             /* wait for master to clear "S_OK" */
        mov.l   _s_sok,r1
0:
        mov.l   @r0,r2
        nop
        nop
        cmp/eq  r1,r2
        bt      0b

        mov.l   _s_adapter,r1
        mov     #0x00,r0                /* no interrupts on the slave */
        mov.b   r0,@(1,r1)
        mov     #0xF0,r0
        ldc     r0,sr

        mov.l   _s_cctl,r0              /* purge, two-way mode + enable cache */
        mov     #CCR_ON,r1
        mov.b   r1,@r0

        mov.l   _s_main,r0
        jmp     @r0
        nop

        .align  2
_s_int_clr:     .long   0x2000401E
_s_stk:         .long   sec_stack
_s_comm4:       .long   0x20004024
_s_sok:         .ascii  "S_OK"
_s_adapter:     .long   0x20004000
_s_cctl:        .long   0xFFFFFE92
_s_main:        .long   _sec_main

sec_err:
        bra     sec_err
        nop

sec_irq:
        rte
        nop

!-----------------------------------------------------------------------
! Level data in ROM (accessed through the SH-2 cache at 0x02xxxxxx)
!-----------------------------------------------------------------------
        .section .romdata,"a"
        .balign 16
        .global _level_data
_level_data:
        .incbin "build/level.lvl"
        .balign 16

# Quake 32X build
TC      ?= /opt/toolchains/sega/bin
CC      = $(TC)/sh-elf-gcc
AS      = $(TC)/sh-elf-as
OBJCOPY = $(TC)/sh-elf-objcopy
MDAS    = $(TC)/m68k-elf-as
MDLD    = $(TC)/m68k-elf-ld
MDOBJCOPY = $(TC)/m68k-elf-objcopy

CFLAGS_OPT ?= -O2
CFLAGS  = -m2 -mb $(CFLAGS_OPT) -ffreestanding -fno-builtin -nostdlib -Wall -Wno-unused-function \
          -fomit-frame-pointer -fno-common -Isrc $(EXTRA_CFLAGS)
LDFLAGS = -m2 -mb -nostdlib -T src/mars.ld -Wl,-Map=build/q32x.map

SRCS    = $(wildcard src/*.c)
ASRCS   = $(filter-out src/crt0.s,$(wildcard src/*.s))
OBJS    = build/crt0.o $(patsubst src/%.c,build/%.o,$(SRCS)) $(patsubst src/%.s,build/%.o,$(ASRCS))

MAP     ?= e1m1

all: quake32x.32x

build/m68k.bin: src-md/md.s | build
	$(MDAS) -m68000 --register-prefix-optional -o build/md.o $<
	$(MDLD) -Ttext=0x880800 -o build/md.elf build/md.o
	$(MDOBJCOPY) -O binary build/md.elf $@

data/$(MAP).lvl: tools/mkmap.py data/pak0.pak
	python3 tools/mkmap.py data/pak0.pak maps/$(MAP).bsp $@

build/level.lvl: data/$(MAP).lvl | build
	cp $< $@

# FAST=1: stacks and the span loop in SH-2 on-chip RAM (two-way cache mode).
# Faster, but needs an emulator/hardware that supports the cache data array
# as RAM. The default build avoids it.
ifeq ($(FAST),1)
ASDEFS  = --defsym ONCHIP=1
CFLAGS += -DONCHIP
endif

# DEMO=1: scripted walkthrough (camera route + menu macros) instead of the pad
ifeq ($(DEMO),1)
CFLAGS += -DDEMO
OBJS   += build/demo_data.o
endif
# DEMO=bench: the demo route at fixed settings (set with the pad), no macros
ifeq ($(DEMO),bench)
CFLAGS += -DDEMO -DDEMO_BENCH
OBJS   += build/demo_data.o
endif

build/demo_data.c: tools/make_demo.py data/$(MAP)_route.json | build
	python3 tools/make_demo.py data/$(MAP)_route.json $@

build/demo_data.o: build/demo_data.c src/demo.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/crt0.o: src/crt0.s build/m68k.bin build/level.lvl
	$(AS) --big $(ASDEFS) -o $@ $<

build/%.o: src/%.s | build
	$(AS) --big -o $@ $<

# geometry code is instruction-cache bound: smaller beats faster here
OPT_r_geom = -Os
OPT_r_setup = -Os
OPT_r_main = -Os

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) $(OPT_$*) -c -o $@ $<

build/q32x.elf: $(OBJS) src/mars.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

quake32x.32x: build/q32x.elf tools/fixrom.py
	$(OBJCOPY) -O binary $< build/q32x.bin
	python3 tools/fixrom.py build/q32x.bin $@

build:
	mkdir -p build

clean:
	rm -rf build quake32x.32x

.PHONY: all clean

# Quake 32X build
TC      ?= /opt/toolchains/sega/bin
CC      = $(TC)/sh-elf-gcc
AS      = $(TC)/sh-elf-as
OBJCOPY = $(TC)/sh-elf-objcopy
MDAS    = $(TC)/m68k-elf-as
MDLD    = $(TC)/m68k-elf-ld
MDOBJCOPY = $(TC)/m68k-elf-objcopy

# the release (VERSION: 0.x.y while in development, 1.0.0 the first public release)
VERSION := $(shell cat VERSION 2>/dev/null || echo 0.0.0)

CFLAGS_OPT ?= -O2
CFLAGS  = -m2 -mb $(CFLAGS_OPT) -ffreestanding -fno-builtin -nostdlib -Wall -Wno-unused-function \
          -fomit-frame-pointer -fno-common -Isrc -DQ32X_VERSION='"$(VERSION)"' $(EXTRA_CFLAGS)
LDFLAGS = -m2 -mb -nostdlib -T src/mars.ld -Wl,-Map=build/q32x.map

SRCS    = $(wildcard src/*.c)
ASRCS   = $(filter-out src/crt0.s,$(wildcard src/*.s))
OBJS    = build/crt0.o $(patsubst src/%.c,build/%.o,$(SRCS)) $(patsubst src/%.s,build/%.o,$(ASRCS))

MAP     ?= e1m1
# the levels in the cartridge, in the order of play
LEVELS  = e1m1 e1m2 e1m3
LVLS    = $(patsubst %,build/%.lvl,$(LEVELS))
# the attract demo is in its level's blob
DEMO_e1m3 = --demo demo1.dem

all: quake32x.32x

build/m68k.bin: src-md/md.s src-md/md.ld | build
	$(MDAS) -m68000 --register-prefix-optional -o build/md.o $<
	$(MDLD) -T src-md/md.ld -o build/md.elf build/md.o
	$(MDOBJCOPY) -O binary -j .text -j .ramtext build/md.elf $@

# Mega Drive tiles, palettes and the pre-rendered view weapons
build/md_data.bin: tools/mkmd.py data/pak0.pak | build
	python3 -I tools/mkmd.py data/pak0.pak $@

LVLTOOLS = tools/mkmap.py tools/mkents.py tools/movers.py tools/anorms.py tools/mkdemo.py tools/dem.py

# the host build's level (little-endian)
data/$(MAP)_le.lvl: $(LVLTOOLS) data/pak0.pak
	python3 -I tools/mkmap.py --le data/pak0.pak maps/$(MAP).bsp $@

build/%.lvl: $(LVLTOOLS) data/pak0.pak | build
	python3 -I tools/mkmap.py data/pak0.pak maps/$*.bsp $@ $(DEMO_$*)

src/g_ids.h src/g_frames.h: tools/mkents.py
	python3 -I tools/mkents.py --header src/g_ids.h src/g_frames.h data/pak0.pak

# FAST=1: stacks and the span loop in SH-2 on-chip RAM (two-way cache mode).
# Faster, but needs an emulator/hardware that supports the cache data array
# as RAM. The default build avoids it.
ifeq ($(FAST),1)
ASDEFS  = --defsym ONCHIP=1
CFLAGS += -DONCHIP
endif

# DEMO=1: scripted walkthrough (camera route + menu macros) instead of the pad
ifeq ($(DEMO),1)
CFLAGS += -DDEMO -DDEMO_CAMERA
OBJS   += build/demo_data.o
endif
# DEMO=bench: the demo route at fixed settings (set with the pad), no macros
ifeq ($(DEMO),bench)
CFLAGS += -DDEMO -DDEMO_CAMERA -DDEMO_BENCH
OBJS   += build/demo_data.o
endif
# the demo bot's route (pause menu, Demo)
OBJS   += build/bot_data.o

build/bot_data.c: tools/make_bot.py data/$(MAP)_botroute.json | build
	python3 tools/make_bot.py data/$(MAP)_botroute.json $@

build/bot_data.o: build/bot_data.c src/bot.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/bot_data.c: tools/make_bot.py

build/demo_data.c: tools/make_demo.py data/$(MAP)_route.json | build
	python3 tools/make_demo.py data/$(MAP)_route.json $@

build/demo_data.o: build/demo_data.c src/demo.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/crt0.o: src/crt0.s build/m68k.bin build/md_data.bin
	$(AS) --big $(ASDEFS) -o $@ $<

build/%.o: src/%.s | build
	$(AS) --big -o $@ $<

# geometry code is instruction-cache bound: smaller beats faster here
OPT_r_geom = -Os
OPT_r_setup = -Os
OPT_r_main = -Os

build/menu.o: VERSION

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) $(OPT_$*) -c -o $@ $<

build/q32x.elf: $(OBJS) src/mars.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

quake32x.32x: build/q32x.elf tools/mkrom.py $(LVLS) VERSION
	$(OBJCOPY) -O binary $< build/q32x.bin
	python3 -I tools/mkrom.py --version $(VERSION) build/q32x.bin $@ $(LVLS)

build:
	mkdir -p build

clean:
	rm -rf build quake32x.32x

.PHONY: all clean

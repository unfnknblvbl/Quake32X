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

SRCS    = $(filter-out src/asmoff.c,$(wildcard src/*.c))
ASRCS   = $(filter-out src/crt0.s,$(wildcard src/*.s))
CSRCS_ASM = $(wildcard src/*.S)
OBJS    = build/crt0.o $(patsubst src/%.c,build/%.o,$(SRCS)) $(patsubst src/%.s,build/%.o,$(ASRCS)) \
          $(patsubst src/%.S,build/%.o,$(CSRCS_ASM))

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

# The converters (levels, the Mega Drive data, the cartridge) are the
# editor's: editor/q32x, built with CMake into build/tools. (tools/*.py are
# the original Python versions; the C++ ones give the same bytes.)
CONVERT_SRC = $(wildcard editor/q32x/lib/src/*.cpp editor/q32x/lib/include/q32x/*.h editor/q32x/cli/*.cpp) editor/q32x/CMakeLists.txt
CONVERT = build/tools/q32x-convert

$(CONVERT): $(CONVERT_SRC) | build
	cmake -S editor/q32x -B build/tools -DCMAKE_BUILD_TYPE=Release -DQ32X_BUILD_TESTS=OFF > /dev/null
	cmake --build build/tools --target q32x-convert > /dev/null
	@touch $@

# Mega Drive tiles, palettes and the pre-rendered view weapons
build/md_data.bin: $(CONVERT) data/pak0.pak | build
	$(CONVERT) mdgfx --packed --flat-weapons --weapon-sizes 0,2,2,3,4,5 data/pak0.pak $@

# the host build's level (little-endian)
data/$(MAP)_le.lvl: $(CONVERT) data/pak0.pak
	$(CONVERT) level --le data/pak0.pak maps/$(MAP).bsp $@

build/%.lvl: $(CONVERT) data/pak0.pak | build
	$(CONVERT) level data/pak0.pak maps/$*.bsp $@ $(DEMO_$*)

src/g_ids.h src/g_frames.h: editor/q32x/lib/src/gamedata.cpp
	$(MAKE) $(CONVERT)
	$(CONVERT) headers data/pak0.pak src/g_ids.h src/g_frames.h

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

build/bot_data.c: $(CONVERT) data/$(MAP)_botroute.json | build
	$(CONVERT) botroute data/$(MAP)_botroute.json $@

build/bot_data.o: build/bot_data.c src/bot.h
	$(CC) $(CFLAGS) -c -o $@ $<


build/demo_data.c: tools/make_demo.py data/$(MAP)_route.json | build
	python3 tools/make_demo.py data/$(MAP)_route.json $@

build/demo_data.o: build/demo_data.c src/demo.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/crt0.o: src/crt0.s build/m68k.bin build/md_data.bin
	$(AS) --big $(ASDEFS) -o $@ $<

build/%.o: src/%.s | build
	$(AS) --big -o $@ $<

# structure offsets for the assembly (.S) files
build/asmoff.h: src/asmoff.c src/*.h | build
	$(CC) $(CFLAGS) -S -o build/asmoff.s src/asmoff.c
	sed -n 's/^->\([A-Z0-9_]*\) #\(-*[0-9]*\).*/#define \1 \2/p' build/asmoff.s > $@

build/%.o: src/%.S build/asmoff.h | build
	$(CC) $(CFLAGS) -Ibuild -c -o $@ $<

# geometry code is instruction-cache bound: smaller beats faster here
OPT_r_geom = -Os
OPT_r_setup = -Os
OPT_r_main = -Os

build/menu.o: VERSION

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) $(OPT_$*) -c -o $@ $<

build/q32x.elf: $(OBJS) src/mars.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) -lgcc

# The cartridge is the project data/quake32x.q32x exported: the three
# levels with the editor's optimisations that keep the picture as it is
# (door areas, simpler collision, merged faces, vertex order, the shared
# sound bank, packed weapon tiles with flat-shaded versions). PLAIN=1 builds
# the levels as converted without them.
# PROJECT=file.q32x exports another project instead (build.sh makes one
# from the maps you choose).
PROJECT ?= data/quake32x.q32x
ifeq ($(PLAIN),1)
quake32x.32x: build/q32x.elf $(CONVERT) $(LVLS) VERSION
	$(OBJCOPY) -O binary $< build/q32x.bin
	$(CONVERT) rom --version $(VERSION) build/q32x.bin $@ $(LVLS)
else
quake32x.32x: build/q32x.elf $(CONVERT) $(PROJECT) data/pak0.pak VERSION
	$(OBJCOPY) -O binary $< build/q32x.bin
	rm -f build/export/*.lvl
	$(CONVERT) export $(PROJECT) build/export
	cp build/export/quake32x.32x $@
endif

# the game program the editor ships (editor/q32x/program): the Mega Drive
# data (Quake's font, border and weapon renders) left out, since the editor
# puts in the project's own when it builds a cartridge
program: build/q32x.elf
	$(OBJCOPY) -O binary $< build/q32x.bin
	mkdir -p editor/q32x/program
	head -c 196608 build/q32x.bin > editor/q32x/program/q32x-program.bin
	printf 'MDQ1' >> editor/q32x/program/q32x-program.bin
	head -c 60 /dev/zero >> editor/q32x/program/q32x-program.bin

build:
	mkdir -p build

clean:
	rm -rf build quake32x.32x

.PHONY: program all clean

# Quake E1M1 for the Sega 32X

A from-scratch Quake world renderer for a **stock 32X** (two 23 MHz SH-2s,
256 KB SDRAM, no extra cartridge hardware), running the first level of Quake
(E1M1) converted from the shareware `pak0.pak`.

It's a rendering tech demo. You can walk or fly around E1M1, but there are no
monsters, items, weapons, sound or game logic yet.

## Controls (3- or 6-button pad)

| Input | Action |
|---|---|
| D-pad up / down | move forward / back |
| D-pad left / right | turn |
| A + left / right | strafe |
| A + up / down | look up / down |
| B | jump (walk mode) / fly up (fly mode) |
| C | fly down (fly mode) |
| START | pause menu |

**Pause menu.** Up/Down picks an item, Left/Right or A/B/C changes it. The
scene keeps rendering behind the menu, so every change shows up immediately.

* **Textures**: ON gives perspective-correct texture mapping. OFF gives flat
  shading, with one colour per polygon taken from the texture's average and the
  face's average light.
* **Lighting**: Quake lightmaps on or off. This applies in both texture modes.
* **Draw distance**: a slider with 256, 384, 512, 768, 1024, 1536, 2048, 3072
  and max. Geometry beyond the far plane is not drawn, and the far area shows
  as black.
* **Show FPS**: frame rate, plus the screen row where the second CPU takes over.
* **Movement**: walk (gravity, steps, collision with the player hull) or fly.
* **Resume**

## Performance

There are two sets of numbers, because emulators are optimistic about 32X
timing:

* **Real-hardware estimate.** This comes from `tools/sh2sim`, a cycle-level
  dual SH-2 model written for this project. It models SDRAM bursts, cartridge
  ROM wait states, the framebuffer FIFO, cache misses and both CPUs contending
  for the bus, using figures from the 32X hardware manual.
* **PicoDrive.** This emulator charges no memory wait states.

| Setting (spawn view / walking down the hall) | Real HW estimate | PicoDrive |
|---|---|---|
| Textured + lit (default) | 14 / 17 fps | 18 / 22 fps |
| Textured, lighting off | 13.5 / 20 fps | 18 / 23 fps |
| Flat shaded + lit | 19 / 31 fps | 29 / 42 fps |
| Flat shaded, lighting off | 18 / 31 fps | 28 / 42 fps |
| Textured, draw distance 512 | 16 / 18 fps | 21 / 22 fps |
| Flat, draw distance 512 | 24 / 31 fps | 34 / 41 fps |

Rendering is 160×112 with every pixel doubled to 320×224. Frame rates vary a
lot with the view: in the simulator, a 24-second walk through the level ran
between 8 and 40 fps. Run `tools/bench_modes.sh` and `tools/bench_pico.sh` to
reproduce the table.

## How it works

The first step is offline conversion. `tools/mkmap.py` turns the BSP into a
big-endian, fixed-point blob that stays in cartridge ROM. It produces:

* nodes (32 bytes each), with the splitting plane stored inside the node and
  faces sorted front-facing first;
* a compact 16-byte geometry record per face, holding vertex range, flags,
  flat colours and a bounding sphere;
* 13.3 fixed-point vertices;
* 8-bit lightmaps already inverted into the colormap's light scale;
* the PVS;
* the clip hull used for collision;
* the palette, colormap, font and sky texture.

Doors and platforms are clipped into the world leaves as static fragments.
Doors are placed in their open position, because there is no game logic yet
to move them.

At runtime, each frame goes like this:

1. **Master SH-2.** Reads input, moves the player against the clip hull, and
   rebuilds the PVS bitsets when the camera changes leaf.
2. **Screen split.** The two SH-2s each render a horizontal band of the
   screen. The split row moves each frame to balance the load: each CPU counts
   how long it waits for the other, and the split shifts by that amount.
3. **Batched pipeline.** On each CPU, every stage runs over a batch of 32
   faces. That keeps each stage's code inside the 2 KB cache instead of
   refetching about 5 KB of code for every face. The stages are:
   * **A: BSP walk.** Front-to-back, with an explicit stack, PVS tests and
     frustum culling of node bounding boxes (with clip flags). It collects
     candidate faces.
   * **B: geometry.** A bounding-sphere test against the frustum, then vertex
     transforms using 16-bit multiplies, through a per-frame vertex cache.
     The divide for projection runs on the hardware divider while the
     outcodes are computed. Faces are then clipped in view space, but only
     against the planes they actually cross.
   * **C: rasterization.** Walks each polygon's left and right edges into
     per-row coverage gap lists. Nothing is ever overdrawn, rows that are
     already covered are skipped, and the CPU stops once its band is full.
   * **D: shading.** Sets up 1/z, s/z and t/z gradients and fetches the
     surface from a Quake-style surface cache of lit textures (one per CPU,
     ring-allocated). Then it draws the spans.
4. **Spans.** A hand-written SH-2 assembly loop does perspective-correct
   mapping, recomputed every 16 pixels. The 1/z divide for the next span runs
   on the hardware divider while the current span is drawn.
5. **Vertical blank.** Both CPUs begin the next frame's walk and geometry
   while the hardware is still waiting for vertical blank, and block only at
   their first framebuffer write.

Other tricks:

* **Two-way cache mode.** Each SH-2's stack and the span loop live in its 2 KB
  of on-chip RAM (0xC0000000). Register spills then avoid the write-through
  SDRAM bus, and the hot loop never misses the cache.
* **No variable shifts.** The SH-2 has no barrel shifter, and GCC turns
  `x >> n` into library calls. Mip levels and bit tests are written to avoid
  them.
* **Compiler flags per file.** Geometry code is compiled with `-Os`, since it
  is bound by instruction-cache misses. Span and surface code uses `-O2`.

## Building

Requirements:

* an `sh-elf` and `m68k-elf` GNU toolchain (`TC=` in the Makefile, default
  `/opt/toolchains/sega/bin`);
* Python 3;
* Quake's `pak0.pak` copied to `data/pak0.pak`.

```
make                          # -> quake32x.32x (2 MB)
make EXTRA_CFLAGS=-DNO_ORAM_CODE   # keep the span loop in SDRAM (about 2% slower)
```

The ROM contains level data converted from id Software's Quake shareware
files. Use it yourself, but don't redistribute it.

## Tools

* `tools/mkmap.py`: BSP to level blob converter (`--le` writes a
  little-endian copy for the host build).
* `tools/sh2sim.c`: the dual SH-2 and 32X bus timing model, with
  per-function profiles (`-prof`), hottest instructions (`-hot N`), scripted
  pad input and screenshots.
* `tools/harness.c`: a headless libretro front end for PicoDrive, used for
  screenshots and scripted input.
* `host/`: the same renderer built for a PC, with sanitizers and per-frame
  statistics (`tools/hosttest.sh`).

## Not done yet / ideas

* Monsters, items, weapons and game logic (for example by porting
  MG24Quake's translated QuakeC), moving doors and platforms, and sound
  through PWM.
* Assembly versions of geometry stages B and C, which are the biggest
  remaining cost.
* Hardware verification. Everything has been checked in PicoDrive and the
  cycle model, but not on a real 32X. The riskiest part is running code from
  the cache data array; `-DNO_ORAM_CODE` removes that dependency.

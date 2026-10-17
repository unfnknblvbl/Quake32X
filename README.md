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

The controls are shown at the bottom of the screen for the first few seconds.

Input and timing don't depend on any single interrupt. The game clock follows
whichever vblank counter is running, either the 68000's or the master SH-2's,
and the master also samples the pad between render batches.

**Pause menu.** Up/Down picks an item, Left/Right or A/B/C changes it. The
scene keeps rendering behind the menu, so every change shows up immediately.

* **Textures**: ON gives perspective-correct texture mapping. OFF gives flat
  shading, with one colour per polygon taken from the texture's average and the
  face's average light.
* **Lighting**: Quake lightmaps on or off. This applies in both texture modes.
* **Water**: SOLID (as in Quake) or SEE-THROUGH. See-through draws water and
  slime as a fine mesh over whatever is below the surface, in both texture
  modes. Every other screen pixel is the liquid, alternating on each row, so
  on a TV it blends like transparency.
* **Draw distance**: a slider with 256, 384, 512, 768, 1024, 1536, 2048, 3072
  and max. Geometry beyond the far plane is not drawn, and the far area shows
  as black.
* **Screen size**: 100% down to 50% in 10% steps, like PC Quake's viewsize.
  The view keeps its 90 degree field of view and shrinks, and Quake's border
  tile fills the rest of the screen. Fewer pixels to draw makes textured mode
  faster.
* **Show FPS**: frame rate, plus the screen row where the second CPU takes over.
* **Movement**: walk (gravity, steps, collision with the player hull) or fly.
* **Resume**

## Performance

The best single measure is the average over the whole walkthrough route
(the start, the lift, the courtyard, the pool and the channel), from
`tools/bench_route.sh`. This is the real-hardware estimate:

| Screen size | 100% | 90% | 80% | 70% | 60% | 50% |
|---|---|---|---|---|---|---|
| Textured + lit, fps | 18.4 | 20.0 | 21.7 | 23.8 | 25.8 | 27.5 |
| Flat shaded + lit, fps | 31.2 | 32.0 | 32.7 | 33.9 | 34.8 | 35.7 |

With see-through water the route averages 15.3 fps textured and 27.6 fps
flat at full screen, because everything under the water has to be drawn as
well. Textured at 70% with see-through water averages 20.7 fps. Views with
no liquid in sight cost nothing extra. Flat shading gains less from a
smaller screen than texturing does, because it is limited by geometry
rather than by pixels.

There are two sets of numbers, because emulators are optimistic about 32X
timing:

* **Real-hardware estimate.** This comes from `tools/sh2sim`, a cycle-level
  dual SH-2 model written for this project. It models SDRAM bursts, cartridge
  ROM wait states, the framebuffer FIFO, cache misses and both CPUs contending
  for the bus, using figures from the 32X hardware manual.
* **PicoDrive.** This emulator charges no memory wait states.

The table below is from before the screen size and water options. Those
options cost about 1.5% when they are off.

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
   * **Liquids** use Quake's per-pixel warp on the 64x64 base texture (Quake
     never mipmaps them either). Water and slime textures are copied from
     cartridge ROM to SDRAM at start-up, because the warp reads them in a
     scattered order and ROM cache refills cost about 6 times as much.
   * **See-through liquids** are rasterized against the coverage lists
     without filling them, so the geometry under the surface is drawn as
     usual. The liquid's spans are kept, then drawn after everything else
     (back to front), writing one byte of each two-pixel framebuffer word.
     Faces that are only visible from inside a liquid are drawn one mip level
     coarser while you look at them from above, since half their pixels are
     covered. The buffers for this come out of the surface cache memory only
     while the option is on.
   * **Screen size** changes the projection (focal length = half the view
     width) and offsets the framebuffer pointer. The border tile is drawn
     only into framebuffers that had the menu over it.
5. **Vertical blank.** Both CPUs begin the next frame's walk and geometry
   while the hardware is still waiting for vertical blank, and block only at
   their first framebuffer write.

Other tricks:

* **Two-way cache mode** (`FAST=1` only). Each SH-2's stack and the span
  loop live in its 2 KB of on-chip RAM (0xC0000000). Register spills then
  avoid the write-through SDRAM bus, and the hot loop never misses the cache.
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
make            # -> quake32x.32x (2 MB), the compatible build
make FAST=1     # stacks and span loop in SH-2 on-chip RAM (two-way cache mode)
```

The default build uses the normal 4-way cache and keeps everything in SDRAM.
That is within a few percent of the `FAST=1` build and works on emulators
that don't implement the SH-2 cache-as-RAM mode.

E1M1 is part of the Quake shareware episode, so the ROM can be shared.

## Walkthrough demo

```
make DEMO=1     # plays a scripted walkthrough instead of reading the pad
```

The demo build follows a camera route through the level and works the pause
menu by itself, the same way a player would. It tries the screen sizes on the
way to the pool, then sweeps the same view over the pool four times
(textured and flat shaded, each with solid and see-through water). Then it
wades in, dips under the surface to look up through it, and follows the
channel. `make DEMO=bench` follows the same route with no menu changes, for
`tools/bench_route.sh`. The route comes
from `tools/plan_route.py`, a path planner that walks the player clip hull.
`tools/make_demo.py` then turns that route into a 60 Hz camera table plus
timed menu button presses. The route is stored in `data/e1m1_route.json`.

To record it, run `sh2sim ... -video frames.rgb -vlog frames.log`. This
writes every displayed frame as raw 320x224 RGB24, and logs the demo phase
and FPS for each vblank, which is useful for captions.

## Tools

* `tools/mkmap.py`: BSP to level blob converter (`--le` writes a
  little-endian copy for the host build).
* `tools/sh2sim.c`: the dual SH-2 and 32X bus timing model, with
  per-function profiles (`-prof`), hottest instructions (`-hot N`), scripted
  pad input, screenshots and video capture (`-video`).
* `tools/plan_route.py` and `tools/make_demo.py`: the route planner and the
  demo script generator.
* `tools/bench_route.sh`, `tools/bench_view.sh` and `tools/bench_modes.sh`:
  real-hardware estimates averaged over the walkthrough route, from one fixed
  view (built with `-DSTART_POS=x,y,z,yaw,pitch`), or at the spawn point and
  down the first hall.
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
* Hardware verification. Everything has been checked in PicoDrive (dynarec
  and interpreter) and the cycle model, but not on a real 32X.

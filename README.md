# Quake for the Sega 32X: the first three levels

A from-scratch Quake engine for a **stock 32X** (two 23 MHz SH-2s, 256 KB
SDRAM), running the first three levels of Quake's shareware episode, E1M1
(the Slipgate Complex), E1M2 (Castle of the Damned) and E1M3 (the
Necropolis), converted from the shareware `pak0.pak`. They are in a 10 MB
cartridge with Sega's standard ROM mapper (below).

As in Quake, the machine starts with a demo: Quake's own `demo1`, recorded
in E1M3, plays as the attract mode at power-on. It starts again when it
ends, after the closing screen and after the demo bot's run. START opens
the menu over it, and **New game** starts E1M1.

The three levels are playable from start to exit and lead into each other,
with Quake's carry-over of health, armour, weapons and ammunition at each
exit. All their monsters are in: grunts, rottweilers, ogres, knights,
fiends, scrags and zombies. So are the axe, shotgun, super shotgun, nailgun
and grenade launcher; health, the three armours, ammunition, backpacks, the
silver and gold keys and the doors they open, Quad Damage, the Biosuit and
the Ring of Shadows; exploding boxes, spike traps, doors, lifts, trains,
buttons, secret doors, teleporters, triggers and messages; swimming,
drowning and burning in slime and lava; Quake's colour flashes for damage,
pickups, powerups and liquids; a status bar; Quake's end-of-level figures;
and Quake's sounds through the 32X PWM, with its ambient sounds. After
E1M3's exit a closing screen ends the cartridge, and a button returns to
the demo.

A menu setting, **Monsters at once**, caps how many monsters are in the
world at a time (2 by default); the rest wait in a queue and teleport in,
with Quake's teleport sound and particle burst, as others die. Bodies (and
the heads of gibbed monsters) teleport away in the same burst a second
after they come to rest, so there is less to draw and think about. **Demo
(bot plays)** in the same menu lets a bot play E1M1 from the start.

The Mega Drive does its share: its VDP draws the text, the border around a
reduced view and the view weapon (pre-rendered sprites) over the 32X
picture, so the SH-2s spend all their time on the 3D view.

## Controls (3- or 6-button pad)

| Input (default) | Action |
|---|---|
| D-pad up / down | move forward / back |
| D-pad left / right | turn |
| A + left / right | strafe |
| A + up / down | look up / down |
| C | fire (walk mode) / fly down (fly mode) |
| A + C | next weapon (axe, shotgun, super shotgun, nailgun, grenade launcher) |
| B | jump (walk mode) / fly up (fly mode) |
| START | the menu (pauses the game) |

The buttons can be changed in the pause menu (**Controls...**): fire, jump
and strafe/look can each go on A, B, C, X, Y, Z or MODE (the last four on a
6-button pad), next weapon on a free button or the strafe + fire
combination, and looking up and down can be inverted. The controls, with the
buttons as set, are shown at the bottom of the screen for the first few
seconds.

**Analogue pad (XE-1 AP).** If port 1 holds Dempa / Micomsoft's XE-1 AP, or
the 32X-HD emulator's version of it made from a PlayStation or Xbox pad, the
game uses its sticks. They have their own page in the pause menu
(**Analogue pad...**), separate from the regular controls. By default the
left stick moves and strafes, the right stick turns and looks, the right
trigger fires, the left trigger jumps and C changes weapon; the stick also
works the menus. 32X-HD's extension of the protocol adds the right stick's
up and down and the two triggers separately. Without it, the XE-1 AP's
throttle gives the triggers (or, if set, looking up and down).

Input and timing don't depend on any single interrupt. The game clock follows
whichever vblank counter is running, either the 68000's or the master SH-2's,
and the master also samples the pad between render batches.

**Menu.** START opens it, over the attract demo or the game (which pauses).
Up/Down picks an item, Left/Right or A/B/C changes it. The scene keeps
rendering behind the menu, so every change shows up immediately.

* **New game** (A, B or C): E1M1 from the start, with 100 health, the axe,
  the shotgun and 25 shells.

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
* **Monsters at once**: Off, 1, 2, 3, 4, 6, 8 or Unlimited (default 2).
  With a cap, the nearest queued monster whose spot you could see teleports
  in whenever a slot is free. Monsters left far behind (out of sight and over
  800 units away for 6 seconds) go back to the queue. Unlimited spawns them
  all at the start, as Quake does.
* **Show FPS**: the frame rate.
* **Movement**: walk (gravity, steps, collision with the player hull) or fly.
* **Controls...**: the page described above; Defaults puts the buttons back.
* **Analogue pad...**: what each stick does (left: move + strafe or move +
  turn; right: turn + look or just turn), turn speed (90 to 360 degrees a
  second at full stick) and look speed (60 to 240), look up/down inverted,
  the dead zone, what the throttle does without the extension, and which
  trigger or button fires, jumps and changes weapon. The bottom line shows
  what is in port 1.
* **Demo (bot plays)**: the bot starts E1M1 and plays it to the exit
  (below), then the attract demo plays again. Turning it off hands the
  controls back where it is.
* **Resume**

## The levels and the cartridge

| Level | Blob | Pages | Monsters | Notes |
|---|---|---|---|---|
| E1M1, the Slipgate Complex | 2.6 MB | 1-5 | 18 grunts, 5 rottweilers | the lift, the bridge over the slime |
| E1M2, Castle of the Damned | 2.9 MB | 6-11 | 12 grunts, 6 ogres, 5 knights, a fiend | the silver key, trains, spike traps |
| E1M3, the Necropolis | 3.4 MB | 12-18 | 32 zombies, 8 ogres, 5 scrags, 2 fiends | the gold key, trains; the attract demo is in its blob |

(The monster counts are Quake's normal skill.)

The 32X sees at most 4 MB of cartridge, so the cartridge has Sega's standard
mapper (the one *Super Street Fighter II* introduced): the 4 MB window is
eight 512 KB pages, the first fixed and the other seven set by registers at
0xA130F3-0xA130FF that only the 68000 can write. Page 0 holds the program
(415 KB, including the Mega Drive's tiles and the weapon sprites) and a
directory of the levels; each level starts on a page boundary. To change
level, the master SH-2 asks the 68000 (through the same link that carries
the text) to map the level's pages from the second page on, waits for its
answer, and purges its cache; the slave purges its own at the start of its
next frame. The level is then always at 0x02080000, and since every offset
in a blob is from the blob's own start, where it lies in the cartridge does
not matter. A level can be up to 3.5 MB.

PicoDrive turns the mapper on for any 32X ROM over 4 MB. On real hardware it
needs a cartridge or flash cart that provides the mapper for a 32X game;
that has not been tried.

Between levels, both CPUs stop between frames: the master stops the sounds,
maps the next level, resets the renderer (the liquid textures are copied to
SDRAM again, and both surface caches are emptied) and spawns the level's
entities. Each exit carries the player's stats on as Quake's
`SetChangeParms` does (health back to between 50 and 100, everything else
kept). Dying restarts the level.

## The attract demo

`demo1.dem` is a recording of Quake's network messages: entity updates,
sounds, particles, temporary entities, the player's stats and the server's
prints, timed in seconds. `tools/mkdemo.py` turns it into a compact,
big-endian stream (214 KB for the 76 seconds of `demo1`) stored in E1M3's
blob: per frame its time in 1/100 s, the view angles, and records for the
entities that changed (model, frame, skin, position in 1/8 units, angles,
effects), the doors, lifts and trains (as offsets from where they start),
the view position, sounds, particle bursts, explosions and other temporary
entities, the status bar's figures, damage and pickup flashes, messages and
the kill and secret counts. The demo's model and sound numbers are mapped
to the level's own, and anything the demo uses that the level would not
otherwise need is added to the level when it is built.

Playing it back runs no game logic at all: `src/playdemo.c` puts the
entities, the movers and the camera where the recording has them,
interpolated between the last two recorded frames as Quake's client does,
and the renderer, the sound mixer and the status bar take it from there.
The demo keeps real time whatever the frame rate, as Quake's does: its 76
seconds take 76 seconds. (The game itself takes at most 6 ticks a frame, so
below 10 fps it slows down rather than taking longer steps.)

## Performance

Rendering is 160×112 with every pixel doubled to 320×224. All figures are
frames per second. There are two sets of numbers, because emulators are
optimistic about 32X timing:

* **Real-hardware estimate** (all figures unless they say PicoDrive). This
  comes from `tools/sh2sim`, a cycle-level dual SH-2 model written for this
  project. It models SDRAM bursts, cartridge ROM wait states, the
  framebuffer FIFO, cache misses and both CPUs contending for the bus, using
  figures from the 32X hardware manual.
* **PicoDrive.** This emulator charges no memory wait states.

**Over the E1M1 walkthrough route** (`tools/bench_route.sh`: the start, the
lift, the courtyard, the pool and the channel; no monsters):

| Screen size | 100% | 90% | 80% | 70% | 60% | 50% |
|---|---|---|---|---|---|---|
| Textured + lit | 14.9 | 16.0 | 19.5 | 20.5 | 20.8 | 20.9 |
| Flat shaded + lit | 25.9 | 24.5 | 27.7 | 27.7 | 25.8 | 25.2 |

With see-through water the route averages 9.8 textured and 22.9 flat at
full screen, and 16.5 and 25.2 at 70%. Below 70% the figures hardly change.
A frame is shown only at a vertical blank, so frame times come in steps of
1/60 s, and at these sizes most frames probably take just over two blanks
because of the work that does not shrink with the screen (the game, the
scene setup, the Mega Drive link). The one-level build before this one
measured the same 20.9 textured at 50%.

The three-level build is a little slower than that one-level build over the
same route: 14.9 against 15.4 textured (3%), 9.8 against 10.4 with
see-through water (6%), and 25.2 against 26.9 flat at 50%.

**E1M3 is much heavier.** The attract demo averages 6.9 fps textured at full
screen, from 3.7 to 13.5.

**The demo bot's E1M1 run** (flat shaded, see-through water, 80% screen, 2
monsters at once) averages 12.8 fps and reaches the exit after about two
and a half minutes.

**Fixed views in E1M1** (`tools/bench_modes.sh`: the spawn view, then
walking down the first hall into the first room; 2 monsters at once):

| Setting | Spawn view | Walking | PicoDrive spawn view | PicoDrive walking |
|---|---|---|---|---|
| Textured + lit (default) | 11.8 | 7.8 | 17.0 | 19.0 |
| Textured, lighting off | 11.8 | 9.3 | 15.7 | 20.6 |
| Flat shaded + lit | 16.4 | 16.7 | 26.0 | 33.0 |
| Flat shaded, lighting off | 16.5 | 17.1 | 25.0 | 33.4 |
| Textured, draw distance 512 | 18.0 | 9.5 | 18.7 | 19.0 |
| Flat, draw distance 512 | 26.3 | 18.4 | 29.5 | 32.4 |

`tools/bench_modes.sh`, then `tools/bench_pico.sh`, reproduce this table.

### Earlier measurements

Moving the text, the border and the view weapon to the Mega Drive, and the
cold code to ROM (which gave the surface cache about 9 KB more per CPU), sped
up these fixed views in the cycle model (fps, no monsters, full screen):

| View | Textured, before | Textured, after | Flat, before | Flat, after |
|---|---|---|---|---|
| Spawn point | 9.0 | 10.8 | 16.6 | 17.3 |
| Lift room | 16.8 | 18.0 | 22.9 | 25.1 |
| Bottom of the lift | 7.5 | 7.4 | 14.5 | 15.3 |

The gains come from the 32X no longer drawing the weapon model and the text,
and in textured mode from the bigger surface cache. The cutout behind the
weapon adds about 0.5%, since the shotgun covers only about 1.5% of the view.

The tables below are from before monsters and the other game systems were
added (they cost about 12% even with no monsters).

Over the whole walkthrough route, before monsters:

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

## How it works

The first step is offline conversion. `tools/mkmap.py` turns each level's
BSP into a big-endian, fixed-point blob that stays in cartridge ROM, and
`tools/mkrom.py` puts the program and the three blobs together into the
cartridge, each blob on a page boundary, with the directory the game reads
them from. A blob holds:

* nodes (32 bytes each), with the splitting plane stored inside the node and
  faces sorted front-facing first;
* a compact 16-byte geometry record per face, holding vertex range, flags,
  flat colours and a bounding sphere;
* 13.3 fixed-point vertices;
* 8-bit lightmaps already inverted into the colormap's light scale;
* the PVS;
* the clip hull used for collision;
* the palette, colormap, font and sky texture.

Doors, secret doors, platforms, buttons and trains (`tools/movers.py`)
follow their paths from id's QuakeC (doors.qc, plats.qc, buttons.qc,
misc.qc). Each one's faces are
clipped into the world's BSP leaves at keyframes every 16 units along its
path; the renderer draws the keyframe nearest the mover's position, shifted
by the few units left over. Movers are solid in every clip hull, carry what
stands on them, and crush or reverse when blocked. Key doors open for the
player with the key, and unlock every linked door that needs the same key.
Triggers, counters, teleporters, secrets, spike traps, messages and the
level exits work as in QuakeC.

`tools/mkents.py` adds the game data to the same blob: an 18-byte record per
entity the game knows (monsters, items, keys, traps, the exits, ambient
sounds, torches and flames), the models, Quake's 162 vertex normals, and the
sounds as 8-bit 11 kHz samples. Each level gets only the models and sounds
its entities (and, in E1M3, the demo) need, under ids shared by all levels
(`src/g_ids.h`), so the game's tables are the same in every level. A model keeps its
vertex frames (4 bytes per vertex), its triangles, one colour per triangle
per skin (the average of the skin texels it covers) and a bounding box per
frame. Item boxes (`maps/b_*.bsp`) become one-frame models.

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
     width) and offsets the framebuffer pointer. The border is the Mega
     Drive's (below).
5. **Vertical blank.** Both CPUs begin the next frame's walk and geometry
   while the hardware is still waiting for vertical blank, and block only at
   their first framebuffer write.

The game (`src/g_*.c`, `src/r_model.c`, `src/snd.c`):

* **Game logic** runs on the master at 60 Hz, right after it draws its own
  rows and while the slave is still drawing, so the row split hands the
  slave more of the picture. Monsters follow id's QuakeC (soldier.qc,
  dog.qc, ogre.qc, knight.qc, demon.qc, wizard.qc, zombie.qc, ai.qc,
  fight.qc) and Quake's step-and-chase movement against the clip hulls,
  thinking ten times a second. Each monster is a table of its animations:
  the frames, the distance it moves on each, and the events (a shot, a
  bite, a grenade, a leap, a sound) on the frames QuakeC has them. Scrags
  fly; fiends leap; zombies throw their flesh, fall when hit hard and get
  up again unless gibbed. Weapons, missiles, explosions, gibs and radius
  damage follow weapons.qc (`src/g_weapon.c`), with Quake's vertical
  auto-aim. The cold code (`g_main`, `g_monst`, `g_mover`, `g_weapon`, the
  demo player, the bot, the menu, the Mega Drive link) runs from cartridge
  ROM; collision stays in SDRAM.
* **Colour flashes.** Quake's `V_UpdatePalette`: damage, pickups, Quad
  Damage, the Biosuit, the Ring and being under water, slime or lava tint
  the palette. The master builds the shifted palette and its vertical-blank
  interrupt writes it to the 32X's palette RAM, which can only be written
  during blanking.
* **Depth only where it is needed.** There is no z-buffer. Each frame the
  master reserves a per-row interval of depth around every model on screen,
  up to 6,144 samples. World spans write 1/z there as they are drawn, and
  models and particles test against it. A model too close to fit is drawn
  back to front without it.
* **Models** are flat shaded in both texture modes, one colour per triangle,
  lit the way Quake lights models. Each triangle takes the depth of its
  nearest vertex; that is close enough at this resolution.
* **Sound.** The slave's DMA channel 1 feeds the PWM: when a buffer of 256
  stereo samples has played, the DMA interrupt restarts it on the other
  buffer and mixes the next one (6 channels of Quake's 11 kHz samples, each
  doubled to 22 kHz, plus 2 looping channels for the ambient sounds:
  torches, machinery, water). The master queues new sounds; Quake's distance
  fade and stereo panning are worked out when a sound starts, and the two
  loudest ambient sounds near the player are picked as it moves.

The Mega Drive layer (`src-md/md.s`, `src/mdlink.c`, `tools/mkmd.py`):

* The 32X mixes its picture with the Mega Drive's. With the priority bit
  left at 0, the Mega Drive's pixels are in front wherever it draws
  something; where it shows only its backdrop colour, the 32X picture shows.
* **Plane A** holds the text: the status bar, messages, the FPS counter, the
  hint, the menu and the end-of-level figures, in Quake's 8x8 console font.
  **Plane B** holds the border tile around a reduced view, the black status
  bar, and a dither that darkens the screen behind the menu. The view sizes
  are on the Mega Drive's 8-pixel grid, so the border is made of whole tiles.
* **The view weapon** is a set of sprites. `tools/mkmd.py` renders the
  axe, shotgun, super shotgun, nailgun and grenade launcher frames at every
  screen size the way Quake's alias renderer draws them: clipped 5 units in
  front of the eye, back faces culled, textured and lit as Quake lights
  models (at Quake's brightest light, then 1.3 times brighter, since the
  Mega Drive's 3 bits per channel lose dark detail), at full 320-pixel
  resolution and anti-aliased. The weapon has one fixed look (it does not
  darken with the room). Each channel is then ordered-dithered to the Mega
  Drive's 8 levels with a light 4x4 Bayer pattern, the same threshold for
  all three, so greys stay grey and browns stay brown. Each set (a weapon
  at a screen size) has three 15-colour palettes of the colours its frames
  use most, and each sprite (up to 4x4 tiles) uses whichever suits it. The
  68000 copies the current set's frames into VRAM and its palettes into
  lines 1 to 3 when the weapon or screen size changes (up to 320 tiles),
  and places the sprites each frame. The font and the border share
  palette line 0: on the 3-bit grid they need only 11 colours between
  them.
* **The cutout.** For every weapon frame, `mkmd.py` also stores the 32X
  pixels the sprite covers completely, row by row. The renderer starts each
  frame with those marked as covered in its coverage lists, so nothing
  behind the weapon is ever drawn. The weapon model itself is no longer
  drawn by the SH-2s at all.
* **The pad.** The 68000 reads an ordinary pad in each vertical blank and,
  when none answers, tries the XE-1 AP: it pulls REQ low and stores the 12
  nibbles of the report (20 with the extension) as they come, 10 us each,
  and checks their L/H order afterwards, since checking between nibbles is
  too slow to catch the next one. The buttons go to COMM12 as usual (bit 13
  marks the analogue pad, bits 14 and 15 are its triggers); the axes take
  turns in COMM2, three words of two 7-bit values each.
* **The link.** The SH-2s cannot reach the Mega Drive VDP, so the master
  queues 16-bit words for the 68000: tiles for text cells (only the cells
  that changed since the last frame), the weapon's frame and light, "load
  this weapon" and "map these cartridge pages". It hands them over one at a time through COMM0, with
  a number in COMM10 that the 68000 answers in COMM14, and feeds the queue
  between render batches. The 68000 runs its loop from work RAM, off the
  cartridge bus. It applies the weapon at the next vertical blank, with the
  32X's frame flip.

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
make            # -> quake32x.32x (10 MB), the compatible build
make FAST=1     # stacks and span loop in SH-2 on-chip RAM (two-way cache mode)
```

`LEVELS` in the Makefile lists the levels in the order of play, and
`DEMO_<level>` names a demo to put in a level (`DEMO_e1m3 = --demo
demo1.dem`). Each level is built on its own (`build/e1m1.lvl`, ...), and
converting all three takes about 40 seconds.

The default build uses the normal 4-way cache and keeps everything in SDRAM.
That is within a few percent of the `FAST=1` build and works on emulators
that don't implement the SH-2 cache-as-RAM mode.

E1M1-E1M3 and `demo1` are part of the Quake shareware episode, so the ROM
can be shared.

## Walkthrough build

```
make DEMO=1     # plays a scripted walkthrough of E1M1 instead of reading the pad
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

## The demo bot

Pause menu, **Demo (bot plays)**: a bot plays E1M1 from the start to the
exit, through the pad like a person would (the D-pad to turn and walk, C to
fire). It takes the lift down, crosses the courtyard, presses the button
that slides the bridge out over the slime and looks at the slime while it
comes, crosses, presses the three buttons that open the door to the lower
level, and walks out through the exit. Its route comes from
`tools/build_botroute.py`: the legs in `data/e1m1_botlegs.json` are planned
with `tools/plan_route.py` (with closed doors solid, and the lift and bridge
as floors where they stand when used), centred in corridors by
`tools/refine_route.py`, and joined with the actions in between (touch a
button, wait for a mover, look at something). The result is
`data/e1m1_botroute.json`.

It stops to shoot anything in sight within shotgun range and fires only when
Quake's vertical auto-aim would hit. It picks up backpacks, and shells or
health when low, but only along a straight line with floor all the way, and
walks back to where it left the route. Its only help is taking a quarter of
the damage.

```
tools/record_bot.sh OUT [seconds] [menu keys]   # record it in the cycle model
```

`record_bot.sh` builds the normal ROM, sets the menu with scripted
presses over the attract demo (by default: flat shading, see-through water,
80% screen, then Demo), and records every frame, the sound and a per-frame
log. `tools/hostgame.sh` builds the same game for a PC (with sanitizers) for
quick runs of any level: `LOG=1` prints the bot and player each frame,
`DOORLOG=1` the movers, `PLAYDEMO=1` plays the level's demo, `NOBOT=1` with
`PADSCRIPT=file` plays scripted pad input, and `GIVE`, `GOD`, `KEYS` and
`START` give weapons, less damage, keys and a starting point.

## Tools

* `tools/mkmap.py`: BSP to level blob converter (`--le` writes a
  little-endian copy for the host build, `--demo` adds a demo).
* `tools/mkrom.py`: the cartridge: the program, the level directory and the
  levels, each from a page boundary, and the header checksum.
* `tools/dem.py`: a reader for Quake's demo files (network protocol 15);
  run on its own it summarises one.
* `tools/mkdemo.py`: a Quake demo to the stream the 32X plays back.
* `tools/sh2sim.c`: the dual SH-2 and 32X bus timing model, with
  per-function profiles (`-prof`, including code run from ROM), hottest
  instructions (`-hot N`), scripted pad input, screenshots, video capture
  (`-video`), PWM sound with the SH-2 DMA and its interrupt (`-audio`), and
  memory watches in the per-vblank log (`-watch symbol n`). Built by
  `tools/build_sim.sh` with `tools/mdsim.h`, it also runs the Mega Drive
  side: the ROM's 68000 program (on the Musashi 68000 core, taken from a
  PicoDrive source tree, `MUSASHI=...`), the joypad port and the VDP's
  planes and sprites, mixed over the 32X picture as the 32X does it.
  `-xe1ap` (or `-xe1ap-noext`) puts an XE-1 AP in port 1, following
  32X-HD's description of its timing, and `-analog FROM TO lx ly rx ry lt
  rt` sets its sticks and triggers between two vblanks.
* `tools/mkmd.py`: the Mega Drive tiles, palettes, pre-rendered weapon
  sprites and their cutouts (`build/md_data.bin`, at ROM offset 0x4000);
  a third argument writes a preview image of every weapon set.
* `tools/movers.py`: doors, platforms, buttons and triggers worked out from
  the entity lump, shared by `mkmap.py` and `mkents.py`.
* `tools/mkents.py`: entities, models and sounds for the level blob;
  `--header src/g_ids.h` writes the matching ids.
* `tools/plan_route.py` and `tools/make_demo.py`: the route planner and the
  demo script generator.
* `tools/bench_route.sh`, `tools/bench_view.sh` and `tools/bench_modes.sh`:
  real-hardware estimates averaged over the walkthrough route, from one fixed
  view (built with `-DSTART_POS=x,y,z,yaw,pitch`), or at the spawn point and
  down the first hall.
* `tools/harness.c`: a headless libretro front end for PicoDrive, used for
  screenshots, scripted input and sound capture (`AUDIO=file.raw`).
* `host/`: the same renderer built for a PC, with sanitizers and per-frame
  statistics (`tools/hosttest.sh`).

## Not done yet / ideas

* The rest of Episode 1 (E1M4-E1M8). The cartridge was kept to the first
  three levels; each of the others would need another 2.5-3.5 MB.
* Speed in the bigger levels. E1M3 is much heavier than E1M1: the attract
  demo averages 6.9 fps textured at full screen in the cycle model (4 to 13
  fps), against 14.9 fps over the E1M1 route. Flat shading or a smaller
  screen helps a lot. Simpler level geometry (fewer, larger faces) was
  discussed but has not been tried.
* Fall damage, the player's death animation, textured monster skins.
* Simpler monster models (about half the triangles) would make fights
  faster.
* `FAST=1` gives each CPU a 2 KB on-chip stack, which is too small for the
  game's collision code; it hasn't been tried with the game.
* Assembly versions of geometry stages B and C, which are the biggest
  remaining cost.
* Hardware verification. Everything has been checked in PicoDrive (dynarec
  and interpreter) and the cycle model, but not on a real 32X, nor with a
  real cartridge that has the mapper.

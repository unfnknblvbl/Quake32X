# Quake 32X: technical notes

A from-scratch Quake engine for a **stock 32X** (two 23 MHz SH-2s, 256 KB
SDRAM), running the first three levels of Quake's shareware episode, E1M1
(the Slipgate Complex), E1M2 (Castle of the Damned) and E1M3 (the
Necropolis), converted from the shareware `pak0.pak`. The usual build puts
them in a 9 MB cartridge with Sega's standard ROM mapper; `build.sh` can
instead fit chosen maps in 4 MB with no mapper (below). It is a proof of
concept, not tested on real hardware: the README's **Status** lists what
the game does not do yet (Shamblers, Chthon, the rocket launcher, super
nailgun and Pentagram, and some of the later maps' scripting).

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
the heads of gibbed monsters) teleport away in the same burst, silently, a
second after they come to rest, so there is less to draw and think about. **Demo
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

**Cartridges of 4 MB or less** (0.7.0; what `build.sh` makes from the maps
you choose) need no mapper. The SH-2s see the whole cartridge, so the
converter packs the sound bank and the levels after the program page at
64 KB steps, sets bit 0 of the level directory's flags word, and writes the
directory's page numbers and counts in 64 KB steps. The game then reads
each level where it lies (0x02000000 + 64 KB x its first step) and never
asks the 68000 to write the mapper registers. The cartridge is padded to 1,
2 or 4 MB. `build.sh` lists every map in the pak with its size alone (its
sounds included, so a little more than it takes beside maps that share
sounds), and keeps asking until the choice fits in the 3.5 MB after the
program page. Without textures a level loses its textures and light maps
(`flat_only`); on the shareware maps that saves 10-25%.

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

**0.6.0 against 0.5.0** (same runs as below):

| Run | 0.5.0 | 0.6.0 | |
|---|---|---|---|
| E1M1 route, textured | 14.96 | 15.86 | +6.0% |
| E1M1 route, flat | 26.90 | 27.92 | +3.8% |
| E1M1 route, flat, 70% screen | 28.33 | 29.05 | +2.5% |
| E1M3 demo, textured | 6.72 | 7.25 | +7.9% |
| E1M3 demo, flat | 13.81 | 16.45 | +19.1% |

The E1M3 demo gains most because it has the most per-frame work outside
the renderer (monsters, the demo's entities) that now overlaps the second
CPU's drawing; the E1M1 route is a camera path with no game at all. The
figures move by a percent or so with small changes anywhere in the
program (cache and balance effects), so differences under 2% are noise.

**0.5.0 against 0.4.1** (cycle model, full screen unless noted; the E1M1
route with no monsters, the E1M3 attract demo averaged over its first loop):

| Run | 0.4.1 | 0.5.0 | |
|---|---|---|---|
| E1M1 route, textured | 15.04 | 14.96 | -0.5% |
| E1M1 route, flat | 25.91 | 26.90 | +3.8% |
| E1M1 route, flat, 70% screen | 27.58 | 28.33 | +2.7% |
| E1M3 demo, textured | 6.46 | 6.72 | +4.0% |
| E1M3 demo, flat | 12.90 | 13.81 | +7.1% |

The figures below were measured on earlier versions.

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

1. **Screen split.** The two SH-2s each render a horizontal band of the
   screen. The split row moves each frame to balance the load: each CPU counts
   how long it waits for the other, and the split shifts by that amount
   (about 20,000 waiting loops make one row, at most 16 rows a frame).
2. **The next frame, prepared while the slave draws** (0.6.0). Once the
   master has drawn its band it runs the game ticks, draws the Mega Drive
   overlays, and prepares the whole of the next frame: the view, the PVS
   when the camera changes leaf, the movers' state, the models and
   particles to draw, and the BSP walk. All of this goes into a second copy
   (`view_next`, `scene_next`, the spare candidate list) while the slave
   still draws from the first, and `commit_frame` makes it current between
   frames, when both CPUs are idle. The balance hands the slave more rows
   for it. Before 0.6.0 this was done after both CPUs had finished, with the
   slave waiting: about 15 ms of a 68 ms frame in the E1M3 demo, now about
   6 ms (what is left is mostly the frame-to-frame change in the work,
   which the balance can only follow a frame late).
3. **Batched pipeline.** On each CPU, every stage runs over a batch of 32
   faces. That keeps each stage's code inside the 2 KB cache instead of
   refetching about 5 KB of code for every face. The stages are:
   * **A: BSP walk**, once a frame for the whole window (0.6.0; until then
     each CPU walked the tree for its own band). Front-to-back, with an
     explicit stack, PVS tests and frustum culling of node bounding boxes
     (with clip flags). It collects the candidate faces, up to 1,024 (the
     three levels peak at about 650), which both CPUs then take in order.
   * **B: geometry** (SH-2 assembly, `src/geom_asm.S`, from 0.6.0). A
     bounding-sphere test against the frustum (each CPU adds its own band's
     top and bottom planes), then vertex transforms through a per-frame
     vertex cache, with the multiply-accumulate unit reading the vertex and
     the view matrix from memory. The divide for projection runs on the
     hardware divider while the outcodes are computed. A face that crosses
     a plane it must be clipped against (about half of those that get this
     far) is clipped in C, in view space, against only those planes; the
     vertices that stay keep the projection they already have.
   * **C: rasterization** (SH-2 assembly, `src/raster_asm.S`, from 0.6.0).
     Walks each polygon's left and right edges into per-row coverage gap
     lists. Nothing is ever overdrawn, rows that are already covered are
     skipped, and the CPU stops once its band is full.
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
  slave more of the picture. (The particles are the one thing the slave
  reads straight from the game; a particle can be drawn one tick late.) Monsters follow id's QuakeC (soldier.qc,
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
  nearest vertex; that is close enough at this resolution. Their vertex
  transform, lighting and projection are SH-2 assembly
  (`src/model_asm.S`, from 0.6.0).
* **Sound.** The slave's DMA channel 1 feeds the PWM: when a buffer of 256
  stereo samples has played, the DMA interrupt restarts it on the other
  buffer and mixes the next one (6 channels of Quake's 11 kHz samples, each
  doubled to 22 kHz, plus 2 looping channels for the ambient sounds:
  torches, machinery, water). The master queues new sounds; Quake's distance
  fade and stereo panning are worked out when a sound starts, and the two
  loudest ambient sounds near the player are picked as it moves.
  The mixer (0.5.0) cuts each buffer at the points where channels start or
  stop and mixes each stretch with only the channels playing in it: one to
  three channels go straight to the output word, more are summed in pairs
  into the output buffer itself (through its cached address) and packed at
  the end. That halved its cost, from 18% of the slave's time to 10%.
* **Flat mode's merged faces** (0.5.0). `mkmap.py` merges neighbouring faces
  of a BSP node side when they have the same texture, their light levels are
  within 4 of the colormap's 64 rows (`FLAT_TOL`), they share an edge exactly
  and the result is convex with at most 16 vertices. Sky, liquids, mover
  faces and faces marked to keep their texture are never merged. The merged
  face takes the area-weighted light level. Three lumps (`flatof`,
  `flatnode`, `flatlist`) give each node its list of flat-mode faces, and the
  BSP walk uses them when textures are off. E1M1 goes from 5,059 faces to
  4,697 this way, E1M2 from 4,892 to 4,556 and E1M3 from 4,419 to 4,068.
* **Textures kept in flat mode** (0.5.0). Faces whose texture name matches
  `KEEP_TEXTURE` in `mkmap.py` (buttons, switches, shootable switches, key
  door markings, the slipgate and the
  exit signs) carry flag 64 and are drawn textured even with textures off.

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

* **Assembly checked against C.** Each assembly stage has its C version
  (built for the host, and on the 32X with `-DC_RASTER`, `-DC_GEOM` or
  `-DC_MODEL`). `tools/check_asm.sh` builds both into the attract demo with
  test switches that make the frames independent of speed (`FIXED_TICKS`,
  `SERIAL_FRAME`, `FIXED_SPLIT`; `TEST_CFG` sets the menu settings) and
  compares every distinct frame of the 32X picture (`sh2sim -video32x`).
  They are the same bit for bit.
* **Arithmetic right shifts.** The SH-2 shifts one bit at a time, and GCC
  calls a library routine for `x >> 7`. The mixer gets it from the high word
  of a multiply by 2^25 instead.
* **Two-way cache mode** (`FAST=1` only). Each SH-2's stack and the span
  loop live in its 2 KB of on-chip RAM (0xC0000000). Register spills then
  avoid the write-through SDRAM bus, and the hot loop never misses the cache.
* **No variable shifts.** The SH-2 has no barrel shifter, and GCC turns
  `x >> n` into library calls. Mip levels and bit tests are written to avoid
  them.
* **Compiler flags per file.** Geometry code is compiled with `-Os`, since it
  is bound by instruction-cache misses. Span and surface code uses `-O2`.
  (`-O2` for the geometry was tried again in 0.6.0: 1-2% faster flat
  shaded, 3% slower textured, as the bigger code takes surface cache.)
* **SDRAM is the surface cache.** Whatever the program and its data do not
  use is shared between the two surface caches, and textured mode is very
  sensitive to it: 4 KB less for each CPU costs about 8% over the E1M1
  route. 0.6.0 needs about 2.5 KB more data (the second view, scene and
  candidate list) and took it back from the master's stack, which is now
  4 KB (it uses up to about 2.4 KB).

## Level extensions (0.7.0)

The editor's changes to a level that the engine has to know about go in an
extension directory. The level header's i32 at offset 296 points to it (0:
none, as every level before 0.7.0), and the i32 at 300 holds flags
(`LF_FLAT_ONLY` = 1: no textures or light maps). The directory is `'Q32X'`,
a count, then an (offset, count) pair per lump, in this order:

| Lump | Entries | What |
|---|---|---|
| FLATREF | u16 first, u8 count, u8 pad (per face) | the face's flat-mode pieces (light to polygons) |
| FLATMARK | u16 | the pieces, as the leaves' face lists mark faces |
| LIGHTGRID | i16 origin[3], u16 step, u16 dims[3], u16 pad, then u8 light values | model lighting, trilinear |
| LEAFAREA | u8 per leaf | 0 always drawn, 1-127 an area, 128+k the doorway of door k |
| DOORS | u8 mover, u8 area count, u8 areas[4], u16 pad | doors that divide areas |
| LEAFFAR | u16 per leaf | draw distance in world units (0: the menu's) |
| MVMODELS | u32 per mover | movers drawn as flat-coloured models |
| MDLLOD | u32 offset, u16 distance, u16 pad (per model id) | a model's distance version |
| FLATPAL | u16 cram[256], u8 remap[256], u8 cmap[64 x 256] (one) | flat mode's own palette |

* **Door areas.** The converter makes portals from the world BSP, finds the
  leaves each door fills (at least half inside it), joins linked doors, and
  floods the leaves into areas with every door closed. A door divides 2-4
  areas. While the view is in an area, the engine floods through the doors
  that are open (a mover away from its start) and leaves out what it cannot
  reach; leaves are marked again when a door opens or closes.
* **Light to polygons.** Each face's light map is cut into 2-6 light levels
  (one-dimensional k-means over the level), small regions are merged into
  their neighbours, and each region becomes polygons: the light map's cell
  triangles cut along the band edges, clipped to the face, merged per band.
  The pieces are flat-mode faces only (FLATREF/FLATMARK); textured mode
  draws the face as before. A budget keeps the level under the face limit,
  and a T-junction pass adds piece corners to neighbouring flat polygons so
  no cracks show.
* **Flat mode's own palette.** With FLATPAL, the faces' flat colours (lit and
  unlit) are indices into the user's palette, not Quake's. When textures go
  off, `r_commit` copies the lump's `cmap` over the colormap (Quake colour
  and light row to the user's colour: monsters, items, the textures kept in
  flat mode), points `r_palette` at its `cram` (the colour shifts are made
  from it, and the vertical blank writes it) and `r_pmap` at its `remap`
  (particles), and flushes the surface caches; textures on puts Quake's
  back. Only the master changes these, between frames. The converter picks
  each texture's colour as the nearest (in OKLab) to its average, brightened
  with the level's others to an average of about two thirds, saturated
  (vividness) and optionally spread round the colour wheel by hue rank,
  weighted by the square root of the area it covers (variety). Shading
  replaces the light maps for flat mode only: at each light-map sample, 32
  rays in a cosine-weighted hemisphere through the world BSP and the solid
  brush models give an openness (a hit at a fraction f of the reach counts
  f; sky counts open), each ray weighted by 1 + 0.6 z so light comes from
  above; Quake's light formula with shadow rays adds the light entities;
  a 3x3 blur, a soft roll-off to the full colour, then the light-to-polygon
  bands (4 by default) split the faces. The colours a row darker are the
  nearest of the palette to the colour scaled by (64 - row) / 32. E1M3's
  4,244 faces take about 1.3 s on two cores. The colour maths uses only
  +, -, *, / and square roots (the cube roots by Newton's method), so every
  machine builds the same bytes.
* **Smaller sounds.** A sound's format is in the top four bits of its
  table length (the length stays in 11 kHz samples, as played): 0 as
  before, 1 half rate (every other sample, after a 1-2-1 low-pass), 2
  packed in 4 bits, 3 both. Packed blocks are 256 samples in 145 bytes:
  the value before the first, 16 signed steps, and a nibble per sample
  naming its step; the encoder fits the steps to the block (k-means over
  the steps it needs, then again over those its encoding met) and picks
  each sample's step by where it lands, keeping every value in 0..255, so
  decoding is a table look-up and an add. The mixer (`src/snd_dec.h`,
  shared with the converter's tests) decodes a packed channel's samples a
  byte (two samples) at a time into the channel's own buffer; a half-rate
  channel is read where it lies, sample (offset + i) / 2 for output i, in
  its own loops (`mix_direct_half` for up to three, all half-rate;
  `mix_half` when kinds mix), so half rate costs no decoding pass. A loop's
  start or a jump decodes from the start of its block. Over E1M3's attract
  demo: full 12.4 fps, half rate 12.1, packed 12.1, both 11.8 (cycle
  model). IMA ADPCM, tried first, took the demo from 11.3 to 6.3 fps.
* **Reduced textures.** A texture's flags byte has the finest mip level
  drawn in bits 0-1 and, in bit 2, 4 bits a texel: 16 palette bytes, then
  two texels a byte (the first in the high nibble), up to 256 texels a mip
  level. The surface cache unpacks them.
* **Models.** `MF_FRAMEMAP` (0x2000) marks a model with a frame map (after
  its bounding boxes, one entry per Quake frame naming the stored frame), so
  animations can keep every nth frame.
* **The sound bank.** A sound whose offset has bit 31 set is in the shared
  bank. The level directory in `crt0.s` gains the bank's first page and page
  count; the bank goes at the start of the levels and maps into the last
  pages of the 7-page window (the 68000's bank command `0xD000 | n << 6 |
  page`), so each level has 7 - n pages.
* **Simpler collision.** The world's clip hulls (1 and 2) are cut into
  their convex cells (each a polytope, clipped down the original tree from
  the world's bounds), and a new tree is built over the cells: at each node
  the plane (from those bounding the cells) that splits fewest cells and
  halves the open space, where the game's points are; a side whose cells
  are all of one kind becomes a leaf. The solid space is the same planes'
  (110,592 sample points a hull classify the same). In open space a point
  test fell from 15.2 to 11.9 planes (E1M1 hull 1) and from 22.3 to 11.3
  (E1M2 hull 2, which had a 65-deep branch); the E1M1 bot plays the level
  exactly as before, `contents_from` dropped from 4.5% to 3.9% of the first
  CPU, and the run was 2.5% faster.
* **Flat-shaded weapons.** With `flat_weapons`, md_data.bin holds a second
  group of weapon sets after the first (the set count doubles): each
  triangle filled with the average of the skin under it and lit once, as the
  game draws models in flat mode. The game picks them while textures are
  off (`md_gun_flat` in src/mdlink.c). The data must stay within 320 KB (the
  68000 sees the program's first 512 KB, from 0x30000): the default build
  packs the tiles and lets the 90% screen size use the 80% render (276 KB).
* **The Mega Drive data** is at ROM offset 0x30000 (68000 address
  0x8B0000, section `.mddata`, last in the program), so a project's own can
  replace it without relinking. Weapon tiles can be packed: bit 31 of a
  set's tile offset, runs and back references up to 4096 bytes, unpacked
  by the 68000 through a 4 KB ring in work RAM.

## The speed estimate

The editor's speed check runs the level in its copy of the engine and adds
up, every tick, the renderer's own counts (spans, pixels, face set-ups,
models and their screen area, texels built into surfaces). The frame time
is a weighted sum of them, plus the surface building spread over each
second; the weights were fitted (non-negative least squares) to the cycle
model's frame rate, second by second, over the E1M3 attract demo in seven
setups: textured and flat, reduced textures, see-through water, plain
colours with light-polygon shadows, with and without movers drawn as models.
Over whole demos the estimate came within 0-8% (textured with Quake's
textures, the worst, reads 8% high); second by second it is within about
15% on average. The weights are in `editor/q32x/ui/src/Speed.h`.

## Building

`./build.sh` (in the top folder) does everything on CachyOS / Arch Linux; see
the README. By hand you need:

* an `sh-elf` and `m68k-elf` GNU toolchain (`TC=` in the Makefile, default
  `/opt/toolchains/sega/bin`); the SH-2 compiler is GCC 13 configured with
  `--with-cpu=m2 --with-multilib-list=m2 --with-endian=big`;
* CMake and a C++20 compiler, for the converters (`editor/q32x`, built
  into `build/tools` by the Makefile);
* Quake's `pak0.pak` copied to `data/pak0.pak`.

```
make            # -> quake32x.32x (10 MB), the compatible build
make FAST=1     # stacks and span loop in SH-2 on-chip RAM (two-way cache mode)
make program    # editor/q32x/program/q32x-program.bin, the program the editor ships
make PLAIN=1    # the levels as converted, without the project's optimisations
```

The cartridge is the project `data/quake32x.q32x` exported by the editor's
converter (levels, Mega Drive data, sound bank, cartridge in `build/export`).

The version is in `VERSION`; it goes into the menu (top left) and the
cartridge header's memo field.

`LEVELS` in the Makefile lists the levels in the order of play, and
`DEMO_<level>` names a demo to put in a level (`DEMO_e1m3 = --demo
demo1.dem`). Each level is built on its own (`build/e1m1.lvl`, ...), and
converting all three takes about 40 seconds.

The default build uses the normal 4-way cache and keeps everything in SDRAM.
That is within a few percent of the `FAST=1` build and works on emulators
that don't implement the SH-2 cache-as-RAM mode.

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

The conversions are C++ since 0.7.0 (`editor/q32x`, the program
`q32x-convert`; the editor uses the same library). The Python tools below
are the originals they were ported from: with no editor changes the C++
versions write the same bytes (checked level by level, and for the Mega
Drive data and the cartridge). The Python demo tools (`make_demo.py`,
`plan_route.py` and the route scripts) are still Python.


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
  (`-video`; `-video32x` writes the 32X framebuffer's colour indices alone,
  for comparisons), PWM sound with the SH-2 DMA and its interrupt (`-audio`), and
  memory watches in the per-vblank log (`-watch symbol n`). Built by
  `tools/build_sim.sh` with `tools/mdsim.h`, it also runs the Mega Drive
  side: the ROM's 68000 program (on the Musashi 68000 core, taken from a
  PicoDrive source tree, `MUSASHI=...`), the joypad port and the VDP's
  planes and sprites, mixed over the 32X picture as the 32X does it.
  `-xe1ap` (or `-xe1ap-noext`) puts an XE-1 AP in port 1, following
  32X-HD's description of its timing, and `-analog FROM TO lx ly rx ry lt
  rt` sets its sticks and triggers between two vblanks.
* `tools/mkmd.py`: the Mega Drive tiles, palettes, pre-rendered weapon
  sprites and their cutouts (`build/md_data.bin`, at ROM offset 0x30000 since 0.7.0);
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
* `tools/check_asm.sh`: the assembly stages against their C versions, frame
  by frame (How it works, Other tricks).
* `src/asmoff.c`: structure offsets for the assembly files, turned into
  `build/asmoff.h` by the Makefile.
* `tools/harness.c`: a headless libretro front end for PicoDrive, used for
  screenshots, scripted input and sound capture (`AUDIO=file.raw`).
* `host/`: the same renderer built for a PC, with sanitizers and per-frame
  statistics (`tools/hosttest.sh`).

## Not done yet / ideas

* The rest of Episode 1 (E1M4-E1M8). The cartridge was kept to the first
  three levels; each of the others would need another 2.5-3.5 MB.
* Speed in the bigger levels. E1M3 is much heavier than E1M1 (see
  Performance). Flat shading or a smaller screen helps a lot, and so do the
  editor's tools (reduced textures: 12.4 fps textured against 7.1 in the
  E1M3 attract demo).
* Fall damage, the player's death animation, textured monster skins.
* Simpler monster models (about half the triangles) would make fights
  faster.
* `FAST=1` gives each CPU a 2 KB on-chip stack, which is too small for the
  game's collision code; it hasn't been tried with the game.
* What was tried in 0.6.0 and left out: testing each face's bounding sphere
  (and the BSP walk's subtrees) against what is already drawn before its
  vertices are transformed. About 40% of the faces that reach the
  rasterizer are completely hidden, but only half of those can be caught a
  batch early, and the test cost more than it saved (1% slower). A layout
  of the level data in the order it is read was estimated at about 1%,
  since the faces and nodes already sit in walk order (skipping the leaves
  without movers' fragments without reading them made no measurable
  difference). The rest of the time in stages B and C is mostly
  waiting for cartridge ROM and SDRAM writes, which assembly does not
  change: the three assembly stages together gave about 5%.
* Hardware verification. Everything has been checked in PicoDrive (dynarec
  and interpreter) and the cycle model, but not on a real 32X, nor with a
  real cartridge that has the mapper.

# Changelog

Versions are 0.x.y until the first public release, 1.0.0. The minor number
goes up for new features, the last number for fixes. Versions before 0.5.0
were tagged afterwards, on the commits listed.

## 0.8.0 - 2026-10-31

- **Cartridges are 4 MB at most for now** (no mapper). The converter and
  `build.sh` refuse more, saying by how much.
- **One shared library per cartridge:** every sound, texture, model and
  common table is stored once however many levels use it (START and E1M1:
  about 380 KB less). The game reads it through the offsets it always used.
- **Half-size textures** (a quarter of the texels) and **plain liquids,
  skies and stained glass** with textures on too (no texels for them). With
  both, START and E1M1 fit in about 3.9 MB with textures *and* the grey flat
  mode, switched from the pause menu.
- **`build.sh`:** asks full, half or no textures, then plain liquids, skies
  and glass; measures the chosen maps together (`q32x-convert size`: what
  each adds, the running total) and asks again until they fit. The usual
  cartridge is now START and E1M1 (half-size textures, greys, packed
  sounds), since E1M1-E1M3 no longer fit. Stained glass is yellow in a
  flat palette.
- The cartridge header names its levels (e.g. "QUAKE 32X - START E1M1").
- ANALOGUE.md: the analogue controls, and the XE-1 AP protocol (with
  32X-HD's extension) the game reads.
- Fixed: closed split doors showed their hidden edges through themselves
  (faces pressed against another solid face are left out).
- Fixed: messages longer than the status line (START's episode signs)
  overran a buffer and crashed the game; long messages now show a line at
  a time, and maps' `\n` line breaks are real ones.
- Fixed: only one of a level's exits worked (START has five: all its
  slipgates now leave the level).
- Fixed: START's bossgate floor was not solid; func_wall and func_bossgate
  models are now solid to the player and monsters (unless the level's clip
  hulls would grow too big: E1M6 keeps two apart).
- Fixed: teleporters with no destination stopped START from converting.

## 0.7.0 - 2026-10-26

The level editor, and the game support for what it does to levels. Levels
built without the editor's changes play exactly as in 0.6.0.

- **The editor** (`editor/`): TrenchBroom with a Quake 32X module. A
  project (`.q32x`) holds the cartridge's levels and everything the editor
  changes about them, and refers to your own `pak0.pak` without copying it.
  The project window (what `quake32x-editor` opens, or `--32x
  project.q32x`) has a map view with overlays (door areas, a heat map of
  faces in view, light bands, edits), a face inspector (colours set by hand,
  locks, removal), a **32X view** drawn by the game's own renderer and Mega
  Drive layer, a texture editor (the 16-colour reduced texture as a layer
  over the original), sound, model and Mega Drive graphics editors, budgets,
  undo, and cartridge export. `editor/build.sh` builds it for Linux,
  Windows (cross-compiled with MinGW-w64) or both.
- **The converters are C++** (`editor/q32x`, `q32x-convert`), shared by the
  editor and the game's Makefile. With no edits they write the same bytes
  as the Python tools did. The game build no longer needs Python.
- **Level extensions** the editor can add (each optional, in a directory
  the level header points to):
  - closed doors hide the areas behind them;
  - hard light and shadow regions of the light maps as flat-shaded
    polygons;
  - flat-shading-only levels (no textures or light maps);
  - a draw distance per area;
  - reduced textures: a coarser mip level at 16 colours, 4 bits a texel;
  - model lighting from a light grid;
  - doors and lifts drawn as models;
  - trimmed model animations and distance versions of models;
  - a shared sound bank, stored once for all levels.
- **The Mega Drive data has a fixed place** (ROM offset 0x30000), so the
  editor can put a project's own into the shipped game program without
  relinking it. Weapon tiles can be packed (the 68000 unpacks them), and one
  weapon render can serve several screen sizes.
- **Auto-optimise** (editor, and `q32x-convert auto`): every tool that keeps
  the picture as it is goes on, then the others are tightened step by step
  until the level fits its room in the cartridge.
- **Simpler collision:** the world's clip hulls rebuilt as balanced trees
  over exactly the same solid space (checked at 110,000 points a hull). A
  point's collision test takes 22-49% fewer plane tests in open space; the
  E1M1 bot plays through exactly as before, 2.5% faster in the cycle model.
- **Speed check** (32X view): the frame rate the cartridge would get along a
  recorded path or the attract demo, estimated from the renderer's own work
  with weights fitted against the cycle model (whole demos within 0-8%),
  with the slow places marked on the map.
- **Flat mode in a palette of your own** (Colours panel, or `q32x-convert
  flat-palette`): load 256 colours (.pal, .gpl, .hex, .act, .lmp or .png), or use the built-in grey one.
  Each texture gets the nearest of them to its average, made more vivid and,
  if you like, its hue spread round the colour wheel so a level of browns
  gets reds, greens and blues; colour rules and face colours pick from your
  palette. Shading is the editor's own: occlusion (less light under arches
  and bridges, in corners and alcoves, and on faces turned downward) and the
  level's light entities with shadows, optionally blended with Quake's light
  maps; faces are split where the light changes. The game swaps the palette
  in while textures are off, with Quake's colours remapped to it for
  monsters, items and particles (the FLATPAL lump). In the cycle model the
  flat E1M3 demo runs at the same speed as with Quake's palette.
- **4 MB cartridges with no mapper:** a cartridge that fits in 4 MB is built
  without Sega's mapper (the levels packed at 64 KB steps and read where
  they lie), and `build.sh` asks which maps to put on it, listing each with
  its size, whether to keep the textures (without them more maps fit), and
  whether flat shading is in greys (`data/palettes/quake32x-greys.gpl`,
  with walls, floors and monsters in greys only and the special colours
  kept), and whether to make the sounds smaller. New converter commands:
  `maps` and `make-project`.
- **Smaller sounds** (game and converter): at half rate (5.5 kHz, half the
  bytes; the mixer reads each sample twice), packed in 4 bits (each sample
  a step from the last, from 16 steps fitted to each block of 256; 57% of
  the bytes, 19-31 dB from the original on Quake's sounds, decoded a byte
  at a time as they play), or both (28%). Over E1M3's attract demo, which
  plays many sounds, the frame rate is 2-3% lower with either and 4-5%
  with both. (4-bit IMA ADPCM was tried first: 44% lower.)
- **Bodies teleport away silently,** still in their burst of particles.
- **Status, stated plainly:** Quake 32X is a proof of concept. The README
  lists what works and what does not (Shamblers, Chthon and E1M7's ending,
  the rocket launcher, super nailgun and Pentagram, some of E1M4-E1M8's
  scripting), and `build.sh` shows what each map is missing before you
  choose (the converter's `maps` command reports it).
- **The cycle model's palette** (`tools/sh2sim.c`): a long write to the
  32X palette now sets both colours, as the 16-bit bus does on the
  hardware (it set one, so the model showed colour shifts and palette
  changes wrong; the game was right).
- **Plain colours with hard shadows:** flat mode without light-map shading,
  the shadow regions as darker polygons; **colour rules** by texture and
  surface (floor, wall, ceiling) with an arcade preset, and colour remaps.
- **Automatic choices** for draw distances (per door area, from its size) and
  for which movers to draw as models (only small ones: in the cycle model,
  every E1M3 mover drawn as a model cost 30% of the frame rate).
- **Flat-shaded weapon sprites** for flat mode, rendered like the models and
  shown while textures are off.
- **The screen size starts at 70%.**
- **The cartridge is built from a project** (`data/quake32x.q32x`): the
  Makefile exports it with the tools that keep the picture as it is, the
  shared sound bank and packed weapon tiles. 9 MB instead of 10. `make
  PLAIN=1` builds the levels without them.
- A compile profile for ericw-tools in TrenchBroom (func_detail keeps detail
  out of the BSP; Quake's own compiled maps cannot be re-split that way).
- **The editor starts at the Quake 32X window** and asks for pak0.pak, then
  which of its maps become the levels; Quake 32X is the only game it offers.
  `editor/build.sh` leaves a clean `build/<platform>` (`quake32x-editor`,
  `q32x-convert`, the game program and the resources), with the compiler's
  work in `build/.work/<platform>`.
- **The frame rate counter** is at the top left, in the border.
- Texture rules: a pattern starting with `*` now also matches liquids (whose
  names start with `*`).
- In the cycle model: E1M3's attract demo with every texture reduced (the
  editor's texture rules) runs at 12.4 fps textured against 7.1. A
  cartridge with door areas, light polygons, a flat-only E1M2, merged
  textured faces, the sound bank, trimmed models and packed weapon tiles is
  9 MB instead of 10, with the Mega Drive data down from 289 KB to 101 KB.

## 0.6.0 - 2026-10-18

Speed, from giving the second CPU more of the frame and from assembly.

- **The next frame is prepared while the second CPU draws.** The view, the
  visibility, the models to draw and the BSP walk for the next frame are
  worked out by the first CPU while the second is still drawing the current
  one, instead of after both are done. In the E1M3 attract demo the time
  each frame where only one CPU worked went from about 15 ms to 6 ms.
- **One BSP walk a frame instead of two.** The walk of the level's tree is
  done once for the whole screen, ahead of the frame, and both CPUs take
  their faces from it.
- **Assembly** for the rasterizer, the face geometry and the model vertices.
  Each is checked frame by frame against its C version (new
  `tools/check_asm.sh`): the pictures are the same bit for bit.
- The sound mixer's last library call is gone.
- In the cycle model, against 0.5.0: the E1M3 attract demo 19% faster flat
  shaded (13.8 → 16.5 fps) and 8% faster textured (6.7 → 7.3); the E1M1
  route 4% faster flat (26.9 → 27.9) and 6% faster textured (15.0 → 15.9).
- Tried and left out: skipping faces already hidden behind what is drawn
  (it cost more than it saved), and reordering the level data (about 1%).

## 0.5.0 - 2026-10-18

Speed, mostly for flat shading and the second CPU.

- **Flat shading merges walls.** When the cartridge is built, neighbouring
  faces with the same texture and nearly the same light become one larger
  polygon for flat mode, with the light averaged by area. About 7% fewer
  polygons to draw (E1M1 5,059 → 4,697, E1M2 4,892 → 4,556, E1M3 4,419 →
  4,068). Textured mode is unchanged.
- **Switches stay textured in flat mode.** Buttons, switches, shootable
  switches, key-door markings, the slipgate and the exit signs keep their
  textures, so you can still see what to press.
- **Faster sound mixer.** The mixer only works on the channels actually
  playing in each part of a buffer and mixes straight into the output. It
  took 18% of the second CPU and now takes 10%, which goes to drawing.
- **Better use of the second CPU.** The screen split between the two CPUs is
  rebalanced more accurately each frame, and the first CPU draws the Mega
  Drive text and talks to the 68000 while it waits for the second, instead
  of afterwards. The part of each E1M3 frame where only one CPU works went
  from about 35 ms to 24 ms.
- Overall, in the cycle model: the E1M3 attract demo runs 7% faster flat
  shaded (12.9 → 13.8 fps) and 4% faster textured (6.5 → 6.7); the E1M1
  route 4% faster flat (25.9 → 26.9) and the same textured.
- Doors, lifts and trains that have not moved are not set up again each
  frame.
- Version number in the menu (top left) and in the cartridge header.
- `build.sh`: builds the compilers and the cartridge on CachyOS / Arch Linux.
- New README for players; the technical notes moved to `docs/TECHNICAL.md`.

## 0.4.1 - 2026-10-18

- Dead monsters (and gibbed heads) teleport away a second after they land,
  with Quake's teleport flash.
- The weapon in your hands is drawn the way Quake draws it: dithered, at
  1.3x, with three palettes for light, normal and dark areas.

## 0.4.0 - 2026-10-17

The three-level cartridge.

- E1M1, E1M2 and E1M3, played in order, with Quake's carry-over of health,
  armour, weapons and ammunition, end-of-level statistics, and a closing
  screen after E1M3.
- 10 MB cartridge using Sega's standard bank-switching mapper.
- All the monsters of the three levels: grunts, rottweilers, ogres, knights,
  fiends, scrags and zombies.
- Silver and gold keys, trains, spike traps, Quad Damage, Biosuit, Ring of
  Shadows, swimming, drowning, slime and lava, Quake's colour flashes.
- Quake's `demo1` (in E1M3) plays as the attract mode at power-on, in real
  time.

## 0.3.0 - 2026-10-17

- Analogue pad support (XE-1 AP and emulator versions of it), with its own
  menu page.
- The Mega Drive draws the menu text, the border around a smaller view and
  the weapon in your hands.

## 0.2.0 - 2026-10-17

E1M1 becomes a game.

- Monsters, items, weapons, ammunition and sound (Quake's sounds through the
  32X PWM), with a cap on how many monsters are in the level at once.
- Doors, lifts, buttons, secret doors, teleporters and triggers.
- Button remapping.
- A bot that plays E1M1 to the exit, from the menu.

## 0.1.0 - 2026-10-17

The first version: E1M1 to walk (or fly) around in.

- Renderer on both SH-2s: texture-mapped with Quake's light maps, or flat
  shaded.
- Pause menu: textures, lighting, draw distance, screen size, see-through
  water, frame rate counter, walk or fly.

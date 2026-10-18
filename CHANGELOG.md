# Changelog

Versions are 0.x.y until the first public release, 1.0.0. The minor number
goes up for new features, the last number for fixes. Versions before 0.5.0
were tagged afterwards, on the commits listed.

## 0.6.0 — 2026-10-18

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

## 0.5.0 — 2026-10-18

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

## 0.4.1 — 2026-10-18

- Dead monsters (and gibbed heads) teleport away a second after they land,
  with Quake's teleport flash.
- The weapon in your hands is drawn the way Quake draws it: dithered, at
  1.3x, with three palettes for light, normal and dark areas.

## 0.4.0 — 2026-10-17

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

## 0.3.0 — 2026-10-17

- Analogue pad support (XE-1 AP and emulator versions of it), with its own
  menu page.
- The Mega Drive draws the menu text, the border around a smaller view and
  the weapon in your hands.

## 0.2.0 — 2026-10-17

E1M1 becomes a game.

- Monsters, items, weapons, ammunition and sound (Quake's sounds through the
  32X PWM), with a cap on how many monsters are in the level at once.
- Doors, lifts, buttons, secret doors, teleporters and triggers.
- Button remapping.
- A bot that plays E1M1 to the exit, from the menu.

## 0.1.0 — 2026-10-17

The first version: E1M1 to walk (or fly) around in.

- Renderer on both SH-2s: texture-mapped with Quake's light maps, or flat
  shaded.
- Pause menu: textures, lighting, draw distance, screen size, see-through
  water, frame rate counter, walk or fly.

# Quake 32X

A proof of concept: Quake on a stock Sega 32X. A from-scratch engine runs
on the 32X's two SH-2s, and the Mega Drive draws the text, the status bar
border and the weapon in your hands.

A cartridge is 4 MB at most for now, which holds about two levels. **The
usual cartridge has two: START** (the introduction, where you pick a skill
and an episode) **and E1M1** (the Slipgate Complex), textured, with a grey
flat-shaded mode on the pause menu. E1M1 plays from start to exit with its
monsters, weapons, items, doors, lifts, secrets and sounds (see **Status**
below for what is missing). The engine also plays E1M2 (Castle of the
Damned) and E1M3 (the Necropolis) to their exits, but they are not on the
usual cartridge: `build.sh` can make a cartridge with other levels, as many
as fit in 4 MB.

Version **0.8.0** (see [CHANGELOG.md](CHANGELOG.md)). It shows that Quake
can run on the 32X; it is not a complete port, and it is not being
actively developed.

> **Not yet tested on a real 32X.** Quake 32X has only been run in
> emulators (PicoDrive) and in this project's own cycle model of the
> hardware. The frame rates below are that model's estimates. It may not
> run, or may run differently, on a real console and flash cart.

The technical side (how the renderer works, the cartridge layout,
performance figures, the tools) is in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Status

**What works** (in emulators and the cycle model):

* On the usual cartridge, START and E1M1. In START, every skill hall leads
  on to the episodes, but the game plays at normal skill whichever you
  take, and every episode's slipgate leads to E1M1 (the cartridge holds no
  other episode); the gate to the final level stays shut, as in Quake.
* E1M1, and (on a cartridge you build with them) E1M2 and E1M3, from start
  to exit: their doors, lifts, buttons, secrets, keys, teleporters, traps,
  liquids and sounds.
* Monsters: Grunts, Rottweilers, Ogres, Knights, Fiends, Scrags and Zombies.
* Weapons: axe, shotgun, double-barrelled shotgun, nailgun and grenade
  launcher.
* Items: health, ammunition, armour, keys, the Quad Damage, the Biosuit and
  the Ring of Shadows.
* Textured or flat-shaded drawing, the pause menu, the attract demo (on a
  cartridge with E1M3), the demo bot (on a cartridge that starts with
  E1M1), 6-button pads and the XE-1 AP analogue pad.
* Cartridges of up to 4 MB, which need no mapper. Every sound, texture and
  model is stored once on the cartridge however many levels use it.

**What does not work yet** (the converter leaves these out of the levels):

* Shamblers (one in E1M3, many later), and every monster of the later
  episodes (Rotfish, Spawn, Death Knights, Enforcers, Vores, Shub-Niggurath).
* The rocket launcher, the super nailgun, the thunderbolt and the Pentagram
  of Protection.
* E1M4-E1M8 convert and load, but without their Shamblers, and E1M4-E1M7
  without some of their scripting (relayed triggers, monster jumps, lava
  fireballs, bubbles). **E1M7 cannot be finished:** Chthon, his lightning
  machine and the rune are not in the game. The full game's final level is
  not in the shareware pak and would need Shub-Niggurath's ending.
* Deathmatch and co-op; saving; music (the 32X has no CD audio here).
* The demo bot follows a route made for E1M1, from the start of the first
  level on the cartridge, so it only plays properly when that is E1M1 (on
  the usual cartridge the first level is START, so it does not).
* It has not been tested on real hardware (above).

`build.sh` shows what is missing from each map before you choose.

## Playing

The usual cartridge starts in START at power-on. Walk through a skill
hall, then into any episode's slipgate, to go to E1M1; at E1M1's exit a
closing screen ends the game. Press **START** for the menu, and **New
game** starts again from the first level.

On a cartridge you build with other levels, each level's exit leads to the
next, carrying over your health (back to between 50 and 100), armour,
weapons and ammunition, as in Quake, and the last ends with the closing
screen. A cartridge with E1M3 plays Quake's own `demo1` (recorded on E1M3)
at power-on as an attract mode, as Quake does. Dying restarts the level.

Monsters (E1M1: grunts and rottweilers; E1M2 and E1M3 add ogres, knights,
fiends, scrags and zombies, which only stay down when gibbed: use
grenades). Weapons: axe, shotgun, super shotgun, nailgun and grenade
launcher.

### Controls (3- or 6-button pad)

| Button (default) | Action |
|---|---|
| D-pad up / down | move forward / back |
| D-pad left / right | turn |
| A + left / right | strafe |
| A + up / down | look up / down |
| C | fire (walking) / fly down (flying) |
| A + C | next weapon |
| B | jump (walking) / fly up (flying) |
| START | menu (pauses the game) |

Fire, jump and the strafe/look modifier can each be moved to A, B, C, X, Y,
Z or MODE (the last four need a 6-button pad), and next weapon to a free
button, in **Controls...**. The controls as set are shown at the bottom of
the screen for the first few seconds of play.

### Analogue pad (XE-1 AP)

If port 1 holds Dempa / Micomsoft's XE-1 AP, or an emulator's version of it
(Polyshroom maps a PlayStation or Xbox pad to it), the sticks work too. By
default:

| Input | Action |
|---|---|
| left stick | move and strafe |
| right stick | turn and look up/down |
| right trigger | fire |
| left trigger | jump |
| C | next weapon |

The sticks also work the menus. Everything above can be changed in
**Analogue pad...**.

DualAnalogueAdapter, a separate project, turns an Xbox, PlayStation or
other modern pad (Bluetooth or USB) into this pad, or into an ordinary
6-button pad. [ANALOGUE.md](ANALOGUE.md) describes the analogue controls and
the protocol the game reads, for anyone making their own pad, adapter or
emulator support.

## Menu

START opens it over the demo or the game. Up/Down picks a line,
Left/Right (or A, B, C) changes it. The game keeps drawing behind the menu,
so you see each change at once. The version number is at the top left.

| Option | Choices | What it does |
|---|---|---|
| New game | | The first level from the start: 100 health, axe, shotgun, 25 shells |
| Textures | ON / OFF (flat) | Texture-mapped walls, or flat shading: one colour per wall, worked out from the texture and the level's light when the cartridge is built. Buttons, switches, key-door markings and exit signs stay textured. Flat is much faster |
| Lighting | ON / OFF | Quake's lighting on or off, in both modes |
| Water | SOLID / SEE-THROUGH | See-through draws water and slime as a fine mesh over what is below |
| Draw distance | 256 ... 3072, MAX | Nothing beyond it is drawn (black) |
| Screen size | 100% ... 50% | A smaller view with Quake's border around it; faster |
| Monsters at once | OFF, 1, 2, 3, 4, 6, 8, UNLIMITED | How many monsters are in the level at a time (default 2). The rest wait and teleport in as others die; monsters left far behind go back to wait. UNLIMITED is Quake's normal behaviour |
| Show FPS | ON / OFF | Frame rate counter |
| Movement | WALK / FLY | Fly mode has no gravity: B goes up, C goes down |
| Controls... | | Button layout, look up/down inverted, Defaults |
| Analogue pad... | | Stick layout, turn speed (90-360°/s), look speed (60-240°/s), inverted look, dead zone, throttle use, trigger and button assignments; the bottom line shows what is plugged into port 1 |
| Demo (bot plays) | OFF / PLAYING | A bot plays E1M1 to the exit (when the cartridge starts with E1M1). Turn it off to take over where it is |
| Resume | | Back to the game |

Dead monsters teleport away silently, in a burst of particles, a second
after they land, to keep the frame rate up.

## Speed

On the cycle model of the hardware (see docs/TECHNICAL.md), full screen
(measured on 0.6 cartridges with E1M1-E1M3):
E1M1 runs at about 16 fps textured and 28 flat; E1M3, the heaviest level,
at about 7 fps textured over the attract demo and 16 flat. The game starts
at 70% screen size, where E1M3's attract demo runs at about 12 fps
textured. The fastest setup is flat shading at 70-80% screen size; the
editor's tools go further (E1M3 flat with plain colours and hard shadows:
18 fps at 70%).

The cartridge has only been run in emulators (PicoDrive, and the project's
own cycle model). Cartridges are 4 MB at most for now, so they need no
mapper; E1M1-E1M3 together do not fit (about 5.4 MB even without textures),
so the usual cartridge is START and E1M1. The game still has the code for
Sega's bank-switching mapper (the *Super Street Fighter II* one), which
larger cartridges would need, but the converter does not build them.

## Building

You need Quake's `pak0.pak`. The shareware one is enough: everything on the
cartridge comes from the shareware episode.

### CachyOS / Arch Linux

```
git clone <this repository> quake32x && cd quake32x
cp /path/to/quake/id1/pak0.pak data/
./build.sh
```

`build.sh` installs what it needs with pacman (`base-devel cmake wget
texinfo gmp mpfr libmpc`, asking for sudo), builds the
cross compilers the first time (binutils for `sh-elf` and `m68k-elf`, GCC 13
for the SH-2, about 20-40 minutes, into `~/.local/share/q32x-toolchain`),
then builds `quake32x.32x`. Later runs skip straight to the cartridge.

Run in a terminal, `build.sh` then asks what to put on the cartridge, which
must fit in 4 MB (so it needs no mapper: some flash carts and consoles may
have trouble with one):

1. **Textures:** full size, half size (a quarter of the space; walls look
   softer close up) or none (flat shading only, so more maps fit). With
   textures, the pause menu's **Textures** setting switches between textured
   and flat shading as you play.
2. **Liquids, skies and stained glass in plain colours?** (with textures)
   They then take no space, with the textures on too, but lose their moving
   surfaces; with textures on they take the nearest of Quake's own colours.
3. **Flat shading in greys?** Flat mode in `data/palettes/quake32x-greys.gpl`:
   walls, floors, monsters and items in greys, with lava, water, slime,
   teleporters, sky, stained glass (yellow), blood and Quake's full-bright
   colours (fire, flashes, lights) in colour.
4. **Smaller sounds?** Full quality (as Quake has them), half rate (half
   the space, duller), packed (57% of the space, a faint hiss) or both (28%).
   The smaller ones take the mixer a little more work: 2-5% slower when
   many sounds play.
5. **Which maps, in order.** It lists every map in your pak with its size
   on its own, then measures your choice together: maps share their sounds,
   textures and models, so it shows what each adds and the running total,
   and asks again until the choice fits.

Without a terminal it builds the usual cartridge: START and E1M1 with
half-size textures, plain liquids, skies and glass, flat shading in greys
and packed sounds (about 3.9 MB); `MAPS=default ./build.sh` does the same
from a terminal.

```
./build.sh              # everything that is missing
./build.sh toolchain    # only the compilers
./build.sh rom          # only the cartridge
./build.sh clean        # remove build output
PAK=~/quake/id1/pak0.pak ./build.sh    # take pak0.pak from elsewhere
TC_PREFIX=/opt/q32x ./build.sh         # put the compilers somewhere else
SKIP_DEPS=1 ./build.sh                 # don't run pacman
MAPS="e1m1 e1m5" TEXTURES=none GREY=1 SOUNDS=both ./build.sh   # choose without being asked
MAPS="start e1m1" TEXTURES=half FLAT_SPECIALS=1 SOUNDS=packed ./build.sh
```

### Elsewhere

With an `sh-elf` and `m68k-elf` toolchain already installed, CMake and a
C++20 compiler: `make TC=/path/to/toolchain/bin`. See docs/TECHNICAL.md.

## The editor

A level editor for the cartridge (TrenchBroom with a Quake 32X module, which
shows each level as the 32X draws it and has the tools that make levels
smaller and faster) is a separate project, not published yet. The
converter it shares with the game is here, in `editor/q32x` (its library,
command-line tool and tests), because the game's build uses it.

## Legal

Quake is id Software's. This project contains none of Quake's data: the
build reads it from your own `pak0.pak`. A cartridge built from the
shareware `pak0.pak` holds only shareware-episode data. This project is not
affiliated with or endorsed by id Software.

## Licence

Quake 32X is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version (GPL-2.0-or-later). It is distributed in the hope that it
will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
[LICENSE](LICENSE) for the GNU General Public License version 2.

This covers the game, its tools and its converters. Quake's vertex
normals, which the game and converters include, are id Software's, under
the GPL v2 or later.

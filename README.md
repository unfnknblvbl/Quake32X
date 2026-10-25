# Quake 32X

Quake's first three levels on a stock Sega 32X: E1M1 (the Slipgate
Complex), E1M2 (Castle of the Damned) and E1M3 (the Necropolis), playable
from start to exit, with their monsters, weapons, items, doors, lifts,
secrets and sounds. A from-scratch engine runs on the 32X's two SH-2s, and
the Mega Drive draws the text, the status bar border and the weapon in your
hands.

Version **0.7.0** (see [CHANGELOG.md](CHANGELOG.md)). Versions 0.x are works
in progress; 1.0.0 will be the first public release.

The technical side (how the renderer works, the cartridge layout,
performance figures, the tools) is in [docs/TECHNICAL.md](docs/TECHNICAL.md).

## Playing

At power-on the cartridge plays Quake's own `demo1` as an attract mode, as
Quake does. Press **START** for the menu, and pick **New game** to start
E1M1. Each level's exit leads to the next, carrying over your health (back
to between 50 and 100), armour, weapons and ammunition, as in Quake. Dying
restarts the level. After E1M3 a closing screen ends the game, and B returns
to the demo.

Monsters: grunts, rottweilers, ogres, knights, a fiend or three, scrags and
zombies (zombies only stay down when gibbed: use grenades). Weapons: axe,
shotgun, super shotgun, nailgun and grenade launcher.

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
(32X-HD maps a PlayStation or Xbox pad to it), the sticks work too. By
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
6-button pad.

## Menu

START opens it over the demo or the game. Up/Down picks a line,
Left/Right (or A, B, C) changes it. The game keeps drawing behind the menu,
so you see each change at once. The version number is at the top left.

| Option | Choices | What it does |
|---|---|---|
| New game | | E1M1 from the start: 100 health, axe, shotgun, 25 shells |
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
| Demo (bot plays) | OFF / PLAYING | A bot plays E1M1 to the exit, then the attract demo returns. Turn it off to take over where it is |
| Resume | | Back to the game |

Dead monsters teleport away a second after they land, to keep the frame
rate up.

## Speed

On the cycle model of the hardware (see docs/TECHNICAL.md), full screen:
E1M1 runs at about 16 fps textured and 28 flat; E1M3, the heaviest level,
at about 7 fps textured over the attract demo and 16 flat. The game starts
at 70% screen size, where E1M3's attract demo runs at about 12 fps
textured. The fastest setup is flat shading at 70-80% screen size; the
editor's tools go further (E1M3 flat with plain colours and hard shadows:
18 fps at 70%).

The cartridge has only been run in emulators (PicoDrive, and the project's
own cycle model). It is 10 MB and uses Sega's standard bank-switching mapper
(the *Super Street Fighter II* one), so real hardware needs a flash cart
that provides that mapper for 32X games. That has not been tried yet.

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

```
./build.sh              # everything that is missing
./build.sh toolchain    # only the compilers
./build.sh rom          # only the cartridge
./build.sh clean        # remove build output
PAK=~/quake/id1/pak0.pak ./build.sh    # take pak0.pak from elsewhere
TC_PREFIX=/opt/q32x ./build.sh         # put the compilers somewhere else
SKIP_DEPS=1 ./build.sh                 # don't run pacman
```

### Elsewhere

With an `sh-elf` and `m68k-elf` toolchain already installed, CMake and a
C++20 compiler: `make TC=/path/to/toolchain/bin`. See docs/TECHNICAL.md.

## The editor

`editor/` is a level editor for the cartridge: TrenchBroom with a Quake 32X
module. It converts Quake's maps, models and sounds, shows each level as
the 32X draws it, and has the tools to make levels smaller and faster:
door areas, light maps as polygons, reduced textures, colours by hand,
geometry reduction, the sound bank, model trimming, and the Mega Drive's
weapon sprites, border and font. It exports the levels and, with the game
program it ships with, the cartridge. No SH-2 compiler is needed.

```
editor/build.sh              # asks: Linux, Windows or both
editor/build.sh linux        # build/linux/quake32x-editor
editor/build.sh windows      # build/windows (MinGW-w64, Qt from the AUR)
editor/build.sh both --deps  # both, installing what is missing first
```

See [editor/q32x/README.md](editor/q32x/README.md).

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

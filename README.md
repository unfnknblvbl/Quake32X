# Quake 32X

Quake's first three levels on a stock Sega 32X: E1M1 (the Slipgate
Complex), E1M2 (Castle of the Damned) and E1M3 (the Necropolis), playable
from start to exit, with their monsters, weapons, items, doors, lifts,
secrets and sounds. A from-scratch engine runs on the 32X's two SH-2s, and
the Mega Drive draws the text, the status bar border and the weapon in your
hands.

Version **0.5.0** (see [CHANGELOG.md](CHANGELOG.md)). Versions 0.x are works
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
E1M1 runs at about 15 fps textured and 25 or more flat; E1M3, the heaviest
level, at 6-7 fps textured over the attract demo and about twice that flat.
The fastest setup is flat shading at 70-80% screen size.

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

`build.sh` installs what it needs with pacman (`base-devel python
python-numpy wget texinfo gmp mpfr libmpc`, asking for sudo), builds the
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

With an `sh-elf` and `m68k-elf` toolchain already installed, Python 3 and
numpy: `make TC=/path/to/toolchain/bin`. See docs/TECHNICAL.md.

## Legal

Quake is id Software's. This project contains none of Quake's data: the
build reads it from your own `pak0.pak`. A cartridge built from the
shareware `pak0.pak` holds only shareware-episode data.

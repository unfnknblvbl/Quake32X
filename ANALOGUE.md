# Analogue control in Quake 32X

Quake 32X reads controller port 1 in one of two ways:

* an ordinary **3- or 6-button pad**, the usual way;
* an **analogue pad** that speaks the protocol of Dempa / Micomsoft's
  **XE-1 AP**, the analogue pad Sega's own Mega Drive games used. Quake 32X
  also reads **Polyshroom's extension** of that protocol, which adds what a
  modern pad has and the XE-1 AP lacked: the right stick's up and down, and
  the two triggers read separately.

Every vertical blank the Mega Drive's 68000 first reads port 1 as an
ordinary pad. Only when nothing answers that way does it try the XE-1 AP
protocol, and it keeps using it while a pad answers. Nothing has to be
switched over: plug the pad in and the game finds it. The pause menu's
**Analogue pad...** page shows what is in port 1 on its bottom line.

> Like the rest of Quake 32X, this has only been tried in emulators
> (PicoDrive, and Polyshroom's XE-1 AP emulation) and in the project's own
> cycle model, not on a real console.

## What the sticks do

By default:

| Input | Action |
|---|---|
| left stick | move and strafe |
| right stick | turn, and look up and down |
| right trigger | fire |
| left trigger | jump |
| C | next weapon |
| START | the menu |

The stick also works the menus (pushed about 40% of the way from the
centre, it counts as the d-pad). **Analogue pad...** in the pause menu changes:

* the stick layout: left stick move and strafe, or move and turn; right
  stick turn and look, or turn only;
* the turn speed at full stick (90 to 360 degrees a second) and the look
  speed (60 to 240 degrees a second);
* looking up and down inverted;
* the dead zone;
* what the XE-1 AP's throttle does when the pad has no extension: the two
  triggers (forward and back), or looking up and down;
* which trigger or button fires, jumps and changes weapon.

The ordinary pad's controls (**Controls...**) are a separate page and stay
as they are.

## Using a modern pad

**DualAnalogueAdapter**, a separate project by the same author, is an
adapter that turns an Xbox, PlayStation or other modern pad (Bluetooth or
USB) into either an XE-1 AP with the extension or an ordinary 6-button pad,
for any Mega Drive or Genesis game. Some emulators (Polyshroom, for one) can
also present a PC pad as an XE-1 AP.

## The protocol

This is what the game expects on port 1, for anyone making their own pad,
adapter or emulator support. It follows the XE-1 AP as Polyshroom's
ANALOGUE-PAD document describes it.

### The lines

| Port pin | Name here | Direction | Use |
|---|---|---|---|
| TH (pin 7) | REQ | console to pad | the console pulls it low to ask for a report |
| TR (pin 9) | /ACK | pad to console | low: the nibble on D0-D3 is ready |
| TL (pin 6) | L/H | pad to console | which half of a pair this nibble is (0, 1, 0, 1...) |
| D0-D3 (pins 1-4) | data | pad to console | one nibble at a time |

### A report

1. The console drives REQ high, then low: the report starts.
2. About 50 microseconds later the pad sends the first nibble.
3. For each nibble the pad sets D0-D3 and L/H (the nibble's number AND 1)
   with /ACK high, then pulls /ACK low for about 10 microseconds while the
   console reads it. Each nibble takes about 20 microseconds in all.
4. After nibble 11 the XE-1 AP stops. A pad with the extension carries on
   with nibbles 12-19 if REQ is still low.
5. The console raises REQ again.

The pad does not wait for the console, so the game reads each nibble as it
comes and checks the L/H sequence afterwards: a report with a nibble out of
order is cut short there. A report needs at least 12 nibbles, and nibble 10
must be 15, for the game to take it as an XE-1 AP.

### The nibbles

Buttons are 0 when pressed. An axis byte is split over two nibbles, high
nibble first; sticks are 0 to 255 with 128 at rest (X: 0 = left; Y: 0 = up).

| Nibble | Contents |
|---|---|
| 0 | E1, E2, START, SELECT (bits 3 to 0) |
| 1 | A, B, C, D (bits 3 to 0) |
| 2, 6 | channel 1, the left stick X (high, low) |
| 3, 7 | channel 0, the left stick Y (high, low) |
| 4, 8 | channel 3, the right stick X (high, low) |
| 5, 9 | channel 2, the throttle (high, low): 128 at rest, forward is more |
| 10 | 15 |
| 11 | A, B, A', B' (bits 3 to 0; A' and B' are unused here) |
| **Extension** | |
| 12, 13 | 3, 2: the mark that the extension follows |
| 14, 15 | the right stick Y (high, low) |
| 16, 17 | the left trigger, 0 released to 255 fully pressed (high, low) |
| 18, 19 | the right trigger (high, low) |

Without the extension the right stick's up and down stays centred, and the
triggers come from the throttle (forward for the right trigger, back for
the left), unless **Analogue pad...** sets the throttle to looking up and
down.

The game takes the buttons as: A, B and C as on an ordinary pad; D as X; E1
as Y; E2 as Z; START as START; SELECT as MODE.

### Inside the game

The 68000 (`src-md/md.s`: `xe1ap_read`, `xe1ap_pack`) hands the report to
the SH-2s through the 32X's communication registers:

* COMM12: the buttons, in the same format as an ordinary pad's. Bit 13
  marks the analogue pad; bits 14 and 15 are the left and right triggers,
  set when a trigger is past a quarter of the way (64 of 255), or, without
  the extension, when the throttle is below 96 (left) or above 160
  (right). The stick also sets the d-pad bits, for the menus.
* COMM2: the axes, 7 bits each (centre 64), one of three words in turn:

| Bits 15-14 | Bits 13-7 | Bits 6-0 |
|---|---|---|
| 00 | left stick X | left stick Y |
| 01 | right stick X | right stick Y (64 without the extension) |
| 10 | throttle | bit 6: the extension is there |

The master SH-2 (`src/main.c`, `src/game.c`) turns these into movement, turning and
looking with the speeds and dead zone the menu sets. docs/TECHNICAL.md has
more on the pad and on how the two processors share the work.

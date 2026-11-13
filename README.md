# GESTALT for Digitone

GESTALT began as a mash-up of PUMP and RADS; from 5.0 the Digitone's has a wavefolder in RADS's place
(RADS lives on in GESTALT for Digitakt). Its mod id is still `pump`.

A master effect chain for the **Elektron Digitone mk1** (and Digitone Keys), as an
[elekloader](https://github.com/irpina/elekloader) mod (id `pump`): a sine wavefolder, after the
Buchla-style west coast folders, into an SP-303 Vinyl Sim-style levelling compressor, on everything the
Digitone plays. It lives on its own master page and is on every output: main outs,
headphones and USB.

By Robbie (designed and tested on a Digitone mk1, OS 1.44). GPL-2.0-or-later.

**This is the Digitone version.** For the **Digitakt mk1** there is [GESTALT for Digitakt](https://github.com/ClaidArremer/digitakt-gestalt) (RADS into the
same compressor, in place of the Digitakt's stock compressor). The files here do not run
on a Digitakt, and the Digitakt's do not run on a Digitone.

> Unofficial and unsupported. Flashing modified firmware is at your own risk; the stock Digitone OS always
> recovers the unit (send the stock .syx from the boot menu).

## What you need

| | |
|---|---|
| Unit | Digitone mk1 or Digitone Keys |
| OS | **1.44** (tested). A 1.43 build is included, untested; on 1.43 there are no drop-down messages and the SETTINGS choice is not kept over a power cycle |
| Mods | elekloader's **core-dn1 4.0** or later (its insert audio); **4.1.1** (its SETTINGS rows take none of the heap; 3.0 adds two) with [DIRECT JUMP for Digitone](https://github.com/ClaidArremer/digitone-directjump) or any other mod that adds a SETTINGS row (see its README). Not with Tone+FX, which owns the same master page hook |

## Install

Add `gestalt-5.2-os1.44.elemod` (or the 1.43 file) in the elekloader app together with core-dn1,
build, and send the firmware to the Digitone. From the command line:

```
python -m elekloader.patch --stock Digitone_and_Digitone_Keys_OS1.44.syx \
  --mod core-4.1.1-os1.44.elemod --mod gestalt-5.2-os1.44.elemod --out Digitone_gestalt.syx
```

## The page

**FUNC + LFO** opens the master pages; **LEFT / RIGHT** to the page after the stock ones. The knobs sit
as on the page: A–D on the top row (the folder), E–H below (the compressor and the output). Settings are saved
with each pattern.

The chain: FOLD / SHAPE / TONE / TOUCH (the folder) → COMP / RATIO / REL (the compressor) → OUT → a soft limiter.

| Knob | Name | What it does |
|---|---|---|
| A | FOLD | OFF, then the drive into the folder, 1x to 32x (+30 dB): a soft saturation at first, then the wave folds back on itself, again and again as it rises, the harmonics climbing smoothly rather than clipping |
| B | SHAPE | The fold's curve, morphing from one to the next: **SINE** (smooth, round), **TRI** (sharper turns, brighter), **SQR** (a soft square: hollow, nasty), **STEP** (a staircase: gritty, even small signals come out as a fuzz), **RIPL** (extra wiggles: more folds for the same FOLD), **WRAP** (a wavewrapper: the wave jumps from top to bottom instead of turning over), **LEAN** (lopsided: the two halves fold differently, even harmonics) |
| C | TONE | A low-pass after the folder, 300 Hz to 16 kHz, OPEN at the top: tames the folds' top end |
| D | TOUCH | The drive follows the input's level, −64 to +63, OFF in the middle. **+**: loud parts fold harder, tails come out cleaner (hits bloom); **−**: quiet parts fold harder, loud ones less. At full TOUCH the drive moves up to 3 octaves either way, from the level against −12 dBFS |
| E | COMP | OFF, then compression: threshold and make-up (and the ratio, with RATIO on AUTO). More is always louder and denser. 80 to start |
| F | RATIO | AUT (COMP sets it), 2:1 up to 20:1 (8:1 among the snaps), and LIM at the top |
| G | REL | Release, more is slower: PUMP (fast, breathing) through GLUE to LEVEL (slow, smooth) |
| H | OUT | The chain's output level before the limiter: −32 to +6 dB, 0 dB in the middle. Works with COMP OFF too |

Fixed, from PUMP's old controls: attack 6 (a little punch through before the gain comes down), no WRM, no
parallel MIX.

- **Press a knob and turn**: in 8s.
- **FUNC + turn** snaps to the next of each knob's set points: FOLD OFF, 16, 32 … 112, 127; SHAPE each of
  the seven curves; TONE 500 Hz, 1k, 2k, 4k, 8k, 12k, OPEN; TOUCH −64, −32, −16, OFF, +16, +32, +63; COMP OFF, 32, 64, 80,
  96, 127; RATIO AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1, LIMIT; REL 0, 32, 64, 96, 127; OUT −24, −12, −6, −3,
  0, +3, +6 dB.
- **FUNC + REC / STOP / PLAY** copies, pastes or clears the page's settings (clear puts the starting values
  back), with a drop-down saying so.
- The screen on the left, above GESTALT, shows the fold curve: what the folder does to each level of the
  input, as FOLD, SHAPE and TOUCH set it (OFF with FOLD OFF; with TOUCH, it breathes with the playing). It **bounces with the output's level**, so the compressor's pump shows in it. The
  **GR** meter shows the gain reduction in 2 dB segments with a peak tick.
- **YES**: the curve across the whole page, then a graph of the last 2.8 s of gain reduction, then back.
  **FUNC + YES** is the stock TEMP SAVE.
- Every change shows the stock drop-down message, e.g. *Ratio=4.0:1*, *Fold=64*, *Shape=TRI>SQR*, *Tone=2.0kHz*.
- **SETTINGS > FX ANIMATION**: untick it for a still page (no bounce, meter or graph) and a little less CPU.
  Saved with the project.
- Starting values: FOLD OFF, SHAPE SINE, TONE OPEN, TOUCH OFF, COMP 80, RATIO AUTO, REL 64, OUT 0 dB.
  Patterns from 5.0 and 5.1 keep FOLD, TONE and the compressor's four (SHAPE SINE, TOUCH OFF). Patterns from 4.x keep COMP, RATIO, REL and OUT; patterns from PUMP 1.0 to 3.0 keep their CMP, REL, OUT
  and RAT (PUMP off: COMP OFF). The folder starts OFF in both.

Always on: a trim on the folded signal (−3.9 dB, less the drive up to 8 dB: a light fold comes out at the
dry level, a heavy one at about −14 dBFS whatever goes in), a DC block after it (only LEAN makes DC; it rests
otherwise), a 30 Hz high-pass in the compressor's detector (the output
keeps the sub-bass), and a soft limiter that holds peaks under 0 dBFS. The folder runs in stereo; FOLD
glides to a new value over a few milliseconds, so turning it never steps. A folder makes harmonics
past the top of the audio band at high FOLD, and some of them fold back as aliasing: part of the sound, and
TONE takes the edge off.

## Changes

See [CHANGELOG.md](CHANGELOG.md).

## What it processes

PUMP sits at the end of the Digitone's own master stage, so it takes everything that stage mixes: the
four tracks, the external inputs as the Ext-In mixer has them, chorus, delay, reverb and the master
drive. Its output goes to the codec (main outs and headphones) and to USB's main stereo pair; USB's
separate track and input channels stay as the Digitone made them.

## CPU

Each part costs nothing while it is off: with FOLD OFF, COMP OFF and OUT at 0 dB the Digitone plays as
stock. Instructions a 32-frame block (1,500 blocks a second), counted in an emulator, for 5.2: the
compressor about 2,100, the folder about 1,770 (TONE below OPEN: about 500 more; TOUCH: about 100; LEAN, for
its DC block: about 440), both about 3,900 (5.0: about 4,750). SHAPE costs nothing a sample: the curve the
loop reads is built on the UI side when SHAPE or FOLD changes. Both sample loops are in assembly (`src/foldloop.s`, `src/pumploop.s`), checked bit for bit
against the C in `src/fold.c` and `src/pump.c`. The seven curves take 14 KB, the curve buffers 6 KB; the mod is about 42 KB in RAM (4.x: about 152 KB).

## How it works

The compressor (`src/pump.c`) runs once a 32-frame block in one pass: the gain computer works in a log2
domain from two small tables, with a fast stage (attack 0.5 ms, release 800 ms to 70 ms) and a slow,
programme-dependent leveller, from the previous block's stereo-linked peak; the gain ramps across the
block, then OUT and the ceiling. The folder (`src/fold.c`, its sample loop in assembly in
`src/foldloop.s`) runs before it, in stereo: the drive (TOUCH moves it once a block) into a phase, the curve
there (a 1024-entry table, SHAPE's two neighbouring curves mixed and scaled by the trim), the low-pass and a
DC block (resting unless LEAN). Both run as core-dn1 4.0's insert audio, through one owner. No
libc, no FPU, no 64-bit products. The page (`src/pumpctl.c`) adds a kind to the stock master view and
draws over it; settings live in an unused 64-byte block of the pattern (MIDI track 3's), so they follow
pattern switches and saves. The SETTINGS row is `src/pumpset.s`. `test/fold_host.c` runs the chain on a
computer, with the sanitizers, through every combination, a sweep of FOLD, SHAPE, TONE and TOUCH, and silence (`fold_host s` and `t`: the shapes' and TOUCH's levels).

## Building

With elekloader and an m68k cross GCC on the path:

```
python -m elekloader.sdk.build --stock Digitone_and_Digitone_Keys_OS1.44.syx .
```

## Credits and third-party code

PUMP is Robbie's own work, written with Claude (Anthropic), except for two parts adapted from other
GPL-2.0-or-later projects:

| File | Origin | Licence |
|---|---|---|
| `src/pumpctl.c`: the master-page plumbing (adding a page to the stock master view, its draw slot, the per-pattern store) | Adapted from **Tone+FX**'s `digictl` by Digital Alchemy DSP ([DigiAlchemydsp/RingTone](https://github.com/DigiAlchemydsp/RingTone)) | GPL-2.0-or-later |
| `src/pumpset.s`: the SETTINGS row's four menu callbacks | Adapted from **digihealth**'s SYSTEM INFO row by irpina ([irpina/digihealth](https://github.com/irpina/digihealth)) | GPL-2.0-or-later |

Everything else (`src/pump.c`, `src/pump_tables.h`, `src/fold.c`, `src/foldloop.s`, the page design and the
rest of `src/pumpctl.c`) is new. The algorithms used are standard textbook ones: a Chamberlin state-variable
filter, Bresenham's line, fixed-point integer maths.

PUMP builds against irpina's [elekloader](https://github.com/irpina/elekloader) SDK headers and runs on its
core-dn1 (GPL-2.0-or-later); neither is included here. The firmware addresses in `mod.json` and the
sources were found by studying the Digitone's own OS so the mod can work with it; no Elektron code is
included. Inspired by the sound of the **Roland SP-303 Vinyl Sim** compressor and Kwazi's free Ableton rack
take on it; no code from either.

## Trademarks

Elektron and Digitone are trademarks of Elektron Music Machines; Roland and SP-303 of Roland Corporation;
Ableton of Ableton AG. They are named only to say what this works with or was inspired by. This project is
not affiliated with or endorsed by any of them.

## Licence

GPL-2.0-or-later, see [LICENSE](LICENSE).

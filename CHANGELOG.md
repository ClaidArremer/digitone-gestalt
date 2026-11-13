# Changelog: GESTALT for Digitone

**This is the Digitone mk1 / Digitone Keys version** (`pump`). For the Digitakt, see
[PUMP for Digitakt](https://github.com/ClaidArremer/digitakt-pump).

Each version's files are in `elemods/` (`pump-<version>-os1.44.elemod` and `-os1.43`); the newest is there,
older ones are in the history of this repo.

## 5.2 (2026-11-08)

- **SHAPE** in BIAS's place: the fold's curve, morphing through SINE, TRI, SQR, STEP, RIPL, WRAP and LEAN
  (LEAN keeps BIAS's even harmonics). Free a sample: the curve the loop reads is built on the UI side.
  Each curve sits at the same power, so heavy folding keeps one level whichever you pick.
- **TOUCH** in BLEND's place: the drive follows the input's level, + loud parts fold harder, - quiet parts;
  OFF in the middle. About 100 instructions a block.
- The folder is always fully wet (BLEND is gone); the DC block runs only for LEAN.
- Patterns from 5.0 and 5.1 keep FOLD, TONE and the compressor's four; SHAPE starts at SINE, TOUCH OFF.
- The mod is about 42 KB in RAM (the seven curves and their buffers).

## 5.1 (2026-11-07)

- Lighter: the folder costs about a third less, about 1,700 instructions a block with BIAS at the centre
  (5.0: about 2,600) and about 2,150 off it.
- The DC block rests with BIAS at the centre (a centred fold makes no DC), once any DC from before has died
  away (about 170 ms).
- A fixed trim in place of the level evening: -3.9 dB, less the drive up to 8 dB, so a lightly folded signal
  comes out at the dry level and a heavily folded one at about -14 dBFS whatever goes in. It scales the sine
  table (on the UI side, when FOLD changes), so it costs nothing a sample. Heavy folding of a loud input is
  now quieter than in 5.0, and of a quiet one louder: turn OUT to taste.

## 5.0 (2026-11-07)

- **A wavefolder in RADS's place**: a sine folder (FOLD, BIAS, TONE, BLEND) into the same compressor. RADS
  lives on in GESTALT for Digitakt. In stereo, its loop in assembly, checked bit for bit against the C; about
  the CPU of 4.x's RADS, and the mod is about 22 KB instead of 152.
- The page shows the fold curve, bouncing with the output; YES's second view is the curve across the page.
- RATIO's FUNC snaps: 8:1 in place of 6:1 (AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1, LIMIT).
- Patterns from 4.x keep COMP, RATIO, REL and OUT; the folder starts OFF.

## 4.5 (2026-11-07)

- Fixed: just under the converter's clip, the 8-bit index could round to 256, one past the table's end
  (a sample of the next table, at full drive).
- Fixed: in silence, RADS's DC block could settle a hair above 0, and the compressor's make-up lifted
  that to a steady offset at the output. It now settles at 0.
- (Both found while making GESTALT for Digitakt.)

## 4.4 (2026-11-07)

- **RADS in stereo**: each side through its own converter, table, DC block and blend (4.0 to 4.3 ran it
  once on L + R). The sample and hold's clock and the level evening are shared.
- **Lighter**: both sample loops in assembly, the leftovers of LOCUT, the envelope follower and WRM gone,
  the level evening read a frame in eight, the ceiling's common case inline, and a fast path for the usual
  case (no table change under way, BLEND 100 %). In the emulator, with both on, stereo comes to about 3 %
  more instructions than 4.3's mono; the compressor alone is about 8 % lighter.

## 4.3 (2026-11-07)

- Renamed **GESTALT**: a mash-up of PUMP and RADS. The page and its messages say GESTALT. (The mod's id is
  still `pump`, so saved patterns and the FX ANIMATION setting carry over.)

## 4.2 (2026-11-07)

- Fixed: FUNC + LFO opened on a blank "TRIG" page, and one stock master page (EXTERNAL MIXER, or the one
  after it) went missing. The compiler (GCC 15) turned the loop that adds the RADS page to the master pages'
  list into code that wrote every entry one place too late. Built now with `-fno-auto-inc-dec`, so it cannot
  make that kind of loop anywhere in the mod (no change to the audio code's cost).

## 4.1 (2026-11-07)

- Fixed: after FUNC + REC / STOP / PLAY (copy, paste, clear), FUNC could stay "held" (its release came while
  the drop-down hid the page), so PLAY and STOP on the page did nothing until a reboot. FUNC and the knob and
  page keys are now followed whatever is on screen.
- TABLE OFF shows OFF in the table screen (and in the big view) instead of a straight line.
- The page and its messages say RADS.

## 4.0 (2026-11-07)

- **PUMP and RADS in one chain**: RADS's wave tables into the compressor, on one page of eight controls:
  TABLE DRIVE SRR BLEND / COMP RATIO REL OUT. SETTINGS > MASTER FX is gone.
- TABLE and COMP each have **OFF** at the left; with both off (and OUT at 0 dB) the chain costs nothing.
- OUT is the whole chain's output level and works with COMP OFF.
- RATIO shows its ratios on the page (AUT, 2:1 … 20:1, LIM), as PUMP for Digitakt 1.4 does; the drop-down
  says them to a tenth below 10:1.
- **FUNC + turn snaps** on every knob (see the README), as RADS had in 3.0; BLEND shows OFF and percentages.
- Removed: ATK (fixed at 6), WRM (the tables take its place), the parallel MIX, RADS's LOCUT and its
  envelope follower (AMNT, RISE, FALL).
- The record is replaced by the wave table's curve, which bounces with the output's level; YES cycles the
  big curve and the gain-reduction graph.
- Patterns saved with 1.0 to 3.0 keep the compressor's CMP, REL, OUT and RAT (PUMP off becomes COMP OFF).
  RADS starts OFF in them; RADS settings saved with 3.0 are not carried over.
- New patterns start at COMP 80, RATIO AUTO, REL 64, everything else off or at 0 dB.

## 3.0 (2026-11-06)

- **RADS**, a second master effect on the same page: Geiger Counter-style destruction after RADS for Max for
  Live (TABLE GAIN LOCUT SRR / AMNT RISE FALL MIX; see the README). **SETTINGS > MASTER FX** chooses PUMP or RADS,
  saved with the project (PUMP for projects from before 3.0).
- Each effect keeps its own settings in every pattern (RADS in the second half of PUMP's block), and a switch
  fades one out before the other comes in.
- SETTINGS > PUMP ANIMATION is now **FX ANIMATION** (the same choice, for either page).
- FUNC + REC / STOP / PLAY on the page copy, paste and clear PUMP's or RADS's settings (the stock had
  "COPY PAGE NONE").
- RADS's sample loop is in assembly (`src/radsloop.s`, checked sample for sample against the C in
  `src/rads.c`) and runs on the mono sum: about 2,800 instructions a block at its defaults.

## 2.12 (2026-11-02)

- **FUNC + YES** is the stock TEMP SAVE again (2.8 to 2.11 took YES for the graph, FUNC or not), as on PUMP
  for Digitakt 1.4.

The other changes in PUMP for Digitakt 1.4 were already in the Digitone version: every change has always shown
in the stock drop-down, and RAT is a continuous knob here (AUTO, 2:1 to 20:1, LIMIT), whose drop-down shows the
ratio to a tenth (*Ratio=4.0:1*).

## 2.11 (2026-11-01)

- **REL reads the other way**: more is slower, as the stock compressor's REL (0 PUMP, fast; 127 LEVEL, slow),
  as on PUMP for Digitakt 1.4. Patterns saved before sound as they did; their REL now shows as 127 minus its
  old number.

## 2.10 (2026-10-23)

- One step a detent; press and turn moves from multiple to multiple of 8, as the stock pages do.

## 2.9 (2026-10-19)

- The page animates at 20 frames a second again (2.8 drew at 15, and 5 when idle).

## 2.8 (2026-10-18)

- The first release: an SP-303 Vinyl Sim-style levelling compressor on the Digitone's master output, with its
  own master page: PMP, ATK, OUT, RAT, CMP, REL, WRM and MIX, the turning record and tonearm, the
  gain-reduction meter and graph, settings per pattern, and SETTINGS > PUMP ANIMATION.
- Versions 1.0 to 2.7 were development builds, not released.

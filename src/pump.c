/* SPDX-License-Identifier: GPL-2.0-or-later
 * PUMP: a master levelling compressor for the Digitone mk1 / Digitone Keys
 * (OS 1.43 and 1.44), after the character of the SP-303 Vinyl Sim compressor.
 * By Robbie. Runs as core-dn1 4.0's insert audio, on everything the master
 * stage mixes.
 *
 * A levelling amplifier first, a compressor second: turn it on and the mix
 * comes up louder, denser and glued, and the drums breathe with the beat.
 *
 * PUMP 4.0: RADS (rads.c) runs before it in one chain (pm_insert, below). The
 * page sets CMP from COMP (OFF: the compressor fades out), RAT, REL and OUT;
 * ATK is fixed at 6, WRM at 0 and MIX at fully wet (their code is kept). OUT
 * works with the compressor off, a gain alone.
 *
 *   ATK  (B)  punch: each hit's front gets through untouched for 0 .. 15 ms
 *             before the gain comes down (an attack hold, then the 0.5 ms
 *             attack); the body is pumped as before. The ceiling holds peaks.
 *   OUT  (C)  output level before the ceiling: 64 = 0 dB, -32 .. +6 dB.
 *   RAT  (D)  the ratio: AUTO (0, CMP sets it), 2:1 .. 20:1, LIMIT (127).
 *   CMP  (E)  the threshold falls (-10 .. -50 dBFS), with RAT on AUTO the
 *             ratio rises (4:1 .. 20:1), and the make-up gain follows (what
 *             a -12 dBFS peak loses, plus up to 9 dB), so more CMP is always
 *             louder and denser. A wide (10 dB) soft knee.
 *   REL  (F)  the release character. Two stages run together: a FAST one
 *             (release 800 ms at 0 .. 70 ms at 127: smooth riding .. pumping)
 *             and a SLOW leveller (250 ms build-up, 2.5 s let-go) that holds
 *             the overall level on sustained loud passages, as a programme-
 *             dependent levelling amplifier does. Its weight falls as REL
 *             rises (0.9 .. 0.15), so high REL lets the mix swing back up
 *             between hits: the pump.
 *   WRM  (G)  drive into a cubic soft clipper after the gain: +0 .. +12 dB of
 *             drive with the peaks held. 0 skips it.
 *   MIX  (H)  parallel blend; the compressor is a pure gain, so the blend
 *             folds into that gain.
 *   Always: a 30 Hz high-pass (12 dB an octave) in the detector, so inaudible
 *             sub-bass does not drive the pump; the output keeps it.
 *
 * CPU. One pass a 32-frame block (1500 a second). The gain computer and both
 * envelopes run once a block in a log2 domain from small tables, from the
 * last block's stereo-linked level; the gain ramps within the block (over 32
 * frames when it rises, over the first 8 when it falls), so there is no
 * zipper. Per frame: the detector every other frame (shifts and adds) and the
 * gain (two multiplies a channel; with WRM, the gain folds into its drive:
 * three). Off (and faded out) core does not call it.
 *
 * Units. "lu" = log2 in Q8: 256 lu = one octave = 6.02 dB, 0 lu = 24-bit full
 * scale. Envelopes are lu in Q12 (lu << 12) with Q15 per-block coefficients.
 * Linear gains are Q12 (4096 = 1.0, at most ~32x). No libc, no FPU, no 64-bit
 * products.
 */
#include <stdint.h>
#include "pump_tables.h"

#define DN_OUT_FRAMES 32            /* frames a block: core-dn1 hands 32 L,R pairs */

/* The parameters (pumpctl.c edits them and stores them per pattern), 0..127,
 * one step a notch, the stock convention. */
int digipump_on = 1;                /* COMP above OFF (pumpctl.c sets it from COMP) */
int digipump_squash = 80;           /* COMP */
int digipump_pump = 63;             /* REL: stored as 127 - the knob; mid = GLUE */
int digipump_warm = 0;              /* WRM: gone in 4.0 (RADS's tables took its place), fixed 0 */
int digipump_mix = 127;             /* MIX: gone in 4.0, fixed fully wet */
int digipump_atk = 6;               /* ATK: gone from the page in 4.0, fixed at 6 */
int digipump_out = 64;              /* OUT: 64 = 0 dB; on RADS and the compressor alike */
int digipump_rat = 0;               /* RAT: 0 AUTO (CMP sets it), 1..126 2:1..20:1, 127 limit */
int digipump_meter;                 /* the page wants the output level (digipump_lvl) */
int digipump_lvl;                   /* the output's peak since the page last read it (24 bits) */
int digipump_gr;                    /* live gain reduction, lu (page meter) */
int digipump_grpk;                  /* its peak since the page last read it */

#define PM_KNEE   425               /* 10 dB in lu */
#define PM_REF    (-510)            /* -12 dBFS, the Digitone's typical peak, in lu */
#define PM_FLOOR  (-8000)           /* level of a silent block, lu */
#define PM_FS_LU  (23 << 8)         /* 24-bit full scale: 2^23 */
#define PM_RAMP   64                /* on/off crossfade step per block (~43 ms) */

static int pm_wet;                  /* 0..4096: the on/off crossfade */
static int pm_ef, pm_es;            /* fast / slow gain reduction, lu Q12 */
static int pm_g = 4096;             /* gain applied at the end of last block */
static int pm_hold, pm_held;        /* ATK: blocks left to hold; a hit is under way */

/* OUT: 0..127 -> lu: 64 = 0 dB, 0 = -32 dB (0.5 dB a step), 127 = +6 dB */
static int pm_out_lu(int v)
{
    return v <= 64 ? ((v - 64) * 1361) / 64 : ((v - 64) * 255) / 63;
}

/* log2(p) in lu, relative to 24-bit full scale. Once per block. */
static int pm_log2(unsigned p)
{
    int n, m;
    if (p < 256u)
        return PM_FLOOR;
    n = 31;
    while (!(p & (1u << n)))
        n--;
    m = (int)(n >= 8 ? p >> (n - 8) : p << (8 - n));      /* 256..511 */
    return (n << 8) + pm_log2f[m - 256] - PM_FS_LU;
}

/* 2^(lu/256) as a Q12 linear gain, clamped to 0 .. ~32x. Once per block. */
static int pm_exp2(int lu)
{
    int n = lu >> 8;                                    /* floor */
    int b = pm_exp2f[lu & 255];                         /* Q14, 1.0 .. 2.0 */
    if (n > 4)
        return 131071;
    if (n >= 2)
        return b << (n - 2);
    if (2 - n >= 16)
        return 0;
    return b >> (2 - n);
}

/* Output ceiling, for a build with no ceiling stage after us (the standalone
 * PUMP mod sets PUMP_CEILING; inside Tone+FX its own ceiling follows). The same
 * curve as Tone+FX's: transparent below ~-2.5 dBFS, 2:1 above, held under
 * ~-0.7 dBFS, so the make-up gain can never wrap the 24-bit output. */
#ifdef PUMP_CEILING
#define PM_LIM_KNEE 0x600000
#define PM_LIM_MAX  0x740000
static inline int pm_ceil(int x)
{
    if (x > PM_LIM_KNEE) {
        x = PM_LIM_KNEE + ((x - PM_LIM_KNEE) >> 1);
        return x > PM_LIM_MAX ? PM_LIM_MAX : x;
    }
    if (x < -PM_LIM_KNEE) {
        x = -PM_LIM_KNEE - ((-PM_LIM_KNEE - x) >> 1);
        return x < -PM_LIM_MAX ? -PM_LIM_MAX : x;
    }
    return x;
}
#else
#define pm_ceil(x) (x)
#endif

/* one-pole step toward t (lu Q12) with a Q15 coefficient, no overflow:
 * |t - e| < 2^24, so (d >> 9) * c < 2^30. Resolution ~1 lu at the slowest
 * coefficient, a fiftieth of a dB. */
static inline int pm_step(int e, int t, int c)
{
    return e + (((t - e) >> 9) * c >> 6);
}

/* A state-variable filter (Chamberlin): low += f band; high = x - low - q band;
 * band += f high. q = 1.5 (Q 0.67, near Butterworth). The corner is picked so f is
 * made of shifts, no multiplies (this runs on every sample): FS(b) is f * b.
 * States run with 4 extra bits (<< 4) so the small f steps are not lost. */
struct pm_svf { int lo, bd; };
#define PM_SVF(name, FS)                                                        \
static inline int name(struct pm_svf *st, int x)                               \
{                                                                              \
    int hi, b = st->bd;                                                        \
    st->lo += FS(b);                                                           \
    hi = x * 16 - st->lo - b - (b >> 1);      /* q = 1.5 (Q 0.67) */            \
    st->bd = b + FS(hi);                                                       \
    return hi >> 4;                                                            \
}
#define PM_F30(b)  ((b) >> 7)                           /* f = 1/128 at 24 kHz: 29.8 Hz */
PM_SVF(pm_hp30, PM_F30)

/* The detector, every other frame (24 kHz): the rumble cut, a fixed 30 Hz
 * high-pass (12 dB an octave), on the mid only (sub-bass is mono), so the
 * Digitone's inaudible sub-bass no longer drives the gain reduction; the
 * output keeps it. The level is |mid| + |side|, which is max(|L|, |R|): a
 * hard-panned sound counts as before. Shifts and adds only. */
static struct pm_svf pm_hp;
static int pm_peak;                         /* last block's level, for this one */
static inline int pm_detect(struct pm_svf *hp, int l, int r)
{
    int m = pm_hp30(hp, (l + r) >> 1), sd = (l - r) >> 1;
    if (m < 0) m = -m;
    if (sd < 0) sd = -sd;
    return m + sd;
}

/* fully off: the compressor starts clean next time */
static void pm_clean(void)
{
    if (pm_ef || pm_es || pm_hp.lo || pm_hp.bd || pm_peak || digipump_gr) {
        pm_ef = pm_es = 0;
        pm_hp.lo = pm_hp.bd = 0;
        pm_peak = 0;
        digipump_gr = 0;
    }
}

/* One 32-frame block, one pass: core's input (Q1.31) in, the output (Q1.31)
 * out. The gain comes from the last block's level (0.67 ms before); the
 * detector reads this block's on the way through, for the next. */
/* The block's frames (GESTALT 4.4): the gain ramped from g by dg a frame over the first nr, each
 * channel times it (24-bit x, Q12 g, split so no product passes 32 bits), the ceiling; and the
 * detector a frame in two. Returns the block's detected peak. pumploop.s is the same in assembly. */
#if defined(__mcoldfire__) && !defined(PM_C_LOOP)
int pm_frames_asm(const int32_t *in, int32_t *out, int g, int dg, int nr, struct pm_svf *hp);
#define pm_frames pm_frames_asm
#else
static int pm_frames(const int32_t *in, int32_t *out, int g, int dg, int nr, struct pm_svf *hp)
{
    int i, peak = 0;
    for (i = 0; i < DN_OUT_FRAMES; i++) {
        int x0 = in[0] >> 8, x1 = in[1] >> 8, l, r;
        if (i < nr)
            g += dg;
        if (!(i & 1)) {
            int d = pm_detect(hp, x0, x1);
            if (d > peak) peak = d;
        }
        l = (x0 >> 12) * g + (((x0 & 0xfff) * g) >> 12);
        r = (x1 >> 12) * g + (((x1 & 0xfff) * g) >> 12);
        out[0] = (int32_t)((uint32_t)pm_ceil(l) << 8);
        out[1] = (int32_t)((uint32_t)pm_ceil(r) << 8);
        in += 2;
        out += 2;
    }
    return peak;
}
#endif

static void pm_block(const int32_t *in, int32_t *out)
{
    int i, peak, lvl, over, gr, grt, gr_s, w, s, thr, mk;
    int g, gm, dg, nr, mixw, og;
    int sq = digipump_squash, pmp = digipump_pump;

    /* the on/off crossfade; fully off and settled with OUT at 0 dB: true bypass. OUT is not
     * the compressor's: it stays with COMP OFF, for RADS */
    if (digipump_on) {
        if (pm_wet < 4096)
            pm_wet += PM_RAMP;
    } else if (pm_wet > 0) {
        pm_wet -= PM_RAMP;
    } else if (digipump_out == 64 && pm_g == 4096) {
        pm_clean();
        for (i = 0; i < 2 * DN_OUT_FRAMES; i++)        /* settled: as it came */
            out[i] = in[i];
        return;
    } else {
        /* COMP OFF (and faded out), OUT not 0 dB, or on its way back: the level alone, ramped
         * over the block; no detector, the compressor starts clean when COMP comes back */
        int go = digipump_out == 64 ? 4096 : pm_exp2(pm_out_lu(digipump_out));
        g = pm_g;
        dg = (go - g) >> 5;
        for (i = 0; i < DN_OUT_FRAMES; i++) {
            int x0 = in[0] >> 8, x1 = in[1] >> 8, l, r;
            g += dg;
            l = (x0 >> 12) * g + (((x0 & 0xfff) * g) >> 12);
            r = (x1 >> 12) * g + (((x1 & 0xfff) * g) >> 12);
            out[0] = (int32_t)((uint32_t)pm_ceil(l) << 8);
            out[1] = (int32_t)((uint32_t)pm_ceil(r) << 8);
            in += 2;
            out += 2;
        }
        pm_g = go;
        pm_ef = pm_es = 0;
        pm_hp.lo = pm_hp.bd = 0;
        pm_peak = 0;
        digipump_gr = 0;
        return;
    }

    lvl = pm_log2((unsigned)pm_peak);
    peak = 0;

    /* gain computer: soft knee, ratio 4:1 .. 20:1, threshold -10 .. -50 dBFS.
     * The Digitone mixes well below full scale (its factory patterns peak
     * near -12 dBFS), so the range sits where its signal is: at CMP 64 a
     * -12 dBFS peak is pressed down by ~12 dB, and the mix breathes. */
    thr = -425 - (sq * 1701) / 127;                     /* -10 dB - 0..40 dB, lu */
    if (digipump_rat <= 0)                              /* RAT AUTO: 4:1 .. 20:1 with CMP */
        s = 256 - 4096 / (64 + (sq * 256) / 127);       /* 1 - 1/ratio, Q8 */
    else if (digipump_rat >= 127)
        s = 256;                                        /* a limiter */
    else                                                /* 2:1 .. 20:1, even in log */
        s = 256 - (256 << 12) / pm_exp2(256 + ((digipump_rat - 1) * 850) / 125);
    over = lvl - thr;
    if (2 * over <= -PM_KNEE)
        grt = 0;
    else if (2 * over >= PM_KNEE)
        grt = (over * s) >> 8;
    else {
        int t = over + PM_KNEE / 2;                     /* 0 .. knee */
        grt = (s * t * t / (2 * PM_KNEE)) >> 8;
    }
    grt <<= 12;

    /* two release stages: fast (punch / pump) and the slow leveller */
    /* ATK: a hit (the target jumps 3 dB or more above the envelope) holds the
     * gain where it was for up to 15 ms, then the 0.5 ms attack clamps down:
     * the hit's front gets through, the body is pumped as before */
    if (grt > pm_ef + (128 << 12) && !pm_hold && !pm_held && digipump_atk) {
        pm_hold = 1 + (digipump_atk * 22) / 127;        /* blocks of 0.67 ms */
        pm_held = 1;
    }
    if (grt <= pm_ef)
        pm_held = 0;                                    /* the hit is over: re-arm */
    if (pm_hold) {
        pm_hold--;
    } else {
        pm_ef = pm_step(pm_ef, grt, grt > pm_ef ? PM_ATT_FAST : pm_relf[pmp & 127]);
    }
    pm_es = pm_step(pm_es, grt, grt > pm_es ? PM_ATT_SLOW : PM_REL_SLOW);
    w = 230 - (pmp * 192) / 127;                        /* leveller weight, Q8:
                                                         * 0.9 .. 0.15 */
    gr_s = (pm_es >> 8) * w;                            /* lu Q12 */
    gr = pm_ef > gr_s ? pm_ef : gr_s;
    gr >>= 12;
    digipump_gr = gr;
    if (gr > digipump_grpk)
        digipump_grpk = gr;

    /* automatic make-up: what a -12 dBFS peak (PM_REF) loses, back, plus up
     * to 9 dB with CMP: louder and denser as CMP rises */
    mk = PM_REF - thr;
    mk = (mk > 0 ? (mk * s) >> 8 : 0) + (sq * 383) / 127;

    /* the block's gain, the MIX blend and the on/off crossfade, all linear */
    g = pm_exp2(mk - gr);
    mixw = (digipump_mix * 129) >> 2;                   /* 0..127 -> 0..4095 */
    mixw = (mixw * pm_wet) >> 12;
    gm = 4096 + (((g - 4096) * mixw) >> 12);

    /* OUT folds into the gain (free; 4.4: no WRM, so always) */
    og = digipump_out == 64 ? 4096 : pm_exp2(pm_out_lu(digipump_out));
    gm = (gm * og) >> 12;

    /* ramp from last block's gain to this one: across the 32 frames when it
     * rises (release: smooth), across the first 8 when it falls (attack: the
     * block's own peak is caught before it reaches the ceiling) */
    g = pm_g;
    if (gm < g) {
        nr = 8;
        dg = (gm - g) >> 3;
    } else {
        nr = 32;
        dg = (gm - g) >> 5;
    }
    pm_g = gm;


    peak = pm_frames(in, out, g, dg, nr, &pm_hp);
    pm_peak = peak;
    if (!pm_wet)
        pm_clean();                                     /* faded out */
}

/* core-dn1 4.0's insert audio. Core hands us what the master stage
 * wrote (Q1.31) and sends what we return to the codec AND to USB's main pair,
 * so PUMP is on every output, USB included. The block is processed at 24 bits
 * like the stock output; the ceiling (PUMP_CEILING) keeps it inside 24 bits,
 * so the shift back up cannot overflow. */
#include "digitone-mk1/core3.h"

/* PUMP 4.0: RADS, then the compressor, in one chain. RADS runs while TABLE is not OFF (and
 * until its blend has faded out), PUMP while COMP is not OFF or OUT is not 0 dB (and until it has
 * faded out); with both off, core plays the stage's output as is. */
void digifold_block(const int32_t *in, int32_t *out);
int digifold_sync(void);            /* the folder wants the insert, or is still fading out */
static volatile int pm_rads;        /* the folder is in the chain (from the UI task) */
static int32_t pm_buf[2 * DN_OUT_FRAMES];

static int pm_idle(void)
{
    return !digipump_on && pm_wet == 0 && digipump_out == 64 && pm_g == 4096;
}

/* the output's level for the page's bouncing table: a frame in four, while it is shown */
static void pm_level(const int32_t *o)
{
    int i, pk = digipump_lvl;
    for (i = 0; i < 2 * DN_OUT_FRAMES; i += 8) {
        int l = o[i] >> 8, r = o[i + 1] >> 8;
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > pk) pk = l;
        if (r > pk) pk = r;
    }
    digipump_lvl = pk;
}

static void pm_insert(int32_t *out, const int32_t *in)
{
    if (pm_rads) {
        if (pm_idle()) {
            pm_clean();
            digifold_block(in, out);
        } else {
            digifold_block(in, pm_buf);
            pm_block(pm_buf, out);
        }
    } else {
        pm_block(in, out);
    }
    if (digipump_meter)
        pm_level(out);
}

struct core_audio_owner digipump_owner = { 0, pm_insert, CORE_AUDIO_INSERT };

/* From the UI task, every frame: hold the insert while PUMP or RADS is on or still fading out;
 * once both are off and faded, core plays the stage's output as is. */
void digipump_owner_sync(void)
{
    int rads = digifold_sync();
    pm_rads = rads;
    digipump_owner.on = !pm_idle() || rads;
}

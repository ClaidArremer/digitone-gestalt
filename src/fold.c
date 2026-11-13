/* SPDX-License-Identifier: GPL-2.0-or-later
 * fold: GESTALT's wavefolder (GESTALT for Digitone, from 5.0; RADS's place before the compressor).
 * By Robbie.
 *
 * A wavefolder, after the Buchla-style west coast folders: each channel's sample, times the drive,
 * is a phase into a curve. Up to a full-scale input at drive 1 that is a soft saturation; past it the
 * curve turns over and the wave folds back on itself, again and again as the drive rises.
 *
 *   FOLD  (A) OFF, then the drive, 1x .. 32x (+30 dB).
 *   SHAPE (B) the fold's curve, morphing: SINE, TRI, SQR, STEP, RIPL, WRAP, LEAN.
 *   TONE  (C) a one-pole low-pass, 300 Hz .. 16 kHz, OPEN at the top.
 *   TOUCH (D) the drive follows the input's level: + loud parts fold harder, - quiet parts do;
 *             the middle: off.
 *
 * Per channel and frame: the drive (a multiply), the curve (a 1024-entry table, Q15, scaled by the
 * trim), the low-pass (a multiply, unless OPEN), a DC block (only LEAN makes DC: it rests otherwise,
 * once any DC has died away), the ceiling. The curve the loop reads is built on the UI side whenever
 * FOLD or SHAPE changes: SHAPE's two neighbouring curves mixed, times the trim (half the drive back
 * off up to 8 dB: a light fold at the dry level, a heavy one at one level whatever goes in), so none
 * of that costs anything a sample. TOUCH works once a block, from the input's peaks: it moves the
 * drive. The drive glides to a new value over a few blocks, so turning it never steps. foldloop.s is
 * the same loop in assembly. No libc, no FPU, no 64-bit products. With FOLD OFF it fades out and
 * leaves the chain.
 *
 * Levels: the folded path runs at 16 bits (x16, full scale 32767), the dry path at 24.
 */
#include <stdint.h>
#include "fold_data.h"
#include "digitone-mk1/core3.h"

#define FD_FRAMES 32

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The parameters, as pumpctl.c sets them (0..127) */
int digifold_amt = 0;               /* FOLD: 0 OFF (the drive stays where it was, for the fade) */
int digifold_shape = 0;             /* SHAPE: SINE */
int digifold_tone = 127;            /* TONE: OPEN */
int digifold_touch = 64;            /* TOUCH: 64 off */
int digifold_active;                /* FOLD is not OFF */

/* For the page: the drive as heard (Q11), the fade (0..1024), and the curve (SHAPE's, untrimmed) */
int digifold_gq = 2048, digifold_wm;
static short fd_view[1024];
const short *const digifold_sin = fd_view;

/* ---- state ---- */
struct fd_chs { int lp, dx, dy; };      /* the low-pass, the DC block's last input and output */
static struct fd_chs fd_ch[2];
static int fd_g = 2048;                 /* the drive (Q11), gliding to FOLD's */
static int fd_ge = 2048;                /* the drive as heard, with TOUCH */
static int fd_env;                      /* TOUCH: the input's level, peaks (24 bits) */

static int fd_g_target(void) { return fd_gq[clampi(digifold_amt, 1, 127)]; }

/* a quarter of the way to t, a block; the last of the way at once */
static int fd_glide(int v, int t)
{
    int n = v + ((t - v) >> 2);
    return n == v ? t : n;
}
static int fd_wm;                       /* the fade (FOLD OFF) at the end of the last block, Q10 */

/* lu = log2 in Q8 (256 lu = 6.02 dB). Once a block. */
static int fd_log2(unsigned p)
{
    int n = 31, m;
    unsigned x;
    if (!p)
        return -8000;
    while (!(p & (1u << n)))
        n--;
    x = n >= 8 ? p >> (n - 8) : p << (8 - n);       /* 256..511 */
    m = (int)x - 256;
    return (n << 8) + m + ((m * (256 - m)) * 87 >> 16);
}

/* 2^(lu/256) as a Q12 gain, lu within +-6 octaves. Once a block. */
static int fd_exp2(int lu)
{
    int n = lu >> 8, f = lu & 255;
    int b = 4096 + ((f << 4) - (((f * (256 - f)) * 87 >> 16) << 4));
    if (n >= 0)
        return n > 6 ? 4096 << 6 : b << n;
    return -n > 12 ? 0 : b >> -n;
}

/* the ceiling: as PUMP's, transparent below ~-2.5 dBFS, 2:1 above, held under ~-0.9 dBFS */
#define FD_LIM_KNEE 0x600000
#define FD_LIM_MAX  0x740000
static inline int fd_ceil(int x)
{
    if (x > FD_LIM_KNEE) {
        x = FD_LIM_KNEE + ((x - FD_LIM_KNEE) >> 1);
        return x > FD_LIM_MAX ? FD_LIM_MAX : x;
    }
    if (x < -FD_LIM_KNEE) {
        x = -FD_LIM_KNEE - ((-FD_LIM_KNEE - x) >> 1);
        return x < -FD_LIM_MAX ? -FD_LIM_MAX : x;
    }
    return x;
}

/* the DC block's leak, dy >> 11, but at least 1 while dy is above 0, so it settles at 0 */
static inline int fd_leak(int dy)
{
    int l = dy >> 11;
    return (!l && dy > 0) ? 1 : l;
}

/* a block's constants for fd_frames (foldloop.s reads them by these offsets: keep them in step) */
struct fd_k {
    int g0, dg, b0_, db_;               /*  0  4  8 12: the drive at the block's start, a frame's step */
    int k;                              /* 16: the low-pass, Q15 (32768: open) */
    int dc, wm0, dwm, full;             /* 20 24 28 32: dc, the DC block runs */
    const short *sin;                   /* 36 */
};

/* the curve the loop reads, SHAPE's, scaled by the trim: two, the UI task fills the one not playing */
static short fd_tab[2][1024];
static const short *volatile fd_cur = fd_tab[0];
static int fd_tab_f = -1, fd_tab_s = -1;    /* the FOLD and SHAPE fd_cur is built for */
static int fd_asym;                     /* SHAPE leans (LEAN's in it): the DC block runs */
static int fd_dcc;                      /* blocks with no DC made (FD_DC_REST: the DC block rests) */
#define FD_DC_REST 256

static void fd_retrim(void)
{
    int f = clampi(digifold_amt, 1, 127), sh = clampi(digifold_shape, 0, 127), i, tr, n, fr;
    const short *a, *b;
    short *t;
    if (f == fd_tab_f && sh == fd_tab_s)
        return;
    if (sh != fd_tab_s) {                       /* SHAPE: its two neighbours, mixed */
        for (n = 0; n < FD_SHAPES - 2 && sh >= fd_shape_pt[n + 1]; n++)
            ;
        fr = ((sh - fd_shape_pt[n]) << 12) / (fd_shape_pt[n + 1] - fd_shape_pt[n]);   /* Q12 */
        a = fd_shape[n];
        b = fd_shape[n + 1];
        for (i = 0; i < 1024; i++)
            fd_view[i] = (short)(a[i] + (((b[i] - a[i]) * fr) >> 12));
        fd_asym = sh > fd_shape_pt[FD_SHAPES - 2];
    }
    t = fd_tab[fd_cur == fd_tab[0]];
    tr = fd_trim[f];
    for (i = 0; i < 1024; i++)
        t[i] = (short)((fd_view[i] * tr) >> 15);
    fd_cur = t;
    fd_tab_f = f;
    fd_tab_s = sh;
}

/* the curve at a phase (65536 a turn), Q15 */
#define FD_SIN(t, ph) ((t)[((ph) >> 6) & 1023])

/* The block's frames (in and out interleaved L, R), as foldloop.s. Per channel: x14 = x24 >> 9;
 * the phase (x14 g) >> 11; the curve there (Q15 = x16, trimmed); << 4; the low-pass; the DC block
 * (or not); << 4 to 24 bits; the fade against its own dry signal (FOLD going OFF or coming on);
 * the ceiling. */
#if defined(__mcoldfire__) && !defined(FD_C_LOOP)
void fd_frames_asm(const int32_t *in, int32_t *out, struct fd_chs *s, const struct fd_k *k);
#define fd_frames fd_frames_asm
#else
static void fd_frames(const int32_t *in, int32_t *out, struct fd_chs *s, const struct fd_k *k)
{
    int i, c, g = k->g0, wmq = 0, wm = 0;
    for (i = 0; i < FD_FRAMES; i++) {
        int x[2], w;
        g += k->dg;
        if (!k->full) {
            wmq += k->dwm;
            wm = k->wm0 + (wmq >> 11);
        }
        for (c = 0; c < 2; c++) {
            struct fd_chs *st = &s[c];
            int ph, t;
            x[c] = in[c] >> 8;
            ph = ((x[c] >> 9) * g) >> 11;
            t = FD_SIN(k->sin, ph) * 16;            /* x16 << 4 */
            if (k->k < 32768) {
                st->lp += (((t - st->lp) >> 5) * k->k) >> 10;
                t = st->lp;
            }
            if (k->dc) {
                st->dy += t - st->dx - fd_leak(st->dy); /* DC block */
                st->dx = t;
            } else
                st->dy = t;                         /* (dx: at the block's end) */
        }
        for (c = 0; c < 2; c++) {
            w = s[c].dy * 16;                       /* 24 bits */
            if (!k->full) w = x[c] + ((((w - x[c]) >> 7) * wm) >> 3);
            out[c] = (int32_t)((uint32_t)fd_ceil(w) << 8);
        }
        in += 2;
        out += 2;
    }
    if (!k->dc)                                     /* so the DC block takes up where this left off */
        s[0].dx = s[0].dy, s[1].dx = s[1].dy;
}
#endif

/* TOUCH, once a block: the input's peaks (a frame in four), a fast rise and an ~85 ms fall; the drive
 * moved by the level's distance from -12 dBFS (in octaves, times TOUCH, within -3 .. +3 octaves:
 * at full TOUCH, a hit at 0 dBFS doubles to quadruples it, a tail at -30 dBFS takes it down 8x). */
static int fd_touch(const int32_t *in, int g)
{
    int i, pk = 0, d = clampi(digifold_touch, 0, 127) - 64, lu;
    for (i = 0; i < 2 * FD_FRAMES; i += 8) {
        int l = in[i] >> 8, r = in[i + 1] >> 8;
        if (l < 0) l = -l;
        if (r < 0) r = -r;
        if (l > pk) pk = l;
        if (r > pk) pk = r;
    }
    if (pk > fd_env) fd_env += (pk - fd_env) >> 1;
    else fd_env -= fd_env >> 7;
    if (!d)
        return g;
    lu = clampi(fd_log2((unsigned)fd_env + 1) - 21 * 256, -6 * 256, 2 * 256);   /* re -12 dBFS */
    lu = clampi(lu * d / 64, -3 * 256, 3 * 256);
    g = ((g >> 4) * fd_exp2(lu)) >> 8;
    return clampi(g, 16, 120000);               /* (x14 g stays inside 31 bits) */
}

/* One 32-frame block: core's input (Q1.31) in, the output (Q1.31) out (in place is fine) */
void digifold_block(const int32_t *in, int32_t *out)
{
    int ge, wm0, wm1;
    struct fd_k k;

    /* the drive glides to where FOLD is (~3 ms), TOUCH moves it, ramped across the block;
     * FOLD OFF leaves the drive where it was while it fades out */
    if (digifold_active)
        fd_g = fd_glide(fd_g, fd_g_target());
    ge = fd_touch(in, fd_g);
    k.g0 = fd_ge; k.dg = (ge - fd_ge) / FD_FRAMES;
    k.b0_ = k.db_ = 0;
    fd_ge = ge;                                     /* (a step under a frame's worth: next block) */
    digifold_gq = ge;
    if (!fd_asym) {                                 /* the DC block rests once DC has died away */
        if (fd_dcc < FD_DC_REST) fd_dcc++;
    } else
        fd_dcc = 0;
    k.dc = fd_dcc < FD_DC_REST;
    k.k = fd_tone_k[clampi(digifold_tone, 0, 127)];
    k.sin = fd_cur;

    /* the fade: in when FOLD comes on, out when it goes OFF */
    wm1 = digifold_active ? 1024 : 0;
    wm0 = fd_wm;
    k.wm0 = wm0;
    k.dwm = (wm1 - wm0) * 2048 / FD_FRAMES;
    k.full = wm0 == 1024 && wm1 == 1024;
    fd_wm = wm1;
    digifold_wm = wm1;

    fd_frames(in, out, fd_ch, &k);
}

static int fd_run;

int digifold_idle(void)
{
    return fd_wm == 0;
}

/* From the UI task, every frame (pump.c's owner sync): whether the folder wants the chain, or is
 * still fading out. Stopping: its filters start clean next time; starting: the drive starts where
 * FOLD is, with no glide from the last time. Also builds the curve when FOLD or SHAPE has changed. */
int digifold_sync(void)
{
    int want;
    fd_retrim();
    want = digifold_active;
    if (!want && fd_wm == 0) {
        if (fd_run) {
            int c;
            fd_run = 0;
            for (c = 0; c < 2; c++)
                fd_ch[c].lp = fd_ch[c].dx = fd_ch[c].dy = 0;
        }
        digifold_gq = fd_g_target();
        return 0;
    }
    if (!fd_run) {
        fd_g = fd_ge = fd_g_target();
        fd_env = 0;
        fd_dcc = fd_asym ? 0 : FD_DC_REST;          /* the filters start clean: no DC to wait out */
        fd_run = 1;
        digifold_gq = fd_g;
    }
    return 1;
}

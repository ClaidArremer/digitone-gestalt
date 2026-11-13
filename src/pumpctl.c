/* SPDX-License-Identifier: GPL-2.0-or-later
 * pumpctl: the GESTALT master page (5.0 on: a wavefolder and PUMP; the mod's id stays pump).
 * Digitone mk1 / Keys, OS 1.43 and 1.44. By Robbie; the master-page plumbing is adapted from
 * Tone+FX (below).
 *
 * One page of our own appended to the stock master pages (FUNC + LFO, then LEFT / RIGHT). GESTALT
 * From 5.0 it is one chain: a wavefolder (fold.c), then the SP-303 Vinyl Sim-style compressor (pump.c).
 *
 *   A = FOLD (OFF, then the drive), B = SHAPE (the fold's curve), C = TONE (a low-pass after the
 *   folder, OPEN at the top), D = TOUCH (the drive follows the input's level, either way); E = COMP (OFF, then the compressor), F = RATIO,
 *   G = REL, H = OUT (the chain's level, in dB). Press a knob and turn it for big steps; FUNC +
 *   turn: each knob's snap points.
 *
 * The page: a small screen with the fold curve (OFF with FOLD OFF), bouncing with the output's
 * level, so the compressor's pump shows; the eight controls with their values; a gain-reduction
 * meter. YES: the curve across the page, then a graph of the last 2.8 s of gain reduction.
 * SETTINGS > FX ANIMATION unticked: no animation and no meter.
 *
 * The page plumbing is adapted from Tone+FX's digictl (Digital Alchemy DSP,
 * GPL-2.0-or-later), cut down to this one page: the master view (vtable DN_MASTER_VT)
 * keeps its page kinds in a vector at view+124 (end +128, current index +144);
 * we append one kind and take the view's draw slot. Kind 0 is the value
 * Tone+FX's first page has always used with the stock draw. Settings live in
 * the saved pattern (the third MIDI track's unused 64-byte block, magic
 * "DPM1"), so they follow pattern switches and project saves like stock
 * parameters.
 */
#include "digitone-mk1/core3.h"   /* fw_tempo, fw_transport */

#ifndef DN_TEXTF_ADDR
#define DN_TEXTF_ADDR      0x400DDE68
#endif
#ifndef DN_FONT5_ADDR
#define DN_FONT5_ADDR      0x402315C8
#endif
#ifndef DN_INVALIDATE_ADDR
#define DN_INVALIDATE_ADDR 0x400E512A
#endif
#ifndef DN_OP_NEW_ADDR
#define DN_OP_NEW_ADDR     0x400E944C
#endif
#ifndef DN_STOCK_DRAW_ADDR
#define DN_STOCK_DRAW_ADDR 0x4004DDAE
#endif
#ifndef DN_FW_ADDR
#define DN_FW_ADDR 0x402315DC
#endif
#ifndef DN_FO_ADDR
#define DN_FO_ADDR 0x402316DC
#endif
#ifndef DN_FB_ADDR
#define DN_FB_ADDR 0x402318DC
#endif
#ifndef DN_FBP_ADDR
#define DN_FBP_ADDR 0x40241a44u
#endif
#ifndef DN_PAT_BASE
#define DN_PAT_BASE   0x407FC414u
#endif
#ifndef DN_PAT_PTR
#define DN_PAT_PTR    0x4138E214u
#endif
#ifndef DN_MASTER_VT
#define DN_MASTER_VT  0x40199BAC
#endif

#define DN_TEXTF      ((void (*)(void *, const void *, int, int, int, const char *, ...))DN_TEXTF_ADDR)
#define DN_FONT5      ((const void *)DN_FONT5_ADDR)
#define DN_INVALIDATE ((void (*)(void *))DN_INVALIDATE_ADDR)
/* The firmware returns pointers in %d0 (its own callers test %d0 after
 * malloc); an m68k-linux-gnu build would read %a0. Declared as returning an
 * integer so every toolchain reads %d0, then cast. */
#define DN_OP_NEW(n)  ((int *)((unsigned (*)(unsigned))DN_OP_NEW_ADDR)(n))
#define DN_STOCK_DRAW ((void (*)(void *, void *))DN_STOCK_DRAW_ADDR)
/* The stock drop-down message ("Drive=3"): the firmware's message manager, a
 * singleton (0x4014a060 on 1.44 returns it, creating it the first time), and
 * its printf (0x400ae25c: message manager, format, arguments), the pair the
 * OS uses for "Paste Pattern" or "Sequence cleared!". Both return pointers in
 * %d0, so the getter is declared as returning an integer. OS 1.44 only: its
 * port defines the addresses; without them no message is shown. */
#if defined(DN_POPUP_GET_ADDR) && defined(DN_POPUP_PRINTF_ADDR)
#define DN_POPUP_GET    ((unsigned (*)(void))DN_POPUP_GET_ADDR)
#define DN_POPUP_PRINTF ((void (*)(unsigned, const char *, ...))DN_POPUP_PRINTF_ADDR)
#define PC_POPUP(...)   do { unsigned m_ = DN_POPUP_GET(); \
                             if (m_) DN_POPUP_PRINTF(m_, __VA_ARGS__); } while (0)
#else
#define PC_POPUP(...)   do { } while (0)
#endif
#define DN_FW  ((const unsigned char *)DN_FW_ADDR)
#define DN_FO  ((const unsigned short *)DN_FO_ADDR)
#define DN_FB  ((const unsigned char *)DN_FB_ADDR)
#define DN_FBP (*(volatile unsigned *)DN_FBP_ADDR)

#define PC_KIND       0            /* our page's kind (stock kinds are 15..17) */
#define DN_K_FUNC     1
#define DN_K_YES      13
#define DN_K_PUSH_A   46           /* knob A pressed; .. H = 53 */
#define PC_FAST       8            /* press + turn: to the next multiple of 8 */
#define DN_K_LEFT     17
#define DN_K_RIGHT    18
#define DC_PER_STEP   4            /* wire counts per encoder detent */
#define DC_ACT_TICKS  20           /* frames a moved slider shows its value */
#define DG_TOP        53           /* rows 53..63 are the stock status bar */

extern int digipump_on, digipump_squash, digipump_pump;
extern int digipump_out, digipump_rat, digipump_gr;
extern int digipump_meter, digipump_lvl;
extern struct core_audio_owner digipump_owner;
void digipump_owner_sync(void);
extern int digifold_amt, digifold_shape, digifold_tone, digifold_touch, digifold_active;
extern int digifold_gq, digifold_wm;
extern const short *const digifold_sin;
#include "fold_data.h"

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* the page's own two: COMP (0 OFF, else the compressor's CMP) and FOLD (0 OFF, else
 * the drive); pumpctl_tick hands them to pump.c and fold.c */
static int hy_comp = 80, hy_fold;

/* ---- per-pattern storage -----------------------------------------------------
 * The patterns are a 0x1611D-byte struct array at DN_PAT_BASE; *DN_PAT_PTR is
 * the current one. Each MIDI track leaves a 64-byte block unused at
 * track_t+0x100; Tone+FX keeps its settings in MIDI tracks 0 and 1's
 * (+0x1040, +0x1410), so we take MIDI track 2's (+0x17E0) and never touch
 * theirs: projects saved under Tone+FX keep its settings.
 * v6 (GESTALT 5.2): the eight controls as the knobs sit, A..H (FOLD SHAPE TONE TOUCH
 * COMP RATIO REL OUT). v5 (5.0, 5.1: FOLD BIAS TONE BLEND, then the same four): FOLD, TONE and
 * the compressor's four are taken, SHAPE and TOUCH start at SINE and off. v4 (4.0 .. 4.5): RADS's four, then the same compressor four:
 * those are taken, the folder starts OFF. v1..v3 (PUMP 1.0 ..
 * 3.0) carry the compressor's own: ON, CMP, REL, WRM, MIX, ATK, OUT, RAT (v1 the
 * first five, v2 seven); COMP, REL, OUT and RAT are taken from them (PUMP off:
 * COMP OFF), the folder's start from their defaults (FOLD OFF). PUMP 3.0's RADS
 * settings (the block's second half, "RDS1") are left as they were. */
#define DN_PAT_STRIDE 0x1611Du
#define DN_PAT_SLOTS  128u
#define PC_STORE_OFF  0x17E0u
#define PC_STORE_N    8
#define PC_STORE_VER  6
#define PC_STORE_LEN  (8 + 2 * PC_STORE_N)
#define PC_STORE_MAG(a) ((a)[0] == 0x44 && (a)[1] == 0x50 && \
                         (a)[2] == 0x4D && (a)[3] == 0x31 && \
                         (a)[5] >= 1 && (a)[5] <= 6)           /* "DPM1" v1..v6 */

static int *const pc_param[PC_STORE_N] = {
    &hy_fold, &digifold_shape, &digifold_tone, &digifold_touch,
    &hy_comp, &digipump_rat, &digipump_pump, &digipump_out,
};
/* FOLD OFF, SHAPE SINE, TONE OPEN, TOUCH off; COMP 80, RATIO AUTO, REL 64 (stored 127 - it), OUT 0 dB */
static const short pc_def[PC_STORE_N] = { 0, 0, 127, 64, 80, 0, 63, 64 };
static const short pc_max[PC_STORE_N] = { 127, 127, 127, 127, 127, 127, 127, 127 };
static unsigned char *pc_blk;                   /* block we last synced */
static unsigned char pc_copy[PC_STORE_LEN];
static int pc_dirty;                            /* a page edit is pending */
static int pc_sync_ph;

static unsigned char *pc_slot(void)
{
    unsigned p = *(volatile unsigned *)DN_PAT_PTR;
    if (p < DN_PAT_BASE ||
        p >= DN_PAT_BASE + (unsigned)DN_PAT_SLOTS * DN_PAT_STRIDE)
        return 0;
    return (unsigned char *)(p + PC_STORE_OFF);
}

static int pc_get(const unsigned char *b, int i)
{
    return (short)((b[8 + 2 * i] << 8) | b[9 + 2 * i]);
}

static void pc_defaults(void)
{
    int i;
    for (i = 0; i < PC_STORE_N; i++)
        *pc_param[i] = pc_def[i];
}

static void pc_read(const unsigned char *b)
{
    int i, n;
    if (b[5] >= 6) {
        for (i = 0; i < PC_STORE_N; i++)
            *pc_param[i] = clampi(pc_get(b, i), 0, pc_max[i]);
        return;
    }
    if (b[5] == 5) {                            /* 5.0, 5.1: BIAS and BLEND are gone */
        for (i = 0; i < PC_STORE_N; i++)
            *pc_param[i] = (i == 1 || i == 3) ? pc_def[i] : clampi(pc_get(b, i), 0, pc_max[i]);
        return;
    }
    if (b[5] == 4) {                            /* 4.x: the compressor's four; the folder OFF */
        pc_defaults();
        for (i = 4; i < PC_STORE_N; i++)
            *pc_param[i] = clampi(pc_get(b, i), 0, pc_max[i]);
        return;
    }
    n = b[5] == 1 ? 5 : b[5] == 2 ? 7 : 8;      /* PUMP 1.0 .. 3.0 */
    pc_defaults();
    hy_comp = pc_get(b, 0) ? clampi(pc_get(b, 1), 1, 127) : 0;
    digipump_pump = clampi(pc_get(b, 2), 0, 127);
    if (n >= 7)
        digipump_out = clampi(pc_get(b, 6), 0, 127);
    if (n >= 8)
        digipump_rat = clampi(pc_get(b, 7), 0, 127);
}

static void pc_write(unsigned char *b)
{
    int i;
    b[0] = 0x44; b[1] = 0x50; b[2] = 0x4D; b[3] = 0x31;   /* "DPM1" */
    b[4] = 0; b[5] = PC_STORE_VER;
    b[6] = 0; b[7] = PC_STORE_N;
    for (i = 0; i < PC_STORE_N; i++) {
        int v = *pc_param[i];
        b[8 + 2 * i] = (unsigned char)((v >> 8) & 0xff);
        b[9 + 2 * i] = (unsigned char)(v & 0xff);
    }
}

/* Once a UI frame: follow a pattern switch, persist a page edit, and adopt a
 * reload the OS made behind us -- but only a block that still carries our
 * magic; anything else in it is a stock write, and we reclaim the block. A
 * pattern that never had PUMP starts from the defaults; an older block is
 * rewritten as v4 once read. */
static void pc_sync(void)
{
    unsigned char *b = pc_slot();
    int i;
    if (!b)
        return;
    if (b == pc_blk) {
        if (pc_dirty) {
            pc_write(b);
        } else if (pc_sync_ph++ & 7) {
            return;                          /* compare every 8th tick */
        } else {
            for (i = 0; i < PC_STORE_LEN; i++)
                if (b[i] != pc_copy[i])
                    break;
            if (i < PC_STORE_LEN) {
                if (PC_STORE_MAG(b))
                    pc_read(b);
                else
                    pc_defaults();
                pc_write(b);
            }
        }
    } else {
        pc_blk = b;                          /* pattern switch / first sync */
        if (PC_STORE_MAG(b))
            pc_read(b);
        else
            pc_defaults();
        pc_write(b);
    }
    pc_dirty = 0;
    for (i = 0; i < PC_STORE_LEN; i++)
        pc_copy[i] = b[i];
}

/* ---- the master view ---------------------------------------------------------*/
static char *pc_view;               /* the master view, from its last draw */
static int pc_vis;                  /* frames left before we disarm */
static int pc_act_id;               /* last-moved slider's encoder (0 = none) */
static int pc_act_ttl;              /* frames left to show that slider's value */
static int pc_func;                 /* FUNC held: knob turns snap to 0 / 127 */
static int pc_ctr;                  /* ticks since the last redraw */
static int pc_rate = 3;             /* ticks between redraws: 3 on PUMP, 8 on a
                                     * stock page (each redraw re-arms pc_vis,
                                     * so the handlers stay live exactly while
                                     * the view is on screen, as in Tone+FX) */
static int pc_eacc[10];             /* per-encoder accumulation (ids 0..9) */
static unsigned char pc_push[8];    /* knob A..H held down: press + turn is fast */

/* Ours AND still open: view+48 is the view's "closed" byte, so a closed view
 * cannot keep our key/encoder handlers armed. */
static int pc_view_open(char *v)
{
    return v && *(int *)v == DN_MASTER_VT && *((unsigned char *)v + 48) == 0;
}

static int pc_live(void)
{
    return pc_vis > 0 && pc_view_open(pc_view);
}

static int pc_nkinds(char *view, int **vp)
{
    int *v = *(int **)(view + 124), *e = *(int **)(view + 128);
    *vp = v;
    if (!v || !e || e <= v)
        return 0;
    return (int)(e - v);
}

static int pc_cur_kind(char *view)
{
    int *v, n = pc_nkinds(view, &v), i = *(int *)(view + 144);
    if (i < 0 || i >= n)
        return -1;
    return v[i];
}

/* Append our kind once, after the stock pages. */
static void pc_add_page(char *view)
{
    int *v, *nv, n = pc_nkinds(view, &v), i;
    if (n < 1 || n > 14)
        return;
    for (i = 0; i < n; i++)
        if (v[i] == PC_KIND)
            return;
    nv = DN_OP_NEW(4u * (unsigned)(n + 1));
    if (!nv)
        return;
    for (i = 0; i < n; i++)
        nv[i] = v[i];
    nv[n] = PC_KIND;
    *(int **)(view + 124) = nv;
    *(int **)(view + 128) = nv + n + 1;
    *(int **)(view + 132) = nv + n + 1;
}

/* ---- drawing (direct framebuffer, 1 bpp column-major; y = 0 at the bottom,
 * the stock convention) -------------------------------------------------------*/
static unsigned char *dg_fb;

static void dg_fill(int x0, int y0, int x1, int y1, int c)
{
    int x, t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 < y1) { t = y0; y0 = y1; y1 = t; }        /* y0 = the top row */
    if (x0 < 0) x0 = 0;
    if (x1 > 127) x1 = 127;
    if (y1 < 0) y1 = 0;
    if (y0 > 63) y0 = 63;
    for (x = x0; x <= x1; x++) {
        int rt = 63 - y0, rb = 63 - y1, p;
        unsigned char *col = dg_fb + (x << 3);
        for (p = rt >> 3; p <= (rb >> 3); p++) {
            unsigned char m = 0xffu;
            if (p == (rt >> 3)) m &= (unsigned char)(0xffu << (rt & 7));
            if (p == (rb >> 3)) m &= (unsigned char)(0xffu >> (7 - (rb & 7)));
            if (c) col[7 - p] |= m; else col[7 - p] &= (unsigned char)~m;
        }
    }
}

static int dg_strw(const char *s, int sc)
{
    int w = 0;
    for (; *s; s++)
        w += (DN_FW[(unsigned char)*s] + 1) * sc;
    return w;
}

static void dg_glyph(int x, int ytop, int ch, int sc)
{
    int w = DN_FW[ch & 0xff];
    int off = DN_FO[ch & 0xff];
    int c, r;
    for (c = 0; c < w; c++) {
        int bits = DN_FB[off + c];
        for (r = 0; r < 8; r++) {
            if (bits & (1 << r)) {              /* bit 0 = top of the glyph */
                int px = x + c * sc;
                int py = ytop - r * sc;
                if (sc == 1) {                  /* one pixel: set it, no fill */
                    if ((unsigned)px < 128u && (unsigned)py < 64u) {
                        int rr = 63 - py;
                        dg_fb[(7 - (rr >> 3)) + (px << 3)] |= (unsigned char)(1u << (rr & 7));
                    }
                } else {
                    dg_fill(px, py, px + sc - 1, py - sc + 1, 1);
                }
            }
        }
    }
}

static void dg_text(int x, int ytop, const char *s, int sc)
{
    for (; *s; s++) {
        dg_glyph(x, ytop, (unsigned char)*s, sc);
        x += (DN_FW[(unsigned char)*s] + 1) * sc;
    }
}

/* ---- the page, in screen coordinates (m_*: x right, y down from the top of
 * the panel, as the preview was drawn; rows 0..10 are the stock title bar) ---- */
static void m_fill(int x0, int y0, int x1, int y1, int c) { dg_fill(x0, 63 - y0, x1, 63 - y1, c); }
static void m_px(int x, int y, int c)
{
    int r;
    if ((unsigned)x > 127u || (unsigned)y > 63u)
        return;
    r = y;                                         /* screen row */
    if (c)
        dg_fb[(7 - (r >> 3)) + (x << 3)] |= (unsigned char)(1u << (r & 7));
    else
        dg_fb[(7 - (r >> 3)) + (x << 3)] &= (unsigned char)~(1u << (r & 7));
}
static int m_w(const char *s) { return dg_strw(s, 1) - 1; }
static void m_text(int x, int y, const char *s) { dg_text(x, 63 - y, s, 1); }
static void m_ctext(int cx, int y, const char *s) { m_text(cx - m_w(s) / 2, y, s); }
static void m_rtext(int xr, int y, const char *s) { m_text(xr - m_w(s), y, s); }
static void m_line(int x0, int y0, int x1, int y1)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, e2, n = 256;           /* n: a hard stop, whatever happens */
    while (n-- > 0) {
        m_px(x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * e;                         /* both tests on the same error */
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void m_itoa(char *b, int v)
{
    int n = 0;
    if (v < 0) { b[n++] = '-'; v = -v; }
    if (v >= 100) b[n++] = (char)('0' + v / 100);
    if (v >= 10) b[n++] = (char)('0' + (v / 10) % 10);
    b[n++] = (char)('0' + v % 10);
    b[n] = 0;
}

/* The animation's state, moved on by pumpctl_tick */
#define PC_HIST 56                          /* graph: 2 px a redraw, 2.8 s */
static unsigned char pc_hist[PC_HIST];      /* gain reduction per redraw, dB */
static int pc_hpos;
static int pc_gr_db, pc_pk_db, pc_pk_hold;  /* the meter, its peak and hold */
static int pc_graph;                        /* YES: 0 the controls, 1 the table, 2 the graph */
static int pc_amp = 256;                    /* the curve's bounce, Q8 of its full height */

/* SETTINGS > FX ANIMATION (pumpset.s): byte 0 set = still. Byte 1 was PUMP
 * 3.0's MASTER FX (unused now). Saved with the project (core's project data,
 * "PMPU"); on OS 1.44 a change also asks the OS to refresh its power-up copy
 * of the project, as digisnake does, so it survives a power cycle. */
unsigned char pumpctl_anim_off[4];
static void pc_prefs_loaded(int32_t found) { (void)found; }   /* none: zeros, animate */
const struct core_projdata pumpctl_projdata = {
    0x504d5055u,                            /* "PMPU" */
    sizeof pumpctl_anim_off, pumpctl_anim_off, pc_prefs_loaded
};
#define pc_still() (pumpctl_anim_off[0] != 0)

#define PC_SAVE_TICKS 90                    /* 1.5 s of quiet, then the request */
static unsigned short pc_save_wait;
static unsigned char pc_anim_last;
static void pc_mirror_request(void)
{
#ifdef PC_MIRROR
    uint8_t *p = fw_project;
    uint32_t buf;
    if (!p)
        return;
    buf = *(volatile uint32_t *)(p + 32);
    if (!buf || *(volatile uint8_t *)(p + 36))
        return;
    ((void (*)(void *, uint32_t, uint32_t))(*(uint32_t **)p)[17])(p, buf, 0);
#endif
}

static int pc_db(int lu) { return lu > 0 ? (lu * 385) >> 14 : 0; }   /* lu -> dB */

/* a 24-bit peak in lu (log2 Q8), 0 = full scale; an approximation, for the page */
static int pc_lu(int p)
{
    int n = 23, f;
    if (p <= 0)
        return -6000;
    if (p > 0x7fffff)
        p = 0x7fffff;
    while (!(p & (1 << n)))
        n--;
    f = n >= 8 ? (p >> (n - 8)) & 255 : (p << (8 - n)) & 255;
    return ((n - 23) << 8) + f;
}

/* Each redraw: the gain reduction since the last one (its peak, from the
 * render), into the meter, its peak hold and the graph; and the output's
 * level, into the curve's bounce: full height at 0 dBFS, falling with the
 * level to 0.4 of it at -36 dB and below, dropping back at most an eighth a redraw. */
static void pc_meter_step(void)
{
    extern int digipump_grpk;
    int d = digipump_on ? pc_db(digipump_grpk) : 0, a;
    digipump_grpk = 0;
    pc_gr_db = d;
    if (d >= pc_pk_db) {
        pc_pk_db = d;
        pc_pk_hold = 20;                        /* 1 s */
    } else if (pc_pk_hold > 0) {
        pc_pk_hold--;
    } else if (pc_pk_db > 0) {
        pc_pk_db--;
    }
    pc_hist[pc_hpos] = (unsigned char)(d > 255 ? 255 : d);
    pc_hpos = (pc_hpos + 1) % PC_HIST;
    if (!digipump_owner.on) {
        a = 256;                                /* nothing running: the curve as it is */
    } else {
        a = clampi(256 + pc_lu(digipump_lvl) / 10, 102, 256); /* -36 dB (1536 lu): 0.4 */
    }
    digipump_lvl = 0;
    pc_amp = a >= pc_amp ? a : (pc_amp - a > 32 ? pc_amp - 32 : a);
}

/* the meter: 12 segments of 2 dB from the top, a peak tick, the dB below */
static void m_meter(void)
{
    char b[6];
    int i, pi;
    m_ctext(124, 12, "GR");
    for (i = 0; i < 12; i++) {
        int y = 21 + i * 3;
        if (pc_gr_db > i * 2 + 1)
            m_fill(121, y, 126, y + 1, 1);
        else {
            m_px(121, y, 1);
            m_px(126, y, 1);
        }
    }
    pi = pc_pk_db / 2;
    if (pi > 11) pi = 11;
    if (pc_pk_db > 1)
        m_fill(120, 22 + pi * 3, 127, 22 + pi * 3, 1);
    m_itoa(b, pc_gr_db);
    m_ctext(124, 56, b);
}

/* RATIO in tenths (20 .. 200), as the DSP sets it: 2^(lu / 256) from its octaves and a curved
 * fraction (within half a percent) */
static int pc_rat_tenths(int v)
{
    int lu = 256 + ((v - 1) * 850) / 125, n = lu >> 8, f = lu & 255;
    int b = 4096 + ((f << 4) - (((f * (256 - f)) * 87 >> 16) << 4));
    return ((10 << n) * b + 2048) >> 12;
}

/* OUT in tenths of a dB: 64 = 0, 0.5 dB a step below, 6/63 dB above */
static int pc_out_tenths(int v)
{
    return v <= 64 ? (v - 64) * 5 : ((v - 64) * 60) / 63;
}

/* ---- the fold curve ------------------------------------------------------- */
/* the chain's curve at i (0..255, the input from -1 to +1): the folder as the DSP has it (the
 * drive as heard, TOUCH and all, the same curve), with the straight line as it fades in or out */
static int rs_curve_at(int i)
{
    int x14 = (i * 2 - 255) * 64;                       /* -16320 .. 16320, as x24 >> 9 */
    int g, ph, v, w;
#ifdef PC_TEST_CURVE
    if (1)                                  /* the emulator runs no audio: from the settings */
#else
    if (pc_still() || !digipump_owner.on)   /* nothing heard: from the settings */
#endif
    {
        g = fd_gq[hy_fold ? hy_fold : 1];
        w = hy_fold ? 1024 : 0;
    } else {                                /* as the folder has them now, gliding */
        g = digifold_gq;
        w = digifold_wm;
    }
    ph = (x14 * g) >> 11;
    v = (digifold_sin[(ph >> 6) & 1023] * 255 / 32767 + 255) / 2;   /* 0 .. 255 */
    return i + (((v - i) * w) >> 10);
}

/* the curve in a box x0..x1, y0 (top) .. y1, joined up, its height pc_amp of the box's */
static void rs_curve(int x0, int y0, int x1, int y1)
{
    int w = x1 - x0, h = y1 - y0, i, py = 0, mid2 = y0 + y1;
#ifdef PC_TEST_CURVE
    int amp = 192;
#else
    int amp = pc_still() ? 256 : pc_amp;
#endif
    for (i = 0; i <= w; i++) {
        int c = rs_curve_at((i * 255) / w) * 2 - 255;       /* -255 .. 255 */
        int y = (mid2 - (c * h * amp) / (255 * 256)) / 2;
        if (i)
            m_line(x0 + i - 1, py, x0 + i, y);
        else
            m_px(x0, y, 1);
        py = y;
    }
}

/* the small screen: a rounded frame, a dotted centre line, the curve */
static void rs_screen(int x0, int y0, int x1, int y1)
{
    int x;
    m_fill(x0 + 2, y0, x1 - 2, y0, 1); m_fill(x0 + 2, y1, x1 - 2, y1, 1);
    m_fill(x0, y0 + 2, x0, y1 - 2, 1); m_fill(x1, y0 + 2, x1, y1 - 2, 1);
    m_px(x0 + 1, y0 + 1, 1); m_px(x1 - 1, y0 + 1, 1);
    m_px(x0 + 1, y1 - 1, 1); m_px(x1 - 1, y1 - 1, 1);
    if (!hy_fold) {                         /* FOLD OFF: OFF, no curve */
        m_ctext((x0 + x1) / 2, (y0 + y1) / 2 - 3, "OFF");
        return;
    }
    for (x = x0 + 3; x < x1 - 1; x += 3)
        m_px(x, (y0 + y1) / 2, 1);
    rs_curve(x0 + 2, y0 + 2, x1 - 2, y1 - 2);
}

/* little string helpers for the cells and the drop-downs */
static void rs_cat(char *b, const char *t) { while (*b) b++; while (*t) *b++ = *t++; *b = 0; }
static void rs_signed(char *b, int v)        /* "+12", "0", "-6" */
{
    if (v > 0) { b[0] = '+'; m_itoa(b + 1, v); } else m_itoa(b, v);
}

/* ---- the controls --------------------------------------------------------- */
/* the knob's value, 0..its maximum (REL: the knob, 127 - the stored value) */
static int hy_get(int id)
{
    switch (id) {
    case 1: return hy_fold;
    case 2: return digifold_shape;
    case 3: return digifold_tone;
    case 4: return digifold_touch;
    case 5: return hy_comp;
    case 6: return digipump_rat;
    case 7: return 127 - digipump_pump;
    default: return digipump_out;
    }
}

static void hy_set(int id, int v)
{
    switch (id) {
    case 1: hy_fold = clampi(v, 0, 127); break;
    case 2: digifold_shape = clampi(v, 0, 127); break;
    case 3: digifold_tone = clampi(v, 0, 127); break;
    case 4: digifold_touch = clampi(v, 0, 127); break;
    case 5: hy_comp = clampi(v, 0, 127); break;
    case 6: digipump_rat = clampi(v, 0, 127); break;
    case 7: digipump_pump = 127 - clampi(v, 0, 127); break;
    default: digipump_out = clampi(v, 0, 127); break;
    }
}

static const char *dg_pumpname(void)
{
    static const char *const n[3] = { "LEVEL", "GLUE", "PUMP" };
    int b = (digipump_pump * 3) >> 7;
    return n[b > 2 ? 2 : b];
}

/* SHAPE: the curve nearest the knob */
static int hy_shape_near(void)
{
    int n, best = 0;
    for (n = 1; n < FD_SHAPES; n++) {
        int d = digifold_shape - fd_shape_pt[n], e = digifold_shape - fd_shape_pt[best];
        if (d * d < e * e)
            best = n;
    }
    return best;
}

/* the values as the cells show them (short); the drop-downs say them in full */
static void hy_cellval(char *b, int id)
{
    int v;
    b[0] = 0;
    switch (id) {
    case 1:                                         /* FOLD: OFF, 1..127 */
        if (!hy_fold) rs_cat(b, "OFF"); else m_itoa(b, hy_fold);
        return;
    case 2:                                         /* SHAPE: the nearest curve's name */
        rs_cat(b, fd_shape_name[hy_shape_near()]);
        return;
    case 3:                                         /* TONE: "500Hz", "4.8K", "12K", OPEN */
        v = fd_tone_hz10[digifold_tone];            /* Hz / 10 */
        if (digifold_tone >= 127) rs_cat(b, "OPEN");
        else if (v < 100) { m_itoa(b, v * 10); rs_cat(b, "Hz"); }
        else if (v < 1000) { b[0] = (char)('0' + v / 100); b[1] = '.';
                             b[2] = (char)('0' + (v / 10) % 10); b[3] = 'K'; b[4] = 0; }
        else { m_itoa(b, (v + 50) / 100); rs_cat(b, "K"); }
        return;
    case 4:                                         /* TOUCH: -64 .. +63, OFF in the middle */
        if (digifold_touch == 64) rs_cat(b, "OFF"); else rs_signed(b, digifold_touch - 64);
        return;
    case 5:                                         /* COMP: OFF, 1..127 */
        if (!hy_comp) rs_cat(b, "OFF"); else m_itoa(b, hy_comp);
        return;
    case 6:                                         /* RATIO: AUT, 2:1 .. 20:1, LIM */
        if (!digipump_rat) rs_cat(b, "AUT");
        else if (digipump_rat >= 127) rs_cat(b, "LIM");
        else { m_itoa(b, (pc_rat_tenths(digipump_rat) + 5) / 10); rs_cat(b, ":1"); }
        return;
    case 7:
        m_itoa(b, 127 - digipump_pump);
        return;
    default:                                        /* OUT: whole dB, signed */
        v = pc_out_tenths(digipump_out);
        rs_signed(b, (v + (v < 0 ? -5 : 5)) / 10);
        rs_cat(b, "dB");
        return;
    }
}

static const char *const hy_lab[8] = { "FOLD", "SHAPE", "TONE", "TOUCH", "COMP", "RATIO", "REL", "OUT" };

/* a cell: its name, its value and a bar (OUT's and TOUCH's grow from the middle) */
static void hy_cell(int x, int y, int id)
{
    char b[8];
    int cx = x + 8, v = hy_get(id), vmax = 127;
    m_ctext(cx, y, hy_lab[id - 1]);
    hy_cellval(b, id);
    m_ctext(cx, y + 8, b);
    m_fill(x, y + 18, x + 16, y + 18, 1);          /* the track */
    if (id == 8 || id == 4) {                       /* OUT and TOUCH: from the middle */
        int c = x + 8, e = x + 8 + ((v - 64) * 8) / 64;
        m_fill(c, y + 16, e, y + 18, 1);
        m_fill(c, y + 16, c, y + 19, 1);
    } else {
        m_fill(x, y + 16, x + (v * 16) / vmax, y + 18, 1);
    }
}

static void hy_cells(void)
{
    int x0 = 32, dx = 23, i;                    /* room between the five-letter names */
    for (i = 0; i < 4; i++) {
        hy_cell(x0 + i * dx, 13, i + 1);
        hy_cell(x0 + i * dx, 37, i + 5);
    }
}

/* the last 2.8 s of gain reduction, hanging from the top line; dotted lines at 6, 12 and
 * 18 dB; the compressor's settings under it */
static void m_graph(void)
{
    char b[8];
    int i, k, x;
    m_fill(1, 12, 112, 12, 1);
    for (k = 6; k <= 18; k += 6)
        for (x = 1; x < 112; x += 4)
            m_px(x, 12 + (k * 34) / 24, 1);
    for (i = 0; i < PC_HIST; i++) {
        int d = pc_hist[(pc_hpos + i) % PC_HIST];
        if (d > 24) d = 24;
        m_fill(1 + 2 * i, 12, 2 + 2 * i, 12 + (d * 34) / 24, 1);
    }
    x = 1;
    for (i = 5; i <= 7; i++) {
        m_text(x, 55, hy_lab[i - 1]); x += m_w(hy_lab[i - 1]) + 3;
        hy_cellval(b, i); m_text(x, 55, b); x += m_w(b) + 6;
    }
}

static void pc_page(void *bmp)
{
    char b[8];
    (void)bmp;
    dg_fill(0, 0, 127, DG_TOP - 1, 0);
    if (!pc_still() && pc_graph == 1) {             /* YES: the curve, big */
        rs_screen(1, 12, 126, 54);
        hy_cellval(b, 1);
        m_text(2, 56, "GESTALT");
        m_rtext(126, 56, b);
        return;
    }
    if (!pc_still() && pc_graph == 2) {             /* YES again: the graph */
        m_graph();
        m_meter();
        return;
    }
    rs_screen(1, 13, 27, 50);
    m_text(0, 57, "GESTALT");               /* under the screen, below the COMP bar */
    hy_cells();
    if (!pc_still())                                /* no meter when still: nothing moves */
        m_meter();
}

/* vtable slot 4 of the master view: draw */
void pumpctl_mdraw(void *view, void *bmp)
{
    pc_add_page((char *)view);
    DN_STOCK_DRAW(view, bmp);
    pc_view = (char *)view;
    if (pc_cur_kind(pc_view) != PC_KIND)
        pc_rate = 8;
    else
        pc_rate = pc_still() ? 30 : 3;          /* STILL: 2 fps, the keepalive; else 20 */
    pc_vis = pc_cur_kind(pc_view) == PC_KIND && !pc_still() ? 3 * pc_rate + 2 : pc_rate + 2;
    pc_ctr = 0;
    if (pc_cur_kind(pc_view) != PC_KIND)
        return;
    dg_fb = (unsigned char *)DN_FBP;
    if (dg_fb)
        pc_page(bmp);
}

/* Every UI frame: storage, the page's two into the DSP's, and a redraw every pc_rate frames
 * while the master view is on screen (20 fps on our page). Leaving the view stops the draws,
 * pc_vis runs out and we disarm. */
void pumpctl_tick(void *ctrl)
{
    int ours;
    (void)ctrl;
    pc_sync();
    digipump_on = hy_comp > 0;
    if (hy_comp > 0)
        digipump_squash = hy_comp;
    digifold_active = hy_fold > 0;
    if (hy_fold > 0)
        digifold_amt = hy_fold;
    if (pumpctl_anim_off[0] != pc_anim_last) {   /* SETTINGS changed it */
        pc_anim_last = pumpctl_anim_off[0];
        pc_save_wait = PC_SAVE_TICKS;
    }
    if (pc_save_wait && !--pc_save_wait)
        pc_mirror_request();
    digipump_owner_sync();               /* hold the insert while on or fading */
    if (pc_act_ttl > 0) {
        pc_act_ttl--;
        if (!pc_act_ttl && pc_view_open(pc_view))
            DN_INVALIDATE(pc_view);
    }
    if (pc_vis)
        pc_vis--;
    ours = pc_vis > 0 && pc_view_open(pc_view) && pc_cur_kind(pc_view) == PC_KIND;
    digipump_meter = ours && !pc_still();   /* the output's level: only while it is shown */
    if (pc_vis > 0 && pc_view_open(pc_view)) {
        if (++pc_ctr >= pc_rate) {
            pc_ctr = 0;
            if (ours)
                pc_meter_step();
            DN_INVALIDATE(pc_view);
        }
    } else if (pc_view && !pc_view_open(pc_view)) {
        pc_vis = 0;
    }
}

/* stock convention: one 0..127 step per detent (4 wire counts) */
static int pc_steps(int id, int d)
{
    int st;
    if ((d > 0 && pc_eacc[id] < 0) || (d < 0 && pc_eacc[id] > 0))
        pc_eacc[id] = 0;
    pc_eacc[id] += d;
    st = pc_eacc[id] / DC_PER_STEP;
    pc_eacc[id] -= st * DC_PER_STEP;
    return st;
}

/* a value moved d detents: one a detent, or with the knob held down, from
 * multiple of 8 to multiple of 8 (61 -> 64 -> 72; 0 .. 127) */
static int pc_move(int v, int d, int fast)
{
    if (fast)
        v = d > 0 ? ((v / PC_FAST) + d) * PC_FAST : (((v + PC_FAST - 1) / PC_FAST) + d) * PC_FAST;
    else
        v += d;
    return clampi(v, 0, 127);
}

/* the knobs' stock-style drop-downs */
static void hy_popup(int id)
{
    int v, a;
    char b[12];
    b[0] = 0;
    switch (id) {
    case 1:
        if (!hy_fold) PC_POPUP("Fold=OFF"); else PC_POPUP("Fold=%d", hy_fold);
        break;
    case 2: a = hy_shape_near();
        if (digifold_shape == fd_shape_pt[a]) PC_POPUP("Shape=%s", fd_shape_name[a]);
        else {
            int n = digifold_shape > fd_shape_pt[a] ? a : a - 1;
            PC_POPUP("Shape=%s>%s", fd_shape_name[n], fd_shape_name[n + 1]);
        }
        break;
    case 3: v = fd_tone_hz10[digifold_tone];
        if (digifold_tone >= 127) PC_POPUP("Tone=OPEN");
        else if (v < 100) PC_POPUP("Tone=%dHz", v * 10);
        else PC_POPUP("Tone=%d.%dkHz", v / 100, (v / 10) % 10);
        break;
    case 4: hy_cellval(b, 4); PC_POPUP("Touch=%s", b); break;
    case 5:
        if (!hy_comp) PC_POPUP("Comp=OFF"); else PC_POPUP("Comp=%d", hy_comp);
        break;
    case 6:
        if (!digipump_rat) PC_POPUP("Ratio=AUTO");
        else if (digipump_rat >= 127) PC_POPUP("Ratio=LIMIT");
        else {
            int r = pc_rat_tenths(digipump_rat);
            if (r < 100) PC_POPUP("Ratio=%d.%d:1", r / 10, r % 10);
            else PC_POPUP("Ratio=%d:1", (r + 5) / 10);
        }
        break;
    case 7: PC_POPUP("Release=%s %d", dg_pumpname(), 127 - digipump_pump); break;
    default:
        v = pc_out_tenths(digipump_out); a = v < 0 ? -v : v;
        PC_POPUP("Out=%c%d.%ddB", v < 0 ? '-' : '+', a / 10, a % 10);
        break;
    }
}

/* FUNC + turn: to the next of the knob's snap points that way (on the knob's value) */
static const unsigned char hy_snap_comp[] = { 0, 32, 64, 80, 96, 127 };
static const unsigned char hy_snap_rat[] = { 0, 1, 23, 39, 76, 88, 126, 127 };  /* AUTO, 2:1, 3:1, 4:1, 8:1, 10:1, 20:1, LIMIT */
static const unsigned char hy_snap_fold[] = { 0, 16, 32, 48, 64, 80, 96, 112, 127 };
static const unsigned char hy_snap_touch[] = { 0, 32, 48, 64, 80, 96, 127 };    /* -64, -32, -16, OFF, +16, +32, +63 */
static const unsigned char hy_snap_rel[] = { 0, 32, 64, 96, 127 };
static const unsigned char hy_snap_out[] = { 16, 40, 52, 58, 64, 96, 127 };     /* -24, -12, -6, -3, 0, +3, +6 dB */
static const unsigned char *const hy_snaps[9] = {
    0, hy_snap_fold, fd_shape_pt, fd_snap_tone, hy_snap_touch, hy_snap_comp, hy_snap_rat, hy_snap_rel, hy_snap_out,
};
static const unsigned char hy_nsnaps[9] = {
    0, sizeof hy_snap_fold, sizeof fd_shape_pt, sizeof fd_snap_tone, sizeof hy_snap_touch, sizeof hy_snap_comp,
    sizeof hy_snap_rat, sizeof hy_snap_rel, sizeof hy_snap_out,
};
static int hy_snap(int id, int v, int d)
{
    const unsigned char *p = hy_snaps[id];
    int i, n = hy_nsnaps[id];
    if (d > 0) {
        for (i = 0; i < n; i++)
            if (p[i] > v)
                return p[i];
    } else {
        for (i = n - 1; i >= 0; i--)
            if (p[i] < v)
                return p[i];
    }
    return v;
}

int pumpctl_enc(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int delta = *(int *)((char *)ev + 16);
    int d, v, fast;
    (void)brain;
    if (id < 1 || id > 8 || !pc_live() || pc_cur_kind(pc_view) != PC_KIND)
        return 0;                        /* not ours (LEVEL/DATA included, so FUNC +
                                          * LEVEL still opens the sound browser): the
                                          * stock handles it */
    if (!delta)
        return 1;
    d = pc_steps(id, delta);
    if (!d)
        return 1;
    fast = !pc_func && pc_push[id - 1];  /* press + turn: big steps, as on the stock pages */
    v = hy_get(id);
    if (pc_func) {                /* FUNC + turn: the next snap point */
        v = hy_snap(id, v, d > 0 ? 1 : -1);
    } else {
        v = pc_move(v, d, fast);
    }
    hy_set(id, v);
    hy_popup(id);
    pc_act_id = id;
    pc_act_ttl = DC_ACT_TICKS;
    pc_dirty = 1;
    DN_INVALIDATE(pc_view);
    return 1;
}

/* Copy, paste, clear: FUNC + REC, STOP or PLAY (or a page key, TRIG .. LFO,
 * held instead of FUNC). On our page the stock had nothing to copy ("COPY
 * PAGE NONE"); here it is our eight settings, with a drop-down saying what was
 * done. */
#define DN_K_REC   10
#define DN_K_PLAY  11
#define DN_K_STOP  12
#define DN_K_PAGE0 20                   /* TRIG; SYN1, SYN2, FLTR, AMP, LFO to 25 */
static unsigned char pc_page_held[6];
static int pc_clip[PC_STORE_N], pc_clip_have;

static int pc_copypaste(int key)
{
    int i;
    if (key == DN_K_REC) {
        for (i = 0; i < PC_STORE_N; i++)
            pc_clip[i] = *pc_param[i];
        pc_clip_have = 1;
        PC_POPUP("GESTALT settings copied");
        return 1;
    }
    if (key == DN_K_STOP) {
        if (!pc_clip_have) {
            PC_POPUP("Nothing to paste");
            return 1;
        }
        for (i = 0; i < PC_STORE_N; i++)
            *pc_param[i] = pc_clip[i];
        PC_POPUP("GESTALT settings pasted");
    } else {
        pc_defaults();
        PC_POPUP("GESTALT settings cleared");
    }
    pc_dirty = 1;
    DN_INVALIDATE(pc_view);
    return 1;
}

/* FUNC is tracked (never consumed). LEFT / RIGHT rotate the master page list,
 * stock pages and ours, while the master view is open. On our page, FUNC (or a
 * page key) + REC / STOP / PLAY copies, pastes, clears (above); YES cycles the
 * views. Nothing else is taken. */
int pumpctl_key(void *brain, void *ev)
{
    int id = *(int *)((char *)ev + 12);
    int flags = *(int *)((char *)ev + 16);
    int *v, n, i;
    (void)brain;
    /* FUNC, the knobs' presses and the page keys are followed always, whatever is on screen,
     * and never taken: a release while a drop-down hid the page (4.0) left FUNC held, and
     * PLAY / STOP were taken as paste / clear */
    if (id >= DN_K_PUSH_A && id < DN_K_PUSH_A + 8) {
        pc_push[id - DN_K_PUSH_A] = (unsigned char)((flags & 1) != 0);
        return 0;
    }
    if (id == DN_K_FUNC) {
        pc_func = (flags & 1) != 0;
        return 0;
    }
    if (id >= DN_K_PAGE0 && id < DN_K_PAGE0 + 6) {
        pc_page_held[id - DN_K_PAGE0] = (unsigned char)((flags & 1) && !(flags & 0x10));
        return 0;
    }
    if (!pc_live())
        return 0;
    if ((id == DN_K_REC || id == DN_K_STOP || id == DN_K_PLAY) &&
        pc_cur_kind(pc_view) == PC_KIND) {
        int held = pc_func || (flags & 2);
        for (n = 0; n < 6; n++)
            held |= pc_page_held[n];
        if (held) {
            if ((flags & 1) && !(flags & 0x10) && !(flags & 8))
                return pc_copypaste(id);
            return 1;                    /* its release and repeats: ours too */
        }
    }
    if (id == DN_K_YES && pc_func)
        return 0;                        /* FUNC + YES: the stock's (TEMP SAVE), 2.12 */
    if (id == DN_K_YES && pc_cur_kind(pc_view) == PC_KIND) {   /* the views */
        if ((flags & 1) && !(flags & 0x10) && !(flags & 8)) {
            if (pc_still()) {
                PC_POPUP("Animation off: SETTINGS");
            } else {
                static const char *const vn[3] = { "CONTROLS", "CURVE", "GRAPH" };
                pc_graph = (pc_graph + 1) % 3;
                PC_POPUP("View=%s", vn[pc_graph]);
                pc_ctr = 0;
                DN_INVALIDATE(pc_view);
            }
        }
        return 1;                        /* YES's other events on our page: ours too */
    }
    if (id != DN_K_LEFT && id != DN_K_RIGHT)
        return 0;
    if (!(flags & 1) || (flags & 0x10) || (flags & 8))
        return 0;                        /* presses only, no repeats */
    n = pc_nkinds(pc_view, &v);
    if (n < 1)
        return 0;
    i = *(int *)(pc_view + 144);
    if (i < 0 || i >= n)
        i = 0;
    *(int *)(pc_view + 144) = (i + (id == DN_K_RIGHT ? 1 : n - 1)) % n;
    DN_INVALIDATE(pc_view);
    return 1;
}

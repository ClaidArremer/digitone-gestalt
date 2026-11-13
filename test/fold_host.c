/* Host test for GESTALT 5.0's chain (fold.c into pump.c), block by block, as core calls the
 * insert owner. Prints levels and checks for clipping, integer overflow (with -fsanitize) and
 * clicks across settings changes. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "../src/fold.c"
#include "../src/pump.c"

#define FS 48000
static double rms(const double *x, int n) { double s = 0; for (int i = 0; i < n; i++) s += x[i] * x[i]; return sqrt(s / n); }
static double db(double v) { return 20 * log10(v > 1e-12 ? v : 1e-12); }

/* a 110 Hz FM-ish tone at -14 dBFS, and a kick every 500 ms at -8 dBFS peak */
static double in_gain = 1;
static double src(int n, int ch)
{
    double t = (double)n / FS, kt = fmod(t, 0.5);
    double tone = 0.2 * sin(2 * M_PI * 110 * t + 1.5 * sin(2 * M_PI * 220 * t));
    double kick = 0.4 * exp(-kt * 18) * sin(2 * M_PI * (45 + 120 * exp(-kt * 30)) * kt);
    return in_gain * (tone * (ch ? 0.9 : 1.0) + kick);
}

static int n_all;
static void run(const char *name, double secs, void (*setp)(int blk))
{
    int nb = (int)(secs * FS / 32), n = 0, over = 0, onb = 0;
    double *xi = malloc(sizeof(double) * nb * 32), *xo = malloc(sizeof(double) * nb * 32), last = 0, mj = 0;
    for (int b = 0; b < nb; b++) {
        int32_t in[64], out[64];
        if (setp) setp(b);
        digipump_meter = 1;
        digipump_owner_sync();
        for (int i = 0; i < 32; i++)
            for (int c = 0; c < 2; c++)
                in[2 * i + c] = (int32_t)lrint(src(n_all + n + i, c) * 2147483647.0 * 0.999);
        if (digipump_owner.on) { digipump_owner.render(out, in); onb++; }
        else memcpy(out, in, sizeof out);
        for (int i = 0; i < 32; i++) {
            double o = out[2 * i] / 2147483648.0;
            if (fabs(o) > 0.95) over++;
            xi[n + i] = in[2 * i] / 2147483648.0;
            xo[n + i] = o;
            if (n + i > 0 && fabs(o - last) > mj) mj = fabs(o - last);
            last = o;
        }
        n += 32;
    }
    n_all += n;
    int skip = n / 3;
    if (in_gain == 0) {                         /* silence in: the last second out, its largest */
        double mx = 0;
        for (int i = n - 48000; i < n; i++) if (fabs(xo[i]) > mx) mx = fabs(xo[i]);
        printf("  silence: the last second's largest output %.2e (%s)\n", mx, mx < 1e-6 ? "OK" : "NOT SILENT");
    }
    printf("%-36s in %6.1f dB  out %6.1f dB  >-0.45dBFS %5d  max step %.3f  blocks on %5d/%d  gr %3d\n",
           name, db(rms(xi + skip, n - skip)), db(rms(xo + skip, n - skip)), over, mj, onb, nb, digipump_gr);
    free(xi); free(xo);
}

static void set(int fold, int shape, int tone, int touch, int comp, int rat, int out)
{
    digifold_active = fold > 0; if (fold) digifold_amt = fold;
    digifold_shape = shape; digifold_tone = tone; digifold_touch = touch;
    digipump_on = comp > 0; if (comp) digipump_squash = comp;
    digipump_rat = rat; digipump_out = out;
}
static void p_off(int b) { (void)b; set(0, 0, 127, 64, 0, 0, 64); }
static void p_comp(int b) { (void)b; set(0, 0, 127, 64, 80, 0, 64); }
static void p_fold(int b) { (void)b; set(64, 0, 127, 64, 0, 0, 64); }
static void p_both(int b) { (void)b; set(80, 115, 60, 100, 80, 39, 64); }
static void p_crush(int b) { (void)b; set(127, 127, 0, 127, 127, 127, 127); }
static void p_crush2(int b) { (void)b; set(127, 60, 127, 0, 0, 0, 127); }
static void p_outonly(int b) { (void)b; set(0, 0, 127, 64, 0, 0, b & 64 ? 16 : 127); }
static void p_toggle(int b) { (void)b; set((b / 200) & 1 ? 70 : 0, 30, 127, 64, (b / 300) & 1 ? 80 : 0, 0, 64); }
static void p_sweep(int b) { set(1 + (b / 8) % 127, (b / 3) % 128, (b / 5) % 128, (b / 7) % 128, 80, 0, 64); }
static int lv_f, lv_s = 0, lv_t = 64;
static void p_lv(int b) { (void)b; set(lv_f, lv_s, 127, lv_t, 0, 0, 64); }

int main(int argc, char **argv)
{
    if (argc > 1 && argv[1][0] == 's') {         /* the shapes' levels, FOLD 64, at a normal level */
        static const char *nm[] = { "SINE", "TRI", "SQR", "STEP", "RIPL", "WRAP", "LEAN" };
        for (int i = 0; i < 7; i++)
            for (int f = 16; f <= 112; f += 48) {
                char b[40];
                lv_f = f; lv_s = fd_shape_pt[i];
                snprintf(b, sizeof b, "shape %s FOLD %d", nm[i], f);
                run(b, 1.0, p_lv);
            }
        return 0;
    }
    if (argc > 1 && argv[1][0] == 't') {         /* TOUCH: -64, off, +63 at FOLD 48 */
        static const int t[] = { 0, 64, 127 };
        for (int i = 0; i < 3; i++) {
            char b[40];
            lv_f = 48; lv_s = 0; lv_t = t[i];
            snprintf(b, sizeof b, "TOUCH %d", t[i] - 64);
            run(b, 2.0, p_lv);
        }
        return 0;
    }
    if (argc > 1) {                             /* levels: FOLD across its range, three input levels */
        static const double g[3] = { 0.25, 1, 2.5 };
        static const int f[] = { 1, 16, 32, 48, 64, 80, 96, 112, 127 };
        for (int j = 0; j < 3; j++)
            for (int i = 0; i < 9; i++) {
                char nm[40];
                in_gain = g[j]; lv_f = f[i];
                snprintf(nm, sizeof nm, "levels: in x%.2f FOLD %d", g[j], f[i]);
                run(nm, 1.0, p_lv);
            }
        return 0;
    }
    run("all off (no insert)", 1.0, p_off);
    run("COMP 80", 2.0, p_comp);
    run("FOLD 64 (COMP OFF)", 2.0, p_fold);
    run("FOLD 80 SHP 115 TONE 60 TCH+36 COMP", 2.0, p_both);
    run("worst: max fold, LEAN, TOUCH +63, LIMIT, OUT +6", 2.0, p_crush);
    run("max fold, STEP, TOUCH -64, OUT +6", 2.0, p_crush2);
    run("OUT only, jumping -24 / +6 dB", 2.0, p_outonly);
    run("toggling FOLD and COMP", 4.0, p_toggle);
    run("sweeping FOLD, SHAPE, TONE, TOUCH", 6.0, p_sweep);
    run("FOLD + COMP again", 1.0, p_both);
    in_gain = 0;
    run("then silence: must settle to 0", 3.0, p_both);
    in_gain = 1;
    run("all off again (fades out)", 1.0, p_off);
    return 0;
}

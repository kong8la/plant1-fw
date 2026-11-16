/* SPDX-License-Identifier: MIT
 * PLANT-1 M2 — presets, envelopes, an LFO, patch storage, and a screen that shows the sound.
 *
 * M1 proved the four generator slots. M2 gives the device something to play: eight patches,
 * an editable ADSR with its curve, an LFO with its shape, a patch that survives a power cycle,
 * and a panel that draws each slot's waveform and the envelope/LFO instead of only numbers. */
#include "fastmath.h"

/* ------------------------------------------------------------- constants --- */
#define SR 44100.0f
#define NV 6
#define NGEN 4
#define UNI_MAX 3
#define WT_N 256
#define NTAB 4

enum { G_OFF, G_ANALOG, G_WT, G_FM, G_NOISE };
enum { W_SAW, W_SQUARE, W_PULSE, W_SINE };
enum { F_OFF, F_LP, F_HP, F_BP, F_NOTCH };
enum { PAGE_GEN, PAGE_ENV, PAGE_LFO, PAGE_PRESET, NPAGES };
enum { LFO_SIN, LFO_TRI, LFO_SAW, LFO_SQR };
enum { LT_CUT, LT_TREM };

/* ---------------------------------------------------------------- patch --- */
typedef struct {
    uint8_t on, type, wave, table, ftype;
    int8_t tune;
    float level, pan, cut, res;
    float pwm, morph, ratio, index, fb, color;
} gen_t;

typedef struct {
    gen_t gen[NGEN];
    uint8_t uni;
    float detune, spread;
    float a_ms, d_ms, s, r_ms;
    uint8_t lfo_shape, lfo_tgt;
    float lfo_rate, lfo_amt;
    float vol;
} patch_t;

static patch_t P;
static uint8_t selected, page, preset_index, octave;

/* the eight patches */
#define G(on_, ty, wv, tb, ft, tn, lv, pn, cu, rs, pw, mo, ra, ix, fb, co) \
    {(uint8_t)(on_), (uint8_t)(ty), (uint8_t)(wv), (uint8_t)(tb), (uint8_t)(ft), (int8_t)(tn), \
     (lv), (pn), (cu), (rs), (pw), (mo), (ra), (ix), (fb), (co)}

static const patch_t PRESETS[8] = {
    /* 0 INIT: M0's FM voice */
    { { G(1, G_FM, W_SINE, 0, F_LP, 0, 0.80f, 0.0f, 12000.0f, 0.10f, 0.5f, 0.0f, 2.0f, 3.2f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SAW, 0, F_LP, 0, 0.70f, 0.0f, 8000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SQUARE, 0, F_LP, 0, 0.60f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      1, 14.0f, 0.6f, 4.0f, 260.0f, 0.55f, 220.0f, LFO_SIN, LT_CUT, 1.5f, 0.0f, 0.80f },
    /* 1 GLASS BELL */
    { { G(1, G_FM, W_SINE, 0, F_LP, 0, 0.80f, 0.0f, 14000.0f, 0.05f, 0.5f, 0.0f, 3.5f, 0.0f, 0.1f, 0.5f),
        G(1, G_FM, W_SINE, 0, F_LP, 12, 0.50f, 0.3f, 14000.0f, 0.05f, 0.5f, 0.0f, 7.0f, 0.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 9000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      1, 8.0f, 0.5f, 2.0f, 2200.0f, 0.0f, 2400.0f, LFO_SIN, LT_CUT, 0.8f, 0.0f, 0.80f },
    /* 2 FM BASS */
    { { G(1, G_FM, W_SINE, 0, F_LP, -12, 0.85f, 0.0f, 9000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 1.6f, 0.3f, 0.5f),
        G(1, G_FM, W_SINE, 0, F_LP, -12, 0.55f, 0.0f, 7000.0f, 0.10f, 0.5f, 0.0f, 2.0f, 0.6f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 8000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      1, 8.0f, 0.4f, 2.0f, 500.0f, 0.45f, 160.0f, LFO_SIN, LT_CUT, 0.5f, 0.0f, 0.85f },
    /* 3 SUPER SAW */
    { { G(1, G_ANALOG, W_SAW, 0, F_LP, 0, 0.80f, 0.0f, 9000.0f, 0.08f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(1, G_ANALOG, W_SAW, 0, F_LP, -12, 0.60f, -0.15f, 5000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SAW, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      3, 14.0f, 0.9f, 20.0f, 1200.0f, 0.80f, 500.0f, LFO_SIN, LT_CUT, 0.18f, 0.15f, 0.80f },
    /* 4 PHASE PAD */
    { { G(1, G_WT, W_SINE, 0, F_LP, 0, 0.70f, 0.0f, 3200.0f, 0.22f, 0.5f, 0.35f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(1, G_WT, W_SINE, 3, F_LP, 12, 0.40f, 0.3f, 4000.0f, 0.15f, 0.5f, 0.6f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      3, 12.0f, 0.8f, 600.0f, 2500.0f, 0.85f, 2200.0f, LFO_SIN, LT_CUT, 0.09f, 0.20f, 0.75f },
    /* 5 PLUCK */
    { { G(1, G_WT, W_SINE, 2, F_LP, 0, 0.80f, 0.0f, 2800.0f, 0.30f, 0.5f, 0.25f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(1, G_NOISE, W_SAW, 0, F_LP, 0, 0.35f, 0.2f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.3f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      2, 10.0f, 0.6f, 1.0f, 420.0f, 0.0f, 300.0f, LFO_SIN, LT_CUT, 2.0f, 0.0f, 0.80f },
    /* 6 ACID */
    { { G(1, G_ANALOG, W_SAW, 0, F_LP, -12, 0.85f, 0.0f, 420.0f, 0.75f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SAW, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      1, 6.0f, 0.3f, 2.0f, 320.0f, 0.20f, 140.0f, LFO_TRI, LT_CUT, 3.2f, 0.35f, 0.85f },
    /* 7 DARK CHOIR */
    { { G(1, G_WT, W_SINE, 3, F_LP, -12, 0.70f, 0.0f, 1500.0f, 0.25f, 0.5f, 0.25f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(1, G_WT, W_SINE, 3, F_LP, 0, 0.55f, 0.35f, 1900.0f, 0.20f, 0.5f, 0.55f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_ANALOG, W_SINE, 0, F_LP, 0, 0.50f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f),
        G(0, G_NOISE, W_SAW, 0, F_LP, 0, 0.40f, 0.0f, 6000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f) },
      2, 9.0f, 0.7f, 700.0f, 2000.0f, 0.80f, 2000.0f, LFO_SIN, LT_CUT, 0.16f, 0.12f, 0.75f }
};

static const char *const PRESET_NAME[8] = {"INIT", "GLASS BELL", "FM BASS", "SUPER SAW", "PHASE PAD", "PLUCK", "ACID", "DARK CHOIR"};

static void preset_load(uint8_t i)
{
    if (i >= 8) i = 0;
    P = PRESETS[i];
    preset_index = i;
}

/* --------------------------------------------------------------- wavetables --- */
static float wt[NTAB][2][WT_N];
static uint8_t ready;

static void tab_add(float *dst, int h, float amp)
{
    int i;
    float w = 6.2831853f * (float)h / (float)WT_N;
    for (i = 0; i < WT_N; i++)
        dst[i] += amp * fm_sinf(w * (float)i);
}

static void tab_norm(float *t)
{
    int i;
    float p = 0.0001f;
    for (i = 0; i < WT_N; i++)
        if (fm_fabsf(t[i]) > p) p = fm_fabsf(t[i]);
    for (i = 0; i < WT_N; i++) t[i] /= p * 1.05f;
}

static void tables_build(void)
{
    int i, h;
    for (i = 0; i < WT_N; i++)
        wt[0][0][i] = wt[0][1][i] = wt[1][0][i] = wt[1][1][i] =
        wt[2][0][i] = wt[2][1][i] = wt[3][0][i] = wt[3][1][i] = 0.0f;
    for (h = 1; h <= 32; h++) {
        tab_add(wt[0][0], h, 1.0f / (float)h);
        if (h & 1) tab_add(wt[0][1], h, 1.0f / (float)h);
    }
    tab_add(wt[1][0], 1, 1.0f);
    for (h = 1; h <= 16; h += 2)
        tab_add(wt[1][1], h, ((h % 4 == 1) ? 1.0f : -1.0f) / (float)(h * h));
    tab_add(wt[2][0], 1, 1.0f); tab_add(wt[2][0], 2, 0.5f);
    tab_add(wt[2][0], 4, 0.3f); tab_add(wt[2][0], 8, 0.18f);
    for (h = 1; h <= 16; h++) tab_add(wt[2][1], h, fm_exp2f(-0.55f * (float)h));
    for (h = 1; h <= 24; h++) {
        float f1 = fm_exp2f(-0.35f * (float)(h - 3) * (float)(h - 3));
        float f2 = 0.6f * fm_exp2f(-0.12f * (float)(h - 9) * (float)(h - 9));
        tab_add(wt[3][0], h, f1 + f2);
        tab_add(wt[3][1], h, 0.7f * f1 + f2);
    }
    for (i = 0; i < NTAB; i++) { tab_norm(wt[i][0]); tab_norm(wt[i][1]); }
    ready = 1;
}

/* ------------------------------------------------------------------ svf --- */
typedef struct { float ic1, ic2, a1, a2, a3, k; uint8_t mode; } svf_t;

static void svf_set(svf_t *f, float cut, float res)
{
    float g, k, a1;
    if (cut < 18.0f) cut = 18.0f;
    if (cut > SR * 0.45f) cut = SR * 0.45f;
    g = fm_tanf(FM_PI * cut / SR);
    k = 2.0f - 1.92f * (res > 0.99f ? 0.99f : res);
    f->k = k;
    a1 = 1.0f / (1.0f + g * (g + k));
    f->a1 = a1; f->a2 = g * a1; f->a3 = g * f->a2;
}

static float svf_run(svf_t *f, float x)
{
    float v3 = x - f->ic2;
    float v1 = f->a1 * f->ic1 + f->a2 * v3;
    float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    switch (f->mode) {
    case F_LP: return v2;
    case F_HP: return x - f->k * v1 - v2;
    case F_BP: return v1;
    case F_NOTCH: return x - f->k * v1;
    default: return x;
    }
}

/* ---------------------------------------------------------------- voices --- */
typedef struct {
    uint8_t active, note, stage;
    float vel, freq, target;
    uint32_t age;
    float phase[NGEN][UNI_MAX];
    float mphase[NGEN];
    float fbprev[NGEN];
    float pink[NGEN];
    float env, lfo_ph, lfo_val;
    svf_t filt[NGEN];
} voice_t;

static voice_t V[NV];
static uint32_t seed = 0x9E3779B9u, note_counter;

static float frand(void)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)(seed & 0x7FFFFFu) / 4194304.0f - 1.0f;
}

static float polyblep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

#define ENV_K(ms) (1.0f - fm_exp2f(-1.0f / ((ms) * 44.1f) * 1.4427f))

static float env_tick(voice_t *v)
{
    switch (v->stage) {
    case 1:
        v->env += (1.0f - v->env) * ENV_K(P.a_ms);
        if (v->env >= 0.999f) { v->env = 1.0f; v->stage = 2; }
        break;
    case 2:
        v->env += (P.s - v->env) * ENV_K(P.d_ms);
        if (v->env <= P.s + 0.001f) { v->env = P.s; v->stage = 3; }
        break;
    case 3:
        v->env = P.s;
        break;
    case 4:
        v->env += (0.0f - v->env) * ENV_K(P.r_ms);
        if (v->env < 0.0005f) { v->env = 0.0f; v->stage = 0; }
        break;
    default:
        v->env = 0.0f;
        break;
    }
    return v->env;
}

static float lfo_shape(uint8_t s, float ph)
{
    switch (s & 3) {
    case LFO_TRI: return 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
    case LFO_SAW: return ph * 2.0f - 1.0f;
    case LFO_SQR: return ph < 0.5f ? 1.0f : -1.0f;
    default: return fm_sinf(ph * FM_TWO_PI);
    }
}

static void voice_start(voice_t *v, uint8_t note, float vel)
{
    int g, u;
    v->active = 1;
    v->note = note;
    v->vel = vel;
    v->target = (float)note + (float)octave * 12.0f;
    v->freq = v->target;
    v->env = 0.0f;
    v->stage = 1;
    v->lfo_ph = 0.0f;
    v->lfo_val = 0.0f;
    v->age = ++note_counter;
    for (g = 0; g < NGEN; g++) {
        for (u = 0; u < UNI_MAX; u++)
            v->phase[g][u] = frand() * 0.5f + 0.5f;
        v->mphase[g] = 0.0f;
        v->fbprev[g] = 0.0f;
        v->pink[g] = 0.0f;
        v->filt[g].ic1 = v->filt[g].ic2 = 0.0f;
    }
}

static void voice_sample(voice_t *v, float *outL, float *outR)
{
    int g, u;
    float mixL = 0.0f, mixR = 0.0f;
    float base = 8.1757989f * fm_exp2f(v->freq * (1.0f / 12.0f));

    for (g = 0; g < NGEN; g++) {
        gen_t *G = &P.gen[g];
        float sumL = 0.0f, sumR = 0.0f;
        float freq;
        if (!G->on || G->type == G_OFF || G->level <= 0.0f) continue;
        freq = base * fm_exp2f((float)G->tune * (1.0f / 12.0f));
        for (u = 0; u < P.uni; u++) {
            float sp = P.uni == 1 ? 0.0f : ((float)u / (float)(P.uni - 1) * 2.0f - 1.0f);
            float fu = freq * fm_exp2f(sp * P.detune * (1.0f / 1200.0f));
            float dt = fu / SR;
            float ph = v->phase[g][u];
            float s = 0.0f;
            float pan = fm_clampf(G->pan + sp * P.spread, -1.0f, 1.0f);
            if (G->type == G_ANALOG) {
                if (G->wave == W_SAW) s = 2.0f * ph - 1.0f - polyblep(ph, dt);
                else if (G->wave == W_SQUARE) {
                    float b = ph + 0.5f;
                    if (b >= 1.0f) b -= 1.0f;
                    s = (ph < 0.5f ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(b, dt);
                } else if (G->wave == W_PULSE) {
                    float edge = ph + 1.0f - G->pwm;
                    if (edge >= 1.0f) edge -= 1.0f;
                    s = (ph < G->pwm ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(edge, dt);
                } else s = fm_sinf(ph * FM_TWO_PI);
                ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            } else if (G->type == G_WT) {
                const float *A = wt[G->table & (NTAB - 1)][0];
                const float *B = wt[G->table & (NTAB - 1)][1];
                float pos = ph * (float)WT_N;
                int i0 = (int)pos;
                float fr = pos - (float)i0;
                int i1 = (i0 + 1) & (WT_N - 1);
                float m0 = 1.0f - G->morph;
                float a0 = A[i0] * m0 + B[i0] * G->morph;
                float a1 = A[i1] * m0 + B[i1] * G->morph;
                s = a0 + (a1 - a0) * fr;
                ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            } else if (G->type == G_FM) {
                float mp = v->mphase[g];
                float modSig = fm_sinf(mp * FM_TWO_PI + v->fbprev[g] * G->fb * 0.6f);
                v->fbprev[g] = modSig;
                s = fm_sinf(ph * FM_TWO_PI + G->index * modSig);
                ph += dt; if (ph >= 1.0f) ph -= 1.0f;
                mp += dt * G->ratio;
                if (mp >= 1.0f) mp -= (float)(int)mp;
                v->mphase[g] = mp;
            } else {
                float nz = frand();
                v->pink[g] += (nz - v->pink[g]) * 0.08f;
                s = nz * G->color + v->pink[g] * (1.0f - G->color) * 2.2f;
                ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            }
            v->phase[g][u] = ph;
            sumL += s * (0.7071f + 0.2929f * pan);
            sumR += s * (0.7071f - 0.2929f * pan);
        }
        if (P.uni > 1) { sumL *= 0.7071f; sumR *= 0.7071f; }
        if (G->ftype) {
            float mono = (sumL + sumR) * 0.5f;
            float f = svf_run(&v->filt[g], mono);
            sumL += f - mono;
            sumR += f - mono;
        }
        mixL += sumL * G->level;
        mixR += sumR * G->level;
    }
    *outL = mixL;
    *outR = mixR;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    int v, g;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f * P.vol;
    float ldt = (float)n / SR;

    if (!ready) tables_build();

    for (v = 0; v < NV; v++) {
        voice_t *vv = &V[v];
        if (!vv->active) continue;
        vv->lfo_ph += ldt * P.lfo_rate;
        if (vv->lfo_ph >= 1.0f) vv->lfo_ph -= (float)(int)vv->lfo_ph;
        vv->lfo_val = lfo_shape(P.lfo_shape, vv->lfo_ph) * P.lfo_amt;
        for (g = 0; g < NGEN; g++) {
            gen_t *G = &P.gen[g];
            float cut = G->cut;
            if (!G->ftype) continue;
            if (P.lfo_tgt == LT_CUT)
                cut = fm_clampf(cut * fm_exp2f(vv->lfo_val * 3.0f), 20.0f, 18000.0f);
            vv->filt[g].mode = G->ftype;
            svf_set(&vv->filt[g], cut, G->res);
        }
    }

    for (i = 0; i < n; i++) {
        float L = 0.0f, R = 0.0f;
        for (v = 0; v < NV; v++) {
            voice_t *vv = &V[v];
            float e, sl, sr, trem = 1.0f;
            if (!vv->active) continue;
            if (vv->freq != vv->target) vv->freq += (vv->target - vv->freq) * 0.0015f;
            e = env_tick(vv);
            if (vv->stage == 0) { vv->active = 0; continue; }
            if (P.lfo_tgt == LT_TREM) trem = 1.0f + vv->lfo_val * 0.5f;
            voice_sample(vv, &sl, &sr);
            L += sl * e * vv->vel * 0.22f * trem;
            R += sr * e * vv->vel * 0.22f * trem;
        }
        lr[2 * i] = (int32_t)(L * gain);
        lr[2 * i + 1] = (int32_t)(R * gain);
    }
}

/* ------------------------------------------------------------------- UI --- */
static const char *const GEN_NAME[5] = {"OFF", "ANALOG", "WAVETABLE", "FM", "NOISE"};
static const char *const FTYPE_NAME[5] = {"OFF", "LP", "HP", "BP", "NOTCH"};
static const char *const PAGE_NAME[NPAGES] = {"GEN", "ENV", "LFO", "PATCH"};
static const char *const LFO_NAME[4] = {"SIN", "TRI", "SAW", "SQR"};

static void note_text(char *out, int32_t m)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t oct, k = 0;
    if (m < 0) { out[0] = '-'; out[1] = 0; return; }
    while (N[((m % 12) + 12) % 12][k]) { out[k] = N[((m % 12) + 12) % 12][k]; k++; }
    oct = m / 12 - 1;
    out[k] = (char)('0' + (oct < 0 ? 0 : oct));
    out[k + 1] = 0;
}

static void num_text(char *out, int32_t n);

static void hz_text(char *out, float hz)
{
    int32_t t = (int32_t)(hz * 10.0f + 0.5f), k = 0, j;
    char a[8], b[8];
    num_text(a, t / 10);
    num_text(b, t % 10);
    while (a[k]) { out[k] = a[k]; k++; }
    out[k++] = '.';
    j = 0;
    while (b[j]) { out[k++] = b[j]; j++; }
    out[k] = 0;
}

static void num_text(char *out, int32_t n)
{
    int32_t a = n < 0 ? -n : n;
    int k = 0, j;
    char t[12];
    do { t[k++] = (char)('0' + a % 10); a /= 10; } while (a);
    if (n < 0) t[k++] = '-';
    for (j = 0; j < k; j++) out[j] = t[k - 1 - j];
    out[k] = 0;
}

/* a generator's shape, for the little picture beside its name */
static float gen_shape(int g, float ph)
{
    gen_t *G = &P.gen[g];
    if (!G->on || G->type == G_OFF) return 0.0f;
    if (G->type == G_ANALOG) {
        if (G->wave == W_SAW) return 2.0f * ph - 1.0f;
        if (G->wave == W_SQUARE) return ph < 0.5f ? 0.9f : -0.9f;
        if (G->wave == W_PULSE) return ph < G->pwm ? 0.9f : -0.9f;
        return fm_sinf(ph * FM_TWO_PI);
    }
    if (G->type == G_WT) {
        int t = G->table & (NTAB - 1);
        int i0 = (int)(ph * (float)WT_N) & (WT_N - 1);
        return wt[t][0][i0] * (1.0f - G->morph) + wt[t][1][i0] * G->morph;
    }
    if (G->type == G_FM) {
        float m = fm_sinf(ph * FM_TWO_PI * G->ratio);
        return fm_sinf(ph * FM_TWO_PI + G->index * m * 0.4f);
    }
    return fm_sinf(ph * 97.0f) * 0.5f;
}

static void draw_slot_wave(int32_t x, int32_t y, int32_t w, int32_t h, int g, uint16_t c)
{
    int32_t i;
    for (i = 0; i < w; i++) {
        float ph = (float)i / (float)w;
        float v = gen_shape(g, ph);
        int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 1));
        if (yy < y) yy = y;
        if (yy > y + h - 2) yy = y + h - 2;
        cv_rect(x + i, yy, 1, 2, c);
    }
}

static void draw_env_graph(int32_t x, int32_t y, int32_t w, int32_t h)
{
    float a = P.a_ms, d = P.d_ms, r = P.r_ms, s = P.s;
    float total = a + d + r + 120.0f;
    int32_t xa = x + (int32_t)((float)w * a / total);
    int32_t xd = xa + (int32_t)((float)w * d / total);
    int32_t xs = xd + (int32_t)((float)w * 0.18f);
    int32_t xr = x + w - 2;
    int32_t i;
    cv_rect(x, y, w, 1, C_LINE);
    cv_rect(x, y, 1, h, C_LINE);
    for (i = x; i <= xa; i++)
        cv_rect(i, y + h - 1 - (int32_t)((float)(h - 2) * (float)(i - x) / (float)(xa - x + 1)), 1, 1, C_HI);
    for (i = xa; i <= xd; i++) {
        float k = (float)(i - xa) / (float)(xd - xa + 1);
        cv_rect(i, y + h - 1 - (int32_t)((float)(h - 2) * (1.0f - (1.0f - s) * (1.0f - fm_exp2f(-4.0f * k)))), 1, 1, C_HI);
    }
    for (i = xd; i <= xs; i++)
        cv_rect(i, y + h - 1 - (int32_t)((float)(h - 2) * s), 1, 1, C_HI);
    for (i = xs; i <= xr; i++) {
        float k = (float)(i - xs) / (float)(xr - xs + 1);
        cv_rect(i, y + h - 1 - (int32_t)((float)(h - 2) * s * (1.0f - fm_exp2f(-4.0f * k))), 1, 1, C_HI);
    }
}

static void draw_lfo_graph(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t i;
    for (i = 0; i < w; i++) {
        float v = lfo_shape(P.lfo_shape, (float)i / (float)w);
        int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 1));
        cv_rect(x + i, yy, 1, 1, C_HI);
    }
}

static void draw(void)
{
    char t[16], v[12];
    int i;
    int32_t n = -1, voices = 0;

    for (i = 0; i < NV; i++) if (V[i].active) { if (n < 0) n = V[i].note; voices++; }

    /* header: name and patch, note and voices */
    cv_begin(240, 30, C_BLACK);
    cv_text(8, 1, &FONT_B, "PLANT-1", C_WHITE);
    cv_text(8, 18, &FONT_T, PRESET_NAME[preset_index], C_GRAY);
    note_text(t, n);
    cv_text(198 - text_w(&FONT_B, t), 1, &FONT_B, t, n >= 0 ? C_HI : C_DIM);
    num_text(v, voices);
    cv_text(214, 18, &FONT_T, v, C_DIM);
    cv_blit(0, 0);

    /* the four slots: shape, type, filter, level */
    cv_begin(240, 96, C_BLACK);
    for (i = 0; i < NGEN; i++) {
        gen_t *G = &P.gen[i];
        int32_t y = i * 24;
        if (i == selected) cv_rect(2, y + 1, 236, 22, C_LINE);
        draw_slot_wave(8, y + 6, 42, 12, i, G->on ? C_HI : C_DIM);
        cv_text(56, y + 3, &FONT_XS, GEN_NAME[G->type], G->on ? C_HI : C_DIM);
        if (G->ftype) cv_text(116, y + 5, &FONT_T, FTYPE_NAME[G->ftype], C_GRAY);
        cv_rect(152, y + 11, 66, 4, C_LINE);
        cv_rect(152, y + 11, (int32_t)(G->level * 66.0f), 4, G->on ? C_HI : C_DIM);
    }
    cv_blit(0, 30);

    /* the page */
    cv_begin(240, 96, C_BLACK);
    switch (page) {
    case PAGE_GEN:
        cv_text(8, 2, &FONT_T, "LEVEL", C_GRAY);
        num_text(v, (int32_t)(P.gen[selected].level * 100.0f));
        cv_text(60, 0, &FONT_S, v, C_HI);
        cv_text(120, 2, &FONT_T, "CUT", C_GRAY);
        num_text(v, (int32_t)P.gen[selected].cut);
        cv_text(160, 0, &FONT_S, v, C_HI);
        cv_text(8, 24, &FONT_T, "TUNE", C_GRAY);
        num_text(v, (int32_t)P.gen[selected].tune);
        cv_text(60, 22, &FONT_S, v, C_HI);
        cv_text(120, 26, &FONT_T, "K1 TYPE K2 LVL", C_DIM);
        cv_text(8, 46, &FONT_T, "K3 PAN  K4 TUNE", C_DIM);
        num_text(v, selected + 1);
        cv_text(8, 64, &FONT_T, "SLOT", C_DIM);
        cv_text(38, 64, &FONT_T, v, C_GRAY);
        cv_text(52, 64, &FONT_T, "/4", C_DIM);
        num_text(v, octave);
        cv_text(96, 64, &FONT_T, "OCT", C_DIM);
        cv_text(126, 64, &FONT_T, v, C_GRAY);
        break;
    case PAGE_ENV:
        draw_env_graph(8, 2, 224, 42);
        cv_text(8, 48, &FONT_T, "A", C_GRAY);
        num_text(v, (int32_t)P.a_ms);
        cv_text(20, 48, &FONT_T, v, C_HI);
        cv_text(62, 48, &FONT_T, "D", C_GRAY);
        num_text(v, (int32_t)P.d_ms);
        cv_text(74, 48, &FONT_T, v, C_HI);
        cv_text(118, 48, &FONT_T, "S", C_GRAY);
        num_text(v, (int32_t)(P.s * 100.0f));
        cv_text(130, 48, &FONT_T, v, C_HI);
        cv_text(176, 48, &FONT_T, "R", C_GRAY);
        num_text(v, (int32_t)P.r_ms);
        cv_text(188, 48, &FONT_T, v, C_HI);
        cv_text(8, 64, &FONT_T, "K1 A   K2 D   K3 S   K4 R", C_DIM);
        break;
    case PAGE_LFO:
        draw_lfo_graph(8, 2, 224, 34);
        cv_text(8, 40, &FONT_S, LFO_NAME[P.lfo_shape & 3], C_HI);
        cv_text(62, 42, &FONT_T, "RATE", C_GRAY);
        hz_text(v, P.lfo_rate);
        cv_text(100, 40, &FONT_S, v, C_HI);
        cv_text(150, 42, &FONT_T, "AMT", C_GRAY);
        num_text(v, (int32_t)(P.lfo_amt * 100.0f));
        cv_text(186, 40, &FONT_S, v, C_HI);
        cv_text(8, 62, &FONT_T, P.lfo_tgt == LT_CUT ? "TARGET CUTOFF" : "TARGET TREMOLO", C_GRAY);
        cv_text(8, 76, &FONT_T, "K1 SHAPE K2 RATE K3 AMT K4 TGT", C_DIM);
        break;
    default:
        cv_text(8, 2, &FONT_S, PRESET_NAME[preset_index], C_HI);
        num_text(v, preset_index + 1);
        cv_text(8, 24, &FONT_T, "PATCH", C_GRAY);
        cv_text(54, 22, &FONT_S, v, C_HI);
        cv_text(76, 24, &FONT_T, "/ 8", C_DIM);
        cv_text(8, 44, &FONT_T, "K1 NEXT PRESET", C_DIM);
        cv_text(8, 60, &FONT_T, "SAVE writes it to flash", C_DIM);
        cv_text(8, 76, &FONT_T, "OCT+/- transpose", C_DIM);
        break;
    }
    cv_blit(0, 126);

    /* page tabs */
    cv_begin(240, 18, C_BLACK);
    for (i = 0; i < NPAGES; i++) {
        int32_t x = 10 + i * 56;
        if (i == page) {
            cv_rect(x - 4, 2, 50, 14, C_LINE);
            cv_text(x, 4, &FONT_T, PAGE_NAME[i], C_HI);
        } else {
            cv_text(x, 4, &FONT_T, PAGE_NAME[i], C_DIM);
        }
    }
    cv_blit(0, 222);
}

static void key_edges(uint32_t keys)
{
    static uint32_t prev;
    uint32_t on = keys & ~prev, off = prev & ~keys;
    int i, v;
    for (i = 0; i < NKEYS; i++) {
        if (on & (1u << i)) {
            int slot = -1;
            uint32_t oldest = 0xFFFFFFFFu;
            for (v = 0; v < NV; v++) if (!V[v].active) { slot = v; break; }
            if (slot < 0) {
                slot = 0;
                for (v = 0; v < NV; v++) if (V[v].age < oldest) { oldest = V[v].age; slot = v; }
            }
            voice_start(&V[slot], (uint8_t)(53 + i), 0.9f);
        }
        if (off & (1u << i)) {
            for (v = 0; v < NV; v++)
                if (V[v].active && V[v].note == (uint8_t)(53 + i)) {
                    if (V[v].stage != 0 && V[v].stage != 4) V[v].stage = 4;
                }
        }
    }
    prev = keys;
}

/* ---------------------------------------------------------------- storage --- */
#define SAVE_MAGIC 0x504C4E32u   /* "PLN2" */
typedef struct { uint32_t magic; uint16_t ver; uint16_t idx; patch_t p; } saved_t;

static void patch_save(void)
{
    saved_t s;
    s.magic = SAVE_MAGIC;
    s.ver = 2;
    s.idx = preset_index;
    s.p = P;
    (void)plat_store_save(OBJ_SETTINGS, &s, sizeof s);
}

static int patch_load(void)
{
    saved_t s;
    if (plat_store_load(OBJ_SETTINGS, &s, sizeof s) != (int)sizeof s) return 0;
    if (s.magic != SAVE_MAGIC || s.ver != 2 || s.idx >= 8) return 0;
    P = s.p;
    preset_index = s.idx;
    return 1;
}

void app_frame(void)
{
    static uint32_t btn_prev;
    uint32_t btns = plat_buttons(), keys = plat_keys();
    uint32_t pressed = btns & ~btn_prev;
    int32_t d;
    int i;

    btn_prev = btns;
    key_edges(keys);
    plat_lights(0, keys, 0, 0, 1);

    if (pressed & (1u << B_SEL)) selected = (uint8_t)((selected + 1) % NGEN);
    if (pressed & (1u << B_EDIT)) page = (uint8_t)((page + 1) % NPAGES);
    if (pressed & (1u << B_HOME)) { page = PAGE_GEN; selected = 0; }
    if (pressed & (1u << B_OCTUP)) { if (octave < 2) octave++; }
    if (pressed & (1u << B_OCTDN)) { if (octave > -2) octave--; }
    if (pressed & (1u << B_SAVE)) patch_save();

    d = plat_enc(EN_K1);
    if (d) {
        if (page == PAGE_GEN) {
            gen_t *G = &P.gen[selected];
            G->type = (uint8_t)(((G->type - 1 + (d > 0 ? 1 : 3)) % 4) + 1);
            G->on = 1;
            G->ftype = G->ftype ? G->ftype : F_LP;
        } else if (page == PAGE_ENV) {
            P.a_ms = fm_clampf(P.a_ms * fm_exp2f((float)d * 0.35f), 0.5f, 4000.0f);
        } else if (page == PAGE_LFO) {
            P.lfo_shape = (uint8_t)((P.lfo_shape + (d > 0 ? 1 : 3)) & 3);
        } else {
            preset_load((uint8_t)((preset_index + (d > 0 ? 1 : 7)) & 7));
        }
    }
    d = plat_enc(EN_K2);
    if (d) {
        if (page == PAGE_GEN) P.gen[selected].level = fm_clampf(P.gen[selected].level + (float)d * 0.02f, 0.0f, 1.0f);
        else if (page == PAGE_ENV) P.d_ms = fm_clampf(P.d_ms * fm_exp2f((float)d * 0.35f), 5.0f, 4000.0f);
        else if (page == PAGE_LFO) P.lfo_rate = fm_clampf(P.lfo_rate * fm_exp2f((float)d * 0.35f), 0.05f, 10.0f);
    }
    d = plat_enc(EN_K3);
    if (d) {
        if (page == PAGE_GEN) P.gen[selected].pan = fm_clampf(P.gen[selected].pan + (float)d * 0.05f, -1.0f, 1.0f);
        else if (page == PAGE_ENV) P.s = fm_clampf(P.s + (float)d * 0.03f, 0.0f, 1.0f);
        else if (page == PAGE_LFO) P.lfo_amt = fm_clampf(P.lfo_amt + (float)d * 0.05f, 0.0f, 1.0f);
    }
    d = plat_enc(EN_K4);
    if (d) {
        if (page == PAGE_GEN) P.gen[selected].tune = (int8_t)clamp((int32_t)P.gen[selected].tune + (d > 0 ? 1 : -1), -24, 24);
        else if (page == PAGE_ENV) P.r_ms = fm_clampf(P.r_ms * fm_exp2f((float)d * 0.35f), 5.0f, 6000.0f);
        else if (page == PAGE_LFO) P.lfo_tgt = (uint8_t)(P.lfo_tgt ? 0 : 1);
    }

    draw();
}

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }

void app_init(void)
{
    int i;
    tables_build();
    for (i = 0; i < NV; i++) { V[i].active = 0; V[i].stage = 0; }
    selected = 0; page = PAGE_GEN; octave = 0; preset_index = 0;
    preset_load(0);
    (void)patch_load();
}

void app_panic(void)
{
    int i;
    for (i = 0; i < NV; i++) { V[i].active = 0; V[i].stage = 0; }
}

void app_input_only(void) {}

void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    int i;
    if (!strcmp(what, "note")) {
        uint32_t k = plat_keys();
        *got = -1;
        for (i = 0; i < NKEYS; i++) if (k & (1u << i)) { *got = 53 + i; break; }
        return 1;
    }
    if (!strcmp(what, "voices")) {
        int c = 0;
        for (i = 0; i < NV; i++) if (V[i].active) c++;
        *got = c; return 1;
    }
    if (!strcmp(what, "sel")) { *got = selected; return 1; }
    if (!strcmp(what, "page")) { *got = page; return 1; }
    if (!strcmp(what, "gtype")) { *got = P.gen[selected].type; return 1; }
    if (!strcmp(what, "glevel")) { *got = (int)(P.gen[selected].level * 100.0f); return 1; }
    if (!strcmp(what, "uni")) { *got = P.uni; return 1; }
    if (!strcmp(what, "preset")) { *got = preset_index; return 1; }
    if (!strcmp(what, "filt")) { *got = P.gen[selected].ftype; return 1; }
    if (!strcmp(what, "genon")) { *got = P.gen[selected].on; return 1; }
    return 0;
}
#endif

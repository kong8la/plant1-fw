/* SPDX-License-Identifier: MIT
 * PLANT-1 M1 — four generator slots (analog, wavetable, FM, noise), a TPT filter per slot,
 * unison, six voices, and a three-page panel on the FM-1's encoders and buttons.
 *
 * A straight port of the first block of the browser engine, sized for the AC79.
 * No modulation matrix yet (that is M2). */
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
enum { PAGE_GEN, PAGE_FILTER, PAGE_VOICE, NPAGES };

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
    float vol;
} patch_t;

static patch_t P;
static uint8_t selected, page;
static int8_t octave;

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

    for (h = 1; h <= 32; h++) {                         /* 0: saw -> square */
        tab_add(wt[0][0], h, 1.0f / (float)h);
        if (h & 1) tab_add(wt[0][1], h, 1.0f / (float)h);
    }
    tab_add(wt[1][0], 1, 1.0f);                         /* 1: sine -> triangle */
    for (h = 1; h <= 16; h += 2)
        tab_add(wt[1][1], h, ((h % 4 == 1) ? 1.0f : -1.0f) / (float)(h * h));
    tab_add(wt[2][0], 1, 1.0f); tab_add(wt[2][0], 2, 0.5f);   /* 2: organ */
    tab_add(wt[2][0], 4, 0.3f); tab_add(wt[2][0], 8, 0.18f);
    for (h = 1; h <= 16; h++) tab_add(wt[2][1], h, fm_exp2f(-0.55f * (float)h));
    for (h = 1; h <= 24; h++) {                         /* 3: vocal-ish */
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
    float env;
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

static void voice_release(voice_t *v)
{
    if (v->stage != 0 && v->stage != 4) v->stage = 4;
}

/* one voice's sample, both channels */
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
                if (G->wave == W_SAW) {
                    s = 2.0f * ph - 1.0f - polyblep(ph, dt);
                } else if (G->wave == W_SQUARE) {
                    float b = ph + 0.5f;
                    if (b >= 1.0f) b -= 1.0f;
                    s = (ph < 0.5f ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(b, dt);
                } else if (G->wave == W_PULSE) {
                    float edge = ph + 1.0f - G->pwm;
                    if (edge >= 1.0f) edge -= 1.0f;
                    s = (ph < G->pwm ? 1.0f : -1.0f) + polyblep(ph, dt) - polyblep(edge, dt);
                } else {
                    s = fm_sinf(ph * FM_TWO_PI);
                }
                ph += dt;
                if (ph >= 1.0f) ph -= 1.0f;
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
                ph += dt;
                if (ph >= 1.0f) ph -= 1.0f;
            } else if (G->type == G_FM) {
                float mp = v->mphase[g];
                float modSig = fm_sinf(mp * FM_TWO_PI + v->fbprev[g] * G->fb * 0.6f);
                v->fbprev[g] = modSig;
                s = fm_sinf(ph * FM_TWO_PI + G->index * modSig);
                ph += dt;
                if (ph >= 1.0f) ph -= 1.0f;
                mp += dt * G->ratio;
                if (mp >= 1.0f) mp -= (float)(int)mp;
                v->mphase[g] = mp;
            } else {
                float nz = frand();
                v->pink[g] += (nz - v->pink[g]) * 0.08f;
                s = nz * G->color + v->pink[g] * (1.0f - G->color) * 2.2f;
                ph += dt;
                if (ph >= 1.0f) ph -= 1.0f;
            }
            v->phase[g][u] = ph;
            sumL += s * (0.7071f + 0.2929f * pan);
            sumR += s * (0.7071f - 0.2929f * pan);
        }
        if (P.uni > 1) { sumL *= 0.7071f; sumR *= 0.7071f; }
        if (G->ftype) {                      /* the slot's filter, on its mono sum */
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

/* the audio interrupt */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    int v, g;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f * P.vol;

    if (!ready) tables_build();
    for (v = 0; v < NV; v++)
        for (g = 0; g < NGEN; g++) {
            gen_t *G = &P.gen[g];
            if (!G->ftype) continue;
            V[v].filt[g].mode = G->ftype;
            svf_set(&V[v].filt[g], G->cut, G->res);
        }

    for (i = 0; i < n; i++) {
        float L = 0.0f, R = 0.0f;
        for (v = 0; v < NV; v++) {
            voice_t *vv = &V[v];
            float e, sl, sr;
            if (!vv->active) continue;
            if (vv->freq != vv->target) vv->freq += (vv->target - vv->freq) * 0.0015f;
            e = env_tick(vv);
            if (vv->stage == 0) { vv->active = 0; continue; }
            voice_sample(vv, &sl, &sr);
            L += sl * e * vv->vel * 0.22f;
            R += sr * e * vv->vel * 0.22f;
        }
        lr[2 * i] = (int32_t)(L * gain);
        lr[2 * i + 1] = (int32_t)(R * gain);
    }
}

/* ------------------------------------------------------------------- UI --- */
static const char *const GEN_NAME[5] = {"OFF", "ANALOG", "WAVETABLE", "FM", "NOISE"};
static const char *const FTYPE_NAME[5] = {"OFF", "LP", "HP", "BP", "NOTCH"};

static void note_text(char *out, int32_t m)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int32_t oct, k = 0;
    if (m < 0) { out[0] = '-'; out[1] = 0; return; }
    while (N[((m % 12) + 12) % 12][k]) {
        out[k] = N[((m % 12) + 12) % 12][k];
        k++;
    }
    oct = m / 12 - 1;
    out[k] = (char)('0' + (oct < 0 ? 0 : oct));
    out[k + 1] = 0;
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

static void draw(void)
{
    char t[12];
    int i;
    int32_t n = -1;
    for (i = 0; i < NV; i++) if (V[i].active) { n = V[i].note; break; }

    cv_begin(240, 32, C_BLACK);
    cv_text(10, 4, &FONT_B, "PLANT-1 M1", C_WHITE);
    note_text(t, n);
    cv_text(226 - text_w(&FONT_B, t), 4, &FONT_B, t, C_HI);
    cv_blit(0, 0);

    cv_begin(240, 150, C_BLACK);
    for (i = 0; i < NGEN; i++) {
        gen_t *G = &P.gen[i];
        char v[12];
        if (i == selected) cv_rect(4, 2 + i * 34, 232, 30, C_LINE);
        cv_text(10, 8 + i * 34, &FONT_S, GEN_NAME[G->type], G->on ? C_HI : C_DIM);
        num_text(v, (int32_t)(G->level * 100.0f));
        cv_text(200 - text_w(&FONT_S, v), 8 + i * 34, &FONT_S, v, G->on ? C_GRAY : C_DIM);
        if (G->ftype) cv_text(110, 8 + i * 34, &FONT_XS, FTYPE_NAME[G->ftype], C_DIM);
    }
    cv_blit(0, 32);

    cv_begin(240, 56, C_BLACK);
    {
        static const char *const PN[NPAGES] = {"GEN", "FILTER", "VOICE"};
        char v[12];
        cv_text(10, 2, &FONT_S, PN[page], C_WHITE);
        if (page == PAGE_GEN) {
            cv_text(60, 2, &FONT_S, GEN_NAME[P.gen[selected].type], C_GRAY);
            num_text(v, (int32_t)(P.gen[selected].level * 100.0f));
            cv_text(10, 20, &FONT_XS, "LVL", C_DIM);
            cv_text(38, 20, &FONT_XS, v, C_HI);
            num_text(v, (int32_t)P.gen[selected].cut);
            cv_text(90, 20, &FONT_XS, "CUT", C_DIM);
            cv_text(118, 20, &FONT_XS, v, C_HI);
            num_text(v, (int32_t)P.gen[selected].tune);
            cv_text(180, 20, &FONT_XS, "TUNE", C_DIM);
            cv_text(212, 20, &FONT_XS, v, C_HI);
        } else if (page == PAGE_FILTER) {
            cv_text(60, 2, &FONT_S, FTYPE_NAME[P.gen[selected].ftype], C_GRAY);
            num_text(v, (int32_t)P.gen[selected].cut);
            cv_text(10, 20, &FONT_XS, "CUT", C_DIM);
            cv_text(38, 20, &FONT_XS, v, C_HI);
            num_text(v, (int32_t)(P.gen[selected].res * 100.0f));
            cv_text(140, 20, &FONT_XS, "RES", C_DIM);
            cv_text(168, 20, &FONT_XS, v, C_HI);
        } else {
            num_text(v, P.uni);
            cv_text(10, 20, &FONT_XS, "UNI", C_DIM);
            cv_text(38, 20, &FONT_XS, v, C_HI);
            num_text(v, (int32_t)P.detune);
            cv_text(90, 20, &FONT_XS, "DET", C_DIM);
            cv_text(118, 20, &FONT_XS, v, C_HI);
            num_text(v, (int32_t)(P.spread * 100.0f));
            cv_text(180, 20, &FONT_XS, "SPR", C_DIM);
            cv_text(208, 20, &FONT_XS, v, C_HI);
        }
    }    cv_blit(0, 184);
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
            for (v = 0; v < NV; v++)
                if (!V[v].active) { slot = v; break; }
            if (slot < 0) {
                slot = 0;
                for (v = 0; v < NV; v++)
                    if (V[v].age < oldest) { oldest = V[v].age; slot = v; }
            }
            voice_start(&V[slot], (uint8_t)(53 + i), 0.9f);
        }
        if (off & (1u << i)) {
            for (v = 0; v < NV; v++)
                if (V[v].active && V[v].note == (uint8_t)(53 + i)) voice_release(&V[v]);
        }
    }
    prev = keys;
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
    if (pressed & (1u << B_SAVE)) {
        for (i = 0; i < NV; i++) V[i].active = 0;
    }

    d = plat_enc(EN_K1);
    if (d) {
        if (page == PAGE_GEN) {
            gen_t *G = &P.gen[selected];
            G->type = (uint8_t)(((G->type - 1 + (d > 0 ? 1 : 3)) % 4) + 1);
            G->on = 1;
            G->ftype = G->ftype ? G->ftype : F_LP;
        } else if (page == PAGE_FILTER) {
            P.gen[selected].ftype = (uint8_t)((P.gen[selected].ftype + (d > 0 ? 1 : 4)) % 5);
        } else {
            P.uni = (uint8_t)clamp((int32_t)P.uni + (d > 0 ? 1 : -1), 1, UNI_MAX);
        }
    }
    d = plat_enc(EN_K2);
    if (d) {
        if (page == PAGE_GEN) {
            float *v = &P.gen[selected].level;
            *v = fm_clampf(*v + (float)d * 0.02f, 0.0f, 1.0f);
        } else if (page == PAGE_FILTER) {
            float *v = &P.gen[selected].cut;
            *v = fm_clampf(*v * fm_exp2f((float)d * 0.3f), 20.0f, 18000.0f);
        } else {
            P.detune = fm_clampf(P.detune + (float)d * 2.0f, 0.0f, 60.0f);
        }
    }
    d = plat_enc(EN_K3);
    if (d) {
        if (page == PAGE_GEN) {
            float *v = &P.gen[selected].pan;
            *v = fm_clampf(*v + (float)d * 0.05f, -1.0f, 1.0f);
        } else if (page == PAGE_FILTER) {
            float *v = &P.gen[selected].res;
            *v = fm_clampf(*v + (float)d * 0.03f, 0.0f, 0.95f);
        } else {
            P.spread = fm_clampf(P.spread + (float)d * 0.05f, 0.0f, 1.0f);
        }
    }
    d = plat_enc(EN_K4);
    if (d && page == PAGE_GEN) {
        P.gen[selected].tune = (int8_t)clamp((int32_t)P.gen[selected].tune + (d > 0 ? 1 : -1), -24, 24);
    }

    draw();
}

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }

void app_init(void)
{
    int i;
    tables_build();
    for (i = 0; i < NV; i++) { V[i].active = 0; V[i].stage = 0; }
    P.gen[0] = (gen_t){1, G_FM, W_SINE, 0, F_LP, 0, 0.80f, 0.0f, 12000.0f, 0.10f, 0.5f, 0.0f, 2.0f, 3.2f, 0.0f, 0.5f};
    for (i = 1; i < NGEN; i++)
        P.gen[i] = (gen_t){0, G_ANALOG, W_SAW, 0, F_LP, 0, 0.70f, 0.0f, 8000.0f, 0.10f, 0.5f, 0.0f, 1.0f, 2.0f, 0.0f, 0.5f};
    P.uni = 1; P.detune = 14.0f; P.spread = 0.6f;
    P.a_ms = 4.0f; P.d_ms = 260.0f; P.s = 0.55f; P.r_ms = 220.0f;
    P.vol = 0.8f;
    selected = 0; page = PAGE_GEN; octave = 0;
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
    if (!strcmp(what, "genon")) { *got = P.gen[selected].on; return 1; }
    return 0;
}
#endif

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
enum { PAGE_GEN, PAGE_ENV, PAGE_LFO, PAGE_MOD, PAGE_FX, PAGE_ARP, PAGE_PRESET, NPAGES };
enum { LFO_SIN, LFO_TRI, LFO_SAW, LFO_SQR };
enum { LT_CUT, LT_TREM };

/* ---------------------------------------------------------------- patch --- */
typedef struct {
    uint8_t on, type, wave, table, ftype;
    int8_t tune;
    float level, pan, cut, res;
    float pwm, morph, ratio, index, fb, color;
} gen_t;

#define FX_MAX 5
enum { FX_OFF, FX_DRIVE, FX_CHORUS, FX_DELAY, FX_REVERB, FX_COMP };

typedef struct { uint8_t on, type; float p1, p2, p3; } fx_t;

/* modulation: a source, a destination (slot * 3 + parameter) and a depth */
#define NROUTE 4
enum { SRC_OFF, SRC_ENV1, SRC_ENV2, SRC_LFO1, SRC_LFO2, SRC_VELO };
enum { DST_LVL, DST_CUT, DST_PITCH };
typedef struct { uint8_t on, src, dst; float amt; } route_t;

typedef struct {
    gen_t gen[NGEN];
    uint8_t uni;
    float detune, spread;
    float a_ms, d_ms, s, r_ms;
    uint8_t lfo_shape, lfo_tgt;
    float lfo_rate, lfo_amt;
    float vol;
    fx_t fx[FX_MAX];
    float e2a, e2d, e2s, e2r;
    uint8_t lfo2_shape;
    float lfo2_rate, lfo2_amt;
    route_t route[NROUTE];
    uint8_t arp_on, arp_pattern, arp_rate, arp_oct;
    float arp_gate;
    uint16_t bpm;
} patch_t;

static patch_t P;
static uint8_t selected, page, preset_index, octave, fx_sel, env_sel, lfo_sel, route_sel;

/* the eight patches */
#define G(on_, ty, wv, tb, ft, tn, lv, pn, cu, rs, pw, mo, ra, ix, fb, co) \
    {(uint8_t)(on_), (uint8_t)(ty), (uint8_t)(wv), (uint8_t)(tb), (uint8_t)(ft), (int8_t)(tn), \
     (lv), (pn), (cu), (rs), (pw), (mo), (ra), (ix), (fb), (co)}

#define FXZ {0, FX_OFF, 0.0f, 0.0f, 0.0f}
#define FXN(t, a, b, c) {1, (t), (a), (b), (c)}
#define RZ {0, SRC_OFF, 0, 0.0f}
#define RN(s_, d_, a_) {1, (s_), (d_), (a_)}

static const patch_t PRESETS[32] = {
    /* 0 INIT */
    { { G(1, 3, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 3.2f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 4.0f, 260.0f, 0.55f, 220.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 1 GLASS BELL */
    { { G(1, 3, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 3.5f, 0.0f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 12, 0.4f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 7.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 2.0f, 2200.0f, 0.0f, 2400.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 2 MUSIC BOX */
    { { G(1, 3, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 4.0f, 1.2f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 12, 0.35f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 8.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 1200.0f, 0.0f, 1800.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 3 VIBES */
    { { G(1, 3, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.4f, 0.8f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 0, 0.3f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 4.8f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 6.0f, 1500.0f, 0.0f, 1500.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 4 CELESTA */
    { { G(1, 2, 3, 1, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 0, 0.3f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 6.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 900.0f, 0.0f, 1200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 5 TUBULAR */
    { { G(1, 3, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 1.0f, 0.5f, 0.4f, 0.5f),
        G(1, 3, 3, 0, 1, 0, 0.4f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.01f, 0.2f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 30.0f, 3000.0f, 0.3f, 3000.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 6 HARP */
    { { G(1, 2, 3, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.2f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 4, 0, 0, 1, 0, 0.15f, 0.0f, 9000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.8f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 800.0f, 0.0f, 900.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 7 ICE BELL */
    { { G(1, 3, 3, 0, 1, 0, 0.75f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 5.0f, 0.0f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 7, 0.3f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 10.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 2000.0f, 0.0f, 2200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 8 FM BASS */
    { { G(1, 3, 3, 0, 1, -12, 0.85f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 1.0f, 1.6f, 0.3f, 0.5f),
        G(1, 3, 3, 0, 1, -12, 0.5f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 0.6f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 2.0f, 500.0f, 0.45f, 160.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 9 SUB BASS */
    { { G(1, 1, 3, 0, 1, -12, 0.9f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 3, 0, 1, -24, 0.4f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 5.0f, 300.0f, 0.8f, 120.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 10 ACID */
    { { G(1, 1, 0, 0, 1, -12, 0.85f, 0.0f, 420.0f, 0.75f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 2.0f, 320.0f, 0.2f, 140.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 11 REESE */
    { { G(1, 1, 0, 0, 1, -12, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 0, 0, 1, -12, 0.6f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      2, 18.0f, 0.6f, 8.0f, 600.0f, 0.7f, 200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 12 GROWL */
    { { G(1, 3, 3, 0, 1, -12, 0.85f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 1.0f, 2.5f, 0.5f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 3.0f, 400.0f, 0.4f, 180.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 13 WUB */
    { { G(1, 2, 3, 0, 1, -12, 0.85f, 0.0f, 600.0f, 0.5f, 0.5f, 0.5f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 10.0f, 400.0f, 0.1f, 200.0f, 0, 0, 3.2f, 0.35f, 0.8f },
    /* 14 PLUCK BASS */
    { { G(1, 2, 3, 2, 1, -12, 0.85f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.25f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 420.0f, 0.0f, 300.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 15 SQUARE BASS */
    { { G(1, 1, 1, 0, 1, -12, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 1, 0, 1, 12, 0.25f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 3.0f, 350.0f, 0.5f, 150.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 16 SUPER SAW */
    { { G(1, 1, 0, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 0, 0, 1, -12, 0.6f, -0.15f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      3, 14.0f, 0.9f, 20.0f, 1200.0f, 0.8f, 500.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 17 HOOVER */
    { { G(1, 1, 0, 0, 1, 0, 0.6f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 0, 0, 1, 12, 0.35f, 0.2f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      3, 22.0f, 0.8f, 12.0f, 900.0f, 0.75f, 400.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 18 METAL */
    { { G(1, 3, 3, 0, 1, 0, 0.85f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 5.0f, 0.4f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1.0f, 700.0f, 0.4f, 400.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 19 SYNC */
    { { G(1, 1, 0, 0, 1, 0, 0.8f, 0.0f, 3000.0f, 0.4f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 4.0f, 700.0f, 0.7f, 350.0f, 0, 1, 0.35f, 0.1f, 0.8f },
    /* 20 FLUTE */
    { { G(1, 2, 3, 1, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 4, 0, 0, 1, 0, 0.12f, 0.0f, 6000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.9f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 80.0f, 600.0f, 0.9f, 400.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 21 WHISTLE */
    { { G(1, 1, 3, 0, 1, 0, 0.7f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 4, 0, 0, 1, 0, 0.08f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.95f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 40.0f, 500.0f, 0.9f, 300.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 22 SOLO */
    { { G(1, 3, 3, 0, 1, 0, 0.7f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 1.0f, 1.2f, 0.0f, 0.5f),
        G(1, 1, 0, 0, 1, 0, 0.45f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 10.0f, 800.0f, 0.7f, 300.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 23 BUZZ */
    { { G(1, 1, 2, 0, 1, 0, 0.8f, 0.0f, 8000.0f, 0.1f, 0.25f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      2, 9.0f, 0.5f, 3.0f, 500.0f, 0.6f, 250.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 24 PHASE PAD */
    { { G(1, 2, 3, 0, 1, 0, 0.7f, 0.0f, 3200.0f, 0.22f, 0.5f, 0.35f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 2, 3, 3, 1, 12, 0.4f, 0.3f, 4000.0f, 0.15f, 0.5f, 0.6f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      3, 12.0f, 0.8f, 600.0f, 2500.0f, 0.85f, 2200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 25 DARK CHOIR */
    { { G(1, 2, 3, 3, 1, -12, 0.7f, 0.0f, 1500.0f, 0.25f, 0.5f, 0.25f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 2, 3, 3, 1, 0, 0.5f, 0.35f, 1900.0f, 0.2f, 0.5f, 0.55f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      2, 9.0f, 0.7f, 600.0f, 1200.0f, 0.6f, 1200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 26 SWEEP */
    { { G(1, 1, 0, 0, 1, 0, 0.6f, 0.0f, 800.0f, 0.4f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 4, 0, 0, 1, 0, 0.25f, 0.0f, 2000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.6f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 800.0f, 2000.0f, 0.8f, 2000.0f, 0, 0, 0.12f, 0.5f, 0.8f },
    /* 27 STRINGS */
    { { G(1, 1, 0, 0, 1, 0, 0.65f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 1, 0, 0, 1, -12, 0.4f, -0.2f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      3, 11.0f, 0.9f, 300.0f, 1500.0f, 0.8f, 1200.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 28 DRONE */
    { { G(1, 3, 3, 0, 1, 0, 0.7f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 1.0f, 0.2f, 0.0f, 0.5f),
        G(1, 2, 3, 3, 1, -12, 0.45f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.4f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      2, 7.0f, 0.5f, 1500.0f, 3000.0f, 0.9f, 2500.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 29 WASH */
    { { G(1, 4, 0, 0, 1, 0, 0.3f, 0.0f, 3000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.4f),
        G(1, 2, 3, 1, 1, 0, 0.5f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.5f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 1000.0f, 3000.0f, 0.7f, 3000.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 30 GLASS PAD */
    { { G(1, 2, 3, 1, 1, 0, 0.7f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.4f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(1, 3, 3, 0, 1, 0, 0.35f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 0.4f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      2, 8.0f, 0.5f, 500.0f, 2000.0f, 0.75f, 2000.0f, 0, 0, 1.5f, 0.0f, 0.8f },
    /* 31 NOISE PAD */
    { { G(1, 4, 0, 0, 1, 0, 0.25f, 0.0f, 2200.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.3f),
        G(1, 1, 0, 0, 1, 0, 0.4f, 0.0f, 2500.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f),
        G(0, 1, 0, 0, 1, 0, 0.0f, 0.0f, 8000.0f, 0.1f, 0.5f, 0.0f, 2.0f, 2.0f, 0.0f, 0.5f) },
      1, 10.0f, 0.5f, 700.0f, 2500.0f, 0.6f, 2500.0f, 0, 0, 1.5f, 0.0f, 0.8f },
};

static const char *const PRESET_NAME[32] = {
    "INIT", "GLASS BELL", "MUSIC BOX", "VIBES",
    "CELESTA", "TUBULAR", "HARP", "ICE BELL",
    "FM BASS", "SUB BASS", "ACID", "REESE",
    "GROWL", "WUB", "PLUCK BASS", "SQUARE BASS",
    "SUPER SAW", "HOOVER", "METAL", "SYNC",
    "FLUTE", "WHISTLE", "SOLO", "BUZZ",
    "PHASE PAD", "DARK CHOIR", "SWEEP", "STRINGS",
    "DRONE", "WASH", "GLASS PAD", "NOISE PAD"
};

/* the chain each preset starts with: drive, chorus, delay, reverb, comp */
static const fx_t FX_PRESET[32][FX_MAX] = {
    { FXZ, FXZ, FXZ, FXZ, FXZ },
    { FXZ, FXZ, FXZ, FXN(4, 0.55f, 0.35f, 0.4f), FXZ },
    { FXZ, FXZ, FXZ, FXN(4, 0.5f, 0.4f, 0.35f), FXZ },
    { FXZ, FXN(2, 0.4f, 0.4f, 0.3f), FXZ, FXN(4, 0.45f, 0.4f, 0.3f), FXZ },
    { FXZ, FXZ, FXN(3, 0.55f, 0.25f, 0.2f), FXN(4, 0.45f, 0.4f, 0.3f), FXZ },
    { FXZ, FXZ, FXZ, FXN(4, 0.8f, 0.45f, 0.45f), FXZ },
    { FXZ, FXZ, FXN(3, 0.6f, 0.3f, 0.22f), FXN(4, 0.4f, 0.4f, 0.2f), FXZ },
    { FXZ, FXN(2, 0.3f, 0.5f, 0.3f), FXZ, FXN(4, 0.65f, 0.3f, 0.4f), FXZ },
    { FXN(1, 0.35f, 0.45f, 0.45f), FXZ, FXZ, FXZ, FXN(5, 0.4f, 0.5f, 0.2f) },
    { FXZ, FXZ, FXZ, FXZ, FXN(5, 0.5f, 0.6f, 0.2f) },
    { FXN(1, 0.55f, 0.5f, 0.55f), FXZ, FXN(3, 0.75f, 0.3f, 0.18f), FXZ, FXN(5, 0.3f, 0.5f, 0.2f) },
    { FXZ, FXN(2, 0.25f, 0.6f, 0.3f), FXZ, FXZ, FXN(5, 0.4f, 0.5f, 0.2f) },
    { FXN(1, 0.6f, 0.55f, 0.6f), FXZ, FXZ, FXZ, FXN(5, 0.35f, 0.6f, 0.25f) },
    { FXN(1, 0.5f, 0.5f, 0.5f), FXZ, FXZ, FXZ, FXN(5, 0.4f, 0.5f, 0.2f) },
    { FXZ, FXZ, FXN(3, 0.7f, 0.28f, 0.15f), FXZ, FXN(5, 0.3f, 0.5f, 0.2f) },
    { FXN(1, 0.3f, 0.5f, 0.4f), FXZ, FXZ, FXZ, FXN(5, 0.4f, 0.5f, 0.2f) },
    { FXZ, FXN(2, 0.45f, 0.55f, 0.45f), FXZ, FXN(4, 0.45f, 0.4f, 0.22f), FXZ },
    { FXN(1, 0.25f, 0.5f, 0.35f), FXN(2, 0.35f, 0.6f, 0.4f), FXZ, FXZ, FXZ },
    { FXN(1, 0.45f, 0.55f, 0.5f), FXZ, FXN(3, 0.6f, 0.3f, 0.2f), FXZ, FXZ },
    { FXN(1, 0.3f, 0.5f, 0.4f), FXZ, FXN(3, 0.5f, 0.3f, 0.2f), FXZ, FXZ },
    { FXZ, FXZ, FXZ, FXN(4, 0.4f, 0.4f, 0.25f), FXZ },
    { FXZ, FXZ, FXN(3, 0.5f, 0.3f, 0.2f), FXN(4, 0.35f, 0.4f, 0.2f), FXZ },
    { FXN(1, 0.3f, 0.5f, 0.35f), FXZ, FXN(3, 0.55f, 0.3f, 0.22f), FXN(4, 0.35f, 0.4f, 0.2f), FXZ },
    { FXN(1, 0.5f, 0.5f, 0.45f), FXZ, FXZ, FXZ, FXZ },
    { FXZ, FXN(2, 0.3f, 0.65f, 0.4f), FXN(3, 0.85f, 0.3f, 0.22f), FXN(4, 0.7f, 0.45f, 0.35f), FXZ },
    { FXZ, FXN(2, 0.22f, 0.6f, 0.3f), FXZ, FXN(4, 0.8f, 0.5f, 0.5f), FXZ },
    { FXZ, FXZ, FXZ, FXN(4, 0.8f, 0.5f, 0.45f), FXZ },
    { FXZ, FXN(2, 0.3f, 0.6f, 0.4f), FXZ, FXN(4, 0.6f, 0.45f, 0.35f), FXZ },
    { FXZ, FXN(2, 0.15f, 0.5f, 0.3f), FXZ, FXN(4, 0.85f, 0.5f, 0.5f), FXZ },
    { FXZ, FXZ, FXN(3, 0.8f, 0.35f, 0.3f), FXN(4, 0.8f, 0.45f, 0.45f), FXZ },
    { FXZ, FXN(2, 0.35f, 0.6f, 0.4f), FXZ, FXN(4, 0.7f, 0.4f, 0.4f), FXZ },
    { FXZ, FXN(2, 0.2f, 0.5f, 0.3f), FXZ, FXN(4, 0.85f, 0.5f, 0.5f), FXZ },
};

/* the matrix each preset starts with */
static const route_t ROUTE_PRESET[32][NROUTE] = {
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(2, 1, 0.5f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(2, 1, 0.45f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(2, 1, 0.7f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(2, 1, 0.6f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(4, 2, 0.02f), RN(2, 1, 0.2f), RZ, RZ },
    { RN(4, 1, 0.15f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(4, 2, 0.08f), RZ, RZ, RZ },
    { RN(3, 0, 0.1f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(4, 4, 0.3f), RN(2, 0, 0.3f), RZ, RZ },
    { RN(3, 0, 0.2f), RZ, RZ, RZ },
    { RN(2, 1, 0.6f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RN(4, 1, 0.2f), RZ, RZ, RZ },
    { RN(3, 4, 0.3f), RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
    { RZ, RZ, RZ, RZ },
};

/* the mod envelope: attack, decay, sustain, release */
static const float E2_PRESET[32][4] = {
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 400.0f, 1500.0f, 0.7f, 1500.0f },
    { 600.0f, 1200.0f, 0.6f, 1200.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
    { 4.0f, 300.0f, 0.5f, 300.0f },
};

static void preset_load(uint8_t i)
{
    if (i >= 32) i = 0;
    int k;
    P = PRESETS[i];
    for (k = 0; k < FX_MAX; k++) P.fx[k] = FX_PRESET[i][k];
    for (k = 0; k < NROUTE; k++) P.route[k] = ROUTE_PRESET[i][k];
    P.e2a = E2_PRESET[i][0]; P.e2d = E2_PRESET[i][1];
    P.e2s = E2_PRESET[i][2]; P.e2r = E2_PRESET[i][3];
    P.lfo2_shape = P.lfo_shape;
    P.lfo2_rate = 0.22f;
    P.lfo2_amt = 0.35f;
    P.arp_on = 0; P.arp_pattern = 0; P.arp_rate = 1; P.arp_oct = 1;
    P.arp_gate = 0.60f; P.bpm = 120;
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

/* ------------------------------------------------------------------ fx --- */
#define DLY_N 6144      /* ~139 ms at 44.1 kHz */
#define CHO_N 2048      /* ~46 ms */
#define RV_N 1200
#define RV_AP_N 512

static int16_t dly_b[2][DLY_N];
static int16_t cho_b[2][CHO_N];
static int16_t rv_b[2][4][RV_N];
static int16_t rv_ap_b[2][2][RV_AP_N];
static uint32_t dly_i, cho_i, rv_i[2][4], rv_api[2][2];
static float cho_ph, dly_lp[2], drv_lp[2], rv_lp[2], comp_env, comp_g;

static float fx_read(const int16_t *b, uint32_t n, uint32_t i, float delay)
{
    float pos = (float)i - delay;
    while (pos < 0.0f) pos += (float)n;
    while (pos >= (float)n) pos -= (float)n;
    {
        uint32_t i0 = (uint32_t)pos;
        float fr = pos - (float)i0;
        uint32_t i1 = (i0 + 1u) % n;
        return ((float)b[i0] + ((float)b[i1] - (float)b[i0]) * fr) * (1.0f / 32768.0f);
    }
}

static void fx_write(int16_t *b, uint32_t n, uint32_t i, float x)
{
    int32_t v = (int32_t)(fm_clampf(x, -1.0f, 1.0f) * 32767.0f);
    b[i % n] = (int16_t)v;
}

static float fx_drive(float x, fx_t *F)
{
    float g = 1.0f + F->p1 * 14.0f;
    float y = fm_tanhf(x * g);
    float tone = 0.02f + (1.0f - F->p2) * 0.5f;
    drv_lp[0] += (y - drv_lp[0]) * tone;
    return x + (drv_lp[0] - x) * F->p3;
}

static float fx_chorus(float x, int ch, fx_t *F)
{
    float rate = 0.05f + F->p1 * 4.95f;
    float d = 0.012f + 0.010f * F->p2 * (0.5f + 0.5f * fm_sinf(cho_ph * FM_TWO_PI + (float)ch * 0.5f));
    float y = fx_read(cho_b[ch], CHO_N, cho_i + (uint32_t)ch * 37u, d * SR);
    fx_write(cho_b[ch], CHO_N, cho_i + (uint32_t)ch * 37u, x);
    return x + y * F->p3;
}

static float fx_delay(float x, int ch, fx_t *F)
{
    float time = (10.0f + F->p1 * 120.0f) * 0.001f * SR;
    float y = fx_read(dly_b[ch], DLY_N, dly_i, time);
    fx_write(dly_b[ch], DLY_N, dly_i, x + y * F->p2);
    return x + y * F->p3;
}

static float fx_reverb(float x, int ch, fx_t *F)
{
    static const uint16_t CL[4] = {1116, 1145, 1188, 1219};
    static const uint16_t AP[2] = {225, 480};
    float g = 0.70f + F->p1 * 0.23f;
    float damp = 0.15f + F->p2 * 0.6f;
    float sum = 0.0f;
    int c;

    for (c = 0; c < 4; c++) {
        uint32_t i = rv_i[ch][c] % CL[c];
        float y = (float)rv_b[ch][c][i] * (1.0f / 32768.0f);
        rv_lp[ch] += (y - rv_lp[ch]) * (1.0f - damp);
        rv_b[ch][c][i] = (int16_t)(fm_clampf(x + rv_lp[ch] * g, -1.0f, 1.0f) * 32767.0f);
        sum += y;
        rv_i[ch][c]++;
    }
    for (c = 0; c < 2; c++) {
        uint32_t i = rv_api[ch][c] % AP[c];
        float b = (float)rv_ap_b[ch][c][i] * (1.0f / 32768.0f);
        float y = b - sum;
        rv_ap_b[ch][c][i] = (int16_t)(fm_clampf(sum + b * 0.5f, -1.0f, 1.0f) * 32767.0f);
        sum = y;
        rv_api[ch][c]++;
    }
    return x + sum * F->p3 * 0.6f;
}

static float fx_comp(float x, fx_t *F)
{
    float a = fm_fabsf(x);
    float thr = 0.02f + F->p1 * 0.5f;
    float ratio = 1.0f + F->p2 * 19.0f;
    comp_env += (a - comp_env) * 0.0008f;
    if (comp_env > thr) {
        float over = thr / (comp_env + 1e-6f);
        comp_g += (over - comp_g) * 0.05f;
        x = x * (1.0f - (1.0f - comp_g) * (1.0f - 1.0f / ratio)) * (1.0f + F->p3);
    } else {
        comp_g += (1.0f - comp_g) * 0.01f;
    }
    return x;
}

static void fx_run(float *L, float *R)
{
    int i;
    for (i = 0; i < FX_MAX; i++) {
        fx_t *F = &P.fx[i];
        if (!F->on || F->type == FX_OFF) continue;
        switch (F->type) {
        case FX_DRIVE:
            *L = fx_drive(*L, F);
            *R = fx_drive(*R, F);
            break;
        case FX_CHORUS:
            *L = fx_chorus(*L, 0, F);
            *R = fx_chorus(*R, 1, F);
            break;
        case FX_DELAY:
            *L = fx_delay(*L, 0, F);
            *R = fx_delay(*R, 1, F);
            break;
        case FX_REVERB:
            *L = fx_reverb(*L, 0, F);
            *R = fx_reverb(*R, 1, F);
            break;
        case FX_COMP:
            *L = fx_comp(*L, F);
            *R = fx_comp(*R, F);
            break;
        default:
            break;
        }
    }
    dly_i++;
    cho_i++;
    cho_ph += 0.0001f;
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
    float env, env2, lfo_ph, lfo2_ph, lfo_val, lfo2_val, mod[NGEN * 3];
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

static float env2_tick(voice_t *v)
{
    static uint8_t st;
    switch (v->env2 < 0.0f ? 0 : st) {
    default:
        if (v->stage == 0) { st = 0; v->env2 = 0.0f; return 0.0f; }
        st = 1;
        /* fall through */
    case 1:
        v->env2 += (1.0f - v->env2) * ENV_K(P.e2a);
        if (v->env2 >= 0.999f) { v->env2 = 1.0f; st = 2; }
        break;
    case 2:
        v->env2 += (P.e2s - v->env2) * ENV_K(P.e2d);
        if (v->env2 <= P.e2s + 0.001f) { v->env2 = P.e2s; st = 3; }
        break;
    case 3:
        v->env2 = P.e2s;
        break;
    case 4:
        v->env2 += (0.0f - v->env2) * ENV_K(P.e2r);
        if (v->env2 < 0.0005f) { v->env2 = 0.0f; st = 0; }
        break;
    }
    return v->env2;
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
    int g, u, i;
    v->active = 1;
    v->note = note;
    v->vel = vel;
    v->target = (float)note + (float)octave * 12.0f;
    v->freq = v->target;
    v->env = 0.0f;
    v->stage = 1;
    v->lfo_ph = 0.0f;
    v->lfo2_ph = 0.0f;
    v->lfo_val = 0.0f;
    v->lfo2_val = 0.0f;
    v->env2 = 0.0f;
    for (i = 0; i < NGEN * 3; i++) v->mod[i] = 0.0f;
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
        freq = base * fm_exp2f(((float)G->tune + v->mod[g * 3 + DST_PITCH] * 24.0f) * (1.0f / 12.0f));
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
        {
            float lvl = G->level * fm_clampf(1.0f + v->mod[g * 3 + DST_LVL], 0.0f, 2.0f);
            mixL += sumL * lvl;
            mixR += sumR * lvl;
        }
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
        int r;
        float src[6];
        vv->lfo_ph += ldt * P.lfo_rate;
        if (vv->lfo_ph >= 1.0f) vv->lfo_ph -= (float)(int)vv->lfo_ph;
        vv->lfo2_ph += ldt * P.lfo2_rate;
        if (vv->lfo2_ph >= 1.0f) vv->lfo2_ph -= (float)(int)vv->lfo2_ph;
        vv->lfo_val = lfo_shape(P.lfo_shape, vv->lfo_ph) * P.lfo_amt;
        vv->lfo2_val = lfo_shape(P.lfo2_shape, vv->lfo2_ph) * P.lfo2_amt;
        src[SRC_OFF] = 0.0f;
        src[SRC_ENV1] = vv->env;
        src[SRC_ENV2] = vv->env2;
        src[SRC_LFO1] = vv->lfo_val;
        src[SRC_LFO2] = vv->lfo2_val;
        src[SRC_VELO] = vv->vel;
        for (g = 0; g < NGEN * 3; g++) vv->mod[g] = 0.0f;
        for (r = 0; r < NROUTE; r++)
            if (P.route[r].on && P.route[r].dst < NGEN * 3)
                vv->mod[P.route[r].dst] += src[P.route[r].src & 7] * P.route[r].amt;
        for (g = 0; g < NGEN; g++) {
            gen_t *G = &P.gen[g];
            float cut = G->cut;
            if (!G->ftype) continue;
            if (P.lfo_tgt == LT_CUT)
                cut = fm_clampf(cut * fm_exp2f(vv->lfo_val * 3.0f), 20.0f, 18000.0f);
            cut = fm_clampf(cut * fm_exp2f(vv->mod[g * 3 + DST_CUT] * 4.0f), 20.0f, 18000.0f);
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
            (void)env2_tick(vv);
            if (vv->stage == 0) { vv->active = 0; continue; }
            if (P.lfo_tgt == LT_TREM) trem = 1.0f + vv->lfo_val * 0.5f;
            voice_sample(vv, &sl, &sr);
            L += sl * e * vv->vel * 0.22f * trem;
            R += sr * e * vv->vel * 0.22f * trem;
        }
        fx_run(&L, &R);
        lr[2 * i] = (int32_t)(L * gain);
        lr[2 * i + 1] = (int32_t)(R * gain);
    }
}

/* ---------------------------------------------------------------- storage --- */
#define SAVE_MAGIC 0x504C4E33u   /* "PLN3" */
#define NSLOT 4

typedef struct { uint8_t used, preset; patch_t p; } slot_t;
typedef struct { uint32_t magic; uint16_t ver; uint8_t cur; uint8_t pad; slot_t slot[NSLOT]; } bank_t;

static bank_t bank;
static uint8_t slot_index;

static void patch_save(void)
{
    bank.magic = SAVE_MAGIC;
    bank.ver = 5;
    bank.cur = slot_index;
    bank.slot[slot_index].used = 1;
    bank.slot[slot_index].preset = preset_index;
    bank.slot[slot_index].p = P;
    (void)plat_store_save(OBJ_SETTINGS, &bank, sizeof bank);
}

static int patch_load(void)
{
    int k;
    if (plat_store_load(OBJ_SETTINGS, &bank, sizeof bank) != (int)sizeof bank) return 0;
    if (bank.magic != SAVE_MAGIC || bank.ver != 5 || bank.cur >= NSLOT) return 0;
    slot_index = bank.cur;
    if (!bank.slot[slot_index].used) return 0;
    P = bank.slot[slot_index].p;
    preset_index = bank.slot[slot_index].preset;
    return 1;
}

/* PATCH page: K2 picks a slot, and loads it if something is stored there */
static void slot_pick(int d)
{
    slot_index = (uint8_t)((slot_index + (d > 0 ? 1 : NSLOT - 1)) % NSLOT);
    if (bank.magic == SAVE_MAGIC && bank.slot[slot_index].used) {
        P = bank.slot[slot_index].p;
        preset_index = bank.slot[slot_index].preset % 32;
    }
}

/* ------------------------------------------------------------------- UI --- */
static const char *const GEN_NAME[5] = {"OFF", "ANALOG", "WAVETABLE", "FM", "NOISE"};
static const char *const FTYPE_NAME[5] = {"OFF", "LP", "HP", "BP", "NOTCH"};
static const char *const PAGE_NAME[NPAGES] = {"GEN", "ENV", "LFO", "MOD", "FX", "ARP", "PATCH"};
static const char *const FX_NAME[6] = {"OFF", "DRIVE", "CHORUS", "DELAY", "REVERB", "COMP"};
static const char *const LFO_NAME[4] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const SRC_NAME[6] = {"--", "ENV1", "ENV2", "LFO1", "LFO2", "VELO"};
static const char *const DST_NAME[3] = {"LVL", "CUT", "PIT"};
static const char *const PAT_NAME[4] = {"UP", "DOWN", "UPDN", "RAND"};
static const char *const RATE_NAME[5] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};
static const char *const BANK_NAME[4] = {"BELLS", "BASS", "LEAD", "PADS"};

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

static void draw_env_graph(int32_t x, int32_t y, int32_t w, int32_t h, float a, float d, float s, float r)
{
    float total = a + d + r + 120.0f;
    int32_t xa = x + (int32_t)((float)w * a / total);
    int32_t xd = xa + (int32_t)((float)w * d / total);
    int32_t xs = xd + (int32_t)((float)w * 0.18f);
    int32_t xr = x + w - 2;
    int32_t i;
    if (a < 1.0f) a = 1.0f;
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
        cv_rect(152, y + 11, 66, 4, RGB(26, 36, 48));
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
    case PAGE_ENV: {
        float ea = env_sel ? P.e2a : P.a_ms, ed = env_sel ? P.e2d : P.d_ms;
        float es = env_sel ? P.e2s : P.s, er = env_sel ? P.e2r : P.r_ms;
        draw_env_graph(8, 2, 224, 42, ea, ed, es, er);
        cv_text(8, 48, &FONT_T, env_sel ? "MOD ENV" : "AMP ENV", C_WHITE);
        cv_text(8, 62, &FONT_T, "A", C_GRAY);
        num_text(v, (int32_t)ea);
        cv_text(20, 62, &FONT_T, v, C_HI);
        cv_text(62, 62, &FONT_T, "D", C_GRAY);
        num_text(v, (int32_t)ed);
        cv_text(74, 62, &FONT_T, v, C_HI);
        cv_text(118, 62, &FONT_T, "S", C_GRAY);
        num_text(v, (int32_t)(es * 100.0f));
        cv_text(130, 62, &FONT_T, v, C_HI);
        cv_text(176, 62, &FONT_T, "R", C_GRAY);
        num_text(v, (int32_t)er);
        cv_text(188, 62, &FONT_T, v, C_HI);
        cv_text(8, 78, &FONT_T, "ENV=AMP/MOD  K1-K4 A D S R", C_DIM);
        break;
    }
    case PAGE_LFO:
        draw_lfo_graph(8, 2, 224, 34);
        cv_text(8, 40, &FONT_S, LFO_NAME[(lfo_sel ? P.lfo2_shape : P.lfo_shape) & 3], C_HI);
        cv_text(62, 42, &FONT_T, "RATE", C_GRAY);
        hz_text(v, lfo_sel ? P.lfo2_rate : P.lfo_rate);
        cv_text(100, 40, &FONT_S, v, C_HI);
        cv_text(150, 42, &FONT_T, "AMT", C_GRAY);
        num_text(v, (int32_t)((lfo_sel ? P.lfo2_amt : P.lfo_amt) * 100.0f));
        cv_text(186, 40, &FONT_S, v, C_HI);
        cv_text(8, 60, &FONT_T, lfo_sel ? "LFO2 -> MOD MATRIX" : "LFO1 -> CUTOFF/TREM", C_GRAY);
        cv_text(8, 76, &FONT_T, "LFO=LFO1/2  K1 SHAPE K2 RATE K3 AMT", C_DIM);
        break;
    case PAGE_MOD: {
        int32_t y;
        int r;
        for (r = 0; r < NROUTE; r++) {
            route_t *R = &P.route[r];
            y = r * 14;
            if (r == route_sel) cv_rect(2, y, 236, 13, C_LINE);
            cv_text(6, y + 1, &FONT_T, SRC_NAME[R->src], R->on ? C_HI : C_DIM);
            cv_text(52, y + 1, &FONT_T, ">", C_DIM);
            num_text(v, R->dst / 3 + 1);
            cv_text(64, y + 1, &FONT_T, "G", C_GRAY);
            cv_text(74, y + 1, &FONT_T, v, R->on ? C_HI : C_DIM);
            cv_text(86, y + 1, &FONT_T, DST_NAME[R->dst % 3], R->on ? C_HI : C_DIM);
            cv_rect(140, y + 5, 80, 3, RGB(26, 36, 48));
            if (R->on)
                cv_rect(140, y + 5, (int32_t)(fm_clampf(fm_fabsf(R->amt), 0.0f, 1.0f) * 80.0f), 3, C_GRAY);
        }
        cv_text(8, 60, &FONT_T, "K1 ROUTE K2 SRC K3 DST K4 DEPTH", C_DIM);
        cv_text(8, 74, &FONT_T, "GLO=toggle route", C_DIM);
        break;
    }
    case PAGE_ARP:
        cv_text(8, 2, &FONT_S, P.arp_on ? "ARP ON" : "ARP OFF", P.arp_on ? C_HI : C_DIM);
        cv_text(8, 24, &FONT_T, "PATTERN", C_GRAY);
        cv_text(80, 22, &FONT_S, PAT_NAME[P.arp_pattern & 3], C_HI);
        cv_text(130, 24, &FONT_T, "RATE", C_GRAY);
        cv_text(172, 22, &FONT_S, RATE_NAME[P.arp_rate % 5], C_HI);
        cv_text(8, 42, &FONT_T, "OCT", C_GRAY);
        num_text(v, P.arp_oct ? P.arp_oct : 1);
        cv_text(40, 40, &FONT_S, v, C_HI);
        cv_text(80, 42, &FONT_T, "GATE", C_GRAY);
        num_text(v, (int32_t)(P.arp_gate * 100.0f));
        cv_text(130, 40, &FONT_S, v, C_HI);
        cv_text(8, 58, &FONT_T, "TEMPO", C_GRAY);
        num_text(v, P.bpm);
        cv_text(80, 56, &FONT_S, v, C_HI);
        cv_text(8, 74, &FONT_T, "K1 PAT K2 RATE K3 OCT K4 GATE", C_DIM);
        cv_text(8, 86, &FONT_T, "ARP=toggle  SEQ=tap tempo", C_DIM);
        break;
    case PAGE_FX:
        for (i = 0; i < FX_MAX; i++) {
            fx_t *F = &P.fx[i];
            int32_t y = i * 14;
            if (i == fx_sel) cv_rect(2, y, 236, 13, C_LINE);
            cv_text(8, y + 1, &FONT_T, FX_NAME[F->type], F->on ? C_HI : C_DIM);
            cv_text(96, y + 1, &FONT_T, F->on ? "ON" : "--", F->on ? C_HI : C_DIM);
            cv_rect(130, y + 5, 90, 3, C_LINE);
            if (F->on) cv_rect(130, y + 5, (int32_t)(fm_clampf(F->p3, 0.0f, 1.0f) * 90.0f), 3, C_GRAY);
        }
        {
            fx_t *F = &P.fx[fx_sel];
            num_text(v, (int32_t)(F->p1 * 100.0f));
            cv_text(8, 76, &FONT_T, "A", C_GRAY); cv_text(20, 76, &FONT_T, v, C_HI);
            num_text(v, (int32_t)(F->p2 * 100.0f));
            cv_text(70, 76, &FONT_T, "B", C_GRAY); cv_text(82, 76, &FONT_T, v, C_HI);
            num_text(v, (int32_t)(F->p3 * 100.0f));
            cv_text(132, 76, &FONT_T, "C", C_GRAY); cv_text(144, 76, &FONT_T, v, C_HI);
            cv_text(176, 76, &FONT_T, "FX=toggle", C_DIM);
        }
        break;
    default:
        cv_text(8, 2, &FONT_S, PRESET_NAME[preset_index], C_HI);
        num_text(v, preset_index + 1);
        cv_text(8, 24, &FONT_T, "PATCH", C_GRAY);
        cv_text(48, 22, &FONT_S, v, C_HI);
        cv_text(66, 24, &FONT_T, "/32", C_DIM);
        num_text(v, slot_index + 1);
        cv_text(104, 24, &FONT_T, "SLOT", C_GRAY);
        cv_text(146, 22, &FONT_S, v, C_HI);
        cv_text(164, 24, &FONT_T, "/4", C_DIM);
        num_text(v, preset_index / 8 + 1);
        cv_text(8, 44, &FONT_T, "BANK", C_GRAY);
        cv_text(44, 42, &FONT_S, v, C_HI);
        cv_text(58, 44, &FONT_T, "/4", C_DIM);
        cv_text(78, 44, &FONT_T, BANK_NAME[preset_index / 8], C_WHITE);
        cv_text(8, 60, &FONT_T, "K1 PRESET K2 SLOT K3 BANK", C_DIM);
        cv_text(8, 76, &FONT_T, slot_index < NSLOT && bank.slot[slot_index].used ? "SAVE overwrite  OCT transpose" : "SAVE store  OCT transpose", C_DIM);
        break;
    }
    cv_blit(0, 126);

    /* page tabs */
    cv_begin(240, 18, C_BLACK);
    for (i = 0; i < NPAGES; i++) {
        int32_t x = i * 34;
        if (i == page) {
            cv_rect(x + 1, 2, 33, 14, C_LINE);
            cv_text(x + 3, 4, &FONT_T, PAGE_NAME[i], C_HI);
        } else {
            cv_text(x + 3, 4, &FONT_T, PAGE_NAME[i], C_DIM);
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

/* ----------------------------------------------------------------- arp --- */
static uint8_t arp_voice, arp_step, arp_held;

static uint32_t arp_ms(void)
{
    static const uint16_t base[5] = {500, 250, 166, 125, 83};
    uint32_t bpm = P.bpm ? P.bpm : 120u;
    uint32_t ms = (uint32_t)base[P.arp_rate % 5] * 120u / bpm;
    return ms ? ms : 1u;
}

static void arp_rel(uint8_t v)
{
    if (V[v].active && V[v].stage != 0 && V[v].stage != 4) V[v].stage = 4;
}

static void arp_reset(void)
{
    int v;
    for (v = 0; v < NV; v++)
        arp_rel((uint8_t)v);
    arp_held = 0;
}

static int32_t arp_note(const int32_t *held, int nh, uint32_t step)
{
    int oct = P.arp_oct ? P.arp_oct : 1;
    int total = nh * oct;
    int idx;
    switch (P.arp_pattern & 3) {
    case 1: idx = total - 1 - (int)(step % (uint32_t)total); break;
    case 2: {
        int per = total > 1 ? (total - 1) * 2 : 1;
        int k = (int)(step % (uint32_t)per);
        idx = k < total ? k : per - k;
        break;
    }
    case 3: idx = (int)((frand() * 0.5f + 0.5f) * (float)total); if (idx >= total) idx = total - 1; if (idx < 0) idx = 0; break;
    default: idx = (int)(step % (uint32_t)total); break;
    }
    return held[idx % nh] + 12 * (idx / nh);
}

void app_arp_frame(uint32_t keys, uint32_t now)
{
    int32_t held[NKEYS];
    int nh = 0, i;
    static uint32_t off_at, next_at;

    for (i = 0; i < NKEYS; i++)
        if (keys & (1u << i)) held[nh++] = 53 + i;
    if (nh != arp_held) {
        arp_held = (uint8_t)nh;
        arp_step = 0;
        next_at = now;
        off_at = 0;
        if (!nh) arp_rel(arp_voice);
    }
    if (off_at && now >= off_at) {
        arp_rel(arp_voice);
        off_at = 0;
    }
    if (!nh) return;
    if (now >= next_at) {
        int32_t n = arp_note(held, nh, arp_step);
        arp_rel(arp_voice);
        arp_voice = (uint8_t)((arp_voice + 1) % NV);
        voice_start(&V[arp_voice], (uint8_t)n, 0.9f);
        off_at = now + (uint32_t)((float)arp_ms() * fm_clampf(P.arp_gate, 0.05f, 0.95f));
        next_at = now + arp_ms();
        arp_step++;
    }
}

/* ---------------------------------------------------------------- lights --- */
#define ANIM_NONE 0
#define ANIM_SWEEP 1
#define ANIM_BLINK 2

static float key_glow[NKEYS];
static uint32_t anim_t0, anim_kind;

static const uint8_t PAGE_BTN[NPAGES] = {B_SEL, B_ENV, B_LFO, B_GLO, B_FX, B_ARP, B_SAVE};

static void anim_start(uint8_t kind, uint32_t now)
{
    anim_kind = kind;
    anim_t0 = now;
}

/* called once a frame: the held keys are bright, released ones fade, an idle scanner
 * walks the keys, a preset load sweeps them, a page change blinks its button. */
static void lights_frame(uint32_t keys, uint32_t pressed, uint32_t now)
{
    uint32_t bright_k = keys, dim_k = 0, bright_b = pressed, dim_b = 0;
    uint32_t step = now / 70u;
    int32_t pos = (int32_t)(step % (uint32_t)(NKEYS + 10)) - 5;
    int i;

    for (i = 0; i < NKEYS; i++) {
        int32_t d;
        if (keys & (1u << i)) key_glow[i] = 1.0f;
        else key_glow[i] *= 0.90f;
        if (key_glow[i] > 0.06f) dim_k |= 1u << i;
        d = i - pos;
        if (d < 0) d = -d;
        if (d < 4) dim_k |= 1u << i;               /* the idle scanner */
    }

    if (anim_kind == ANIM_SWEEP) {
        uint32_t dt = now - anim_t0;
        if (dt < 560u) {
            int32_t upto = (int32_t)((float)NKEYS * (float)dt / 300.0f);
            for (i = 0; i < upto && i < NKEYS; i++) bright_k |= 1u << i;
        } else anim_kind = ANIM_NONE;
    } else if (anim_kind == ANIM_BLINK) {
        uint32_t dt = now - anim_t0;
        if (dt < 420u) {
            if ((dt / 90u) & 1u) bright_b |= 1u << PAGE_BTN[page];
        } else anim_kind = ANIM_NONE;
    }

    dim_b |= 1u << PAGE_BTN[page];                 /* where we are */
    if (octave > 0) dim_b |= 1u << B_OCTUP;
    if (octave < 0) dim_b |= 1u << B_OCTDN;

    plat_lights(bright_b, bright_k, dim_b, dim_k, 2);
}

void app_frame(void)
{
    static uint32_t btn_prev;
    uint32_t btns = plat_buttons(), keys = plat_keys();
    uint32_t pressed = btns & ~btn_prev;
    int32_t d;
    int i;

    uint32_t now = plat_ms();
    btn_prev = btns;
    if (P.arp_on) app_arp_frame(keys, now);
    else key_edges(keys);
    plat_lights(0, keys, 0, 0, 1);

    if (pressed & (1u << B_SEL)) selected = (uint8_t)((selected + 1) % NGEN);
    if (pressed & (1u << B_EDIT)) { page = (uint8_t)((page + 1) % NPAGES); anim_start(ANIM_BLINK, now); }
    if (pressed & (1u << B_HOME)) { page = PAGE_GEN; selected = 0; anim_start(ANIM_BLINK, now); }
    if (pressed & (1u << B_OCTUP)) { if (octave < 2) octave++; }
    if (pressed & (1u << B_OCTDN)) { if (octave > -2) octave--; }
    if (pressed & (1u << B_SAVE)) { patch_save(); anim_start(ANIM_SWEEP, now); }
    if (pressed & (1u << B_ARP)) {
        P.arp_on = (uint8_t)!P.arp_on;
        if (!P.arp_on) arp_reset();
        anim_start(ANIM_BLINK, now);
    }
    if (pressed & (1u << B_SEQ)) {
        static uint32_t last_tap;
        if (last_tap && now - last_tap < 2000u) {
            uint32_t ms = now - last_tap;
            int32_t bpm = (int32_t)(60000u / (ms ? ms : 1u));
            P.bpm = (uint16_t)clamp(bpm, 60, 200);
            anim_start(ANIM_BLINK, now);
        }
        last_tap = now;
    }
    if ((pressed & (1u << B_ENV)) && page == PAGE_ENV) env_sel = (uint8_t)!env_sel;
    if ((pressed & (1u << B_LFO)) && page == PAGE_LFO) lfo_sel = (uint8_t)!lfo_sel;
    if ((pressed & (1u << B_FX)) && page == PAGE_FX) {
        fx_t *F = &P.fx[fx_sel];
        F->on = (uint8_t)!F->on;
        if (F->on && F->type == FX_OFF) F->type = FX_DRIVE;
    }
    if ((pressed & (1u << B_GLO)) && page == PAGE_MOD) {
        route_t *R = &P.route[route_sel];
        R->on = (uint8_t)!R->on;
        if (R->on && R->src == SRC_OFF) { R->src = SRC_ENV2; R->dst = DST_CUT; R->amt = 0.30f; }
    }

    d = plat_enc(EN_K1);
    if (d) {
        switch (page) {
        case PAGE_GEN: {
            gen_t *G = &P.gen[selected];
            G->type = (uint8_t)(((G->type - 1 + (d > 0 ? 1 : 3)) % 4) + 1);
            G->on = 1;
            G->ftype = G->ftype ? G->ftype : F_LP;
            break;
        }
        case PAGE_ENV:
            if (env_sel) P.e2a = fm_clampf(P.e2a * fm_exp2f((float)d * 0.35f), 0.5f, 4000.0f);
            else P.a_ms = fm_clampf(P.a_ms * fm_exp2f((float)d * 0.35f), 0.5f, 4000.0f);
            break;
        case PAGE_LFO:
            if (lfo_sel) P.lfo2_shape = (uint8_t)((P.lfo2_shape + (d > 0 ? 1 : 3)) & 3);
            else P.lfo_shape = (uint8_t)((P.lfo_shape + (d > 0 ? 1 : 3)) & 3);
            break;
        case PAGE_MOD:
            route_sel = (uint8_t)((route_sel + (d > 0 ? 1 : NROUTE - 1)) % NROUTE);
            break;
        case PAGE_FX:
            fx_sel = (uint8_t)((fx_sel + (d > 0 ? 1 : FX_MAX - 1)) % FX_MAX);
            break;
        case PAGE_ARP:
            P.arp_pattern = (uint8_t)((P.arp_pattern + (d > 0 ? 1 : 3)) & 3);
            break;
        default:
            preset_load((uint8_t)((preset_index + (d > 0 ? 1 : 31)) % 32));
            anim_start(ANIM_SWEEP, now);
            break;
        }
    }
    d = plat_enc(EN_K2);
    if (d) {
        switch (page) {
        case PAGE_GEN:
            P.gen[selected].level = fm_clampf(P.gen[selected].level + (float)d * 0.02f, 0.0f, 1.0f);
            break;
        case PAGE_ENV:
            if (env_sel) P.e2d = fm_clampf(P.e2d * fm_exp2f((float)d * 0.35f), 5.0f, 4000.0f);
            else P.d_ms = fm_clampf(P.d_ms * fm_exp2f((float)d * 0.35f), 5.0f, 4000.0f);
            break;
        case PAGE_LFO:
            if (lfo_sel) P.lfo2_rate = fm_clampf(P.lfo2_rate * fm_exp2f((float)d * 0.35f), 0.05f, 10.0f);
            else P.lfo_rate = fm_clampf(P.lfo_rate * fm_exp2f((float)d * 0.35f), 0.05f, 10.0f);
            break;
        case PAGE_MOD: {
            route_t *R = &P.route[route_sel];
            R->src = (uint8_t)((R->src + (d > 0 ? 1 : 5)) % 6);
            if (R->src && !R->on) { R->on = 1; R->amt = 0.30f; }
            break;
        }
        case PAGE_FX: {
            fx_t *F = &P.fx[fx_sel];
            if (!F->on) { F->on = 1; if (F->type == FX_OFF) F->type = FX_DRIVE; }
            F->p1 = fm_clampf(F->p1 + (float)d * 0.02f, 0.0f, 1.0f);
            break;
        }
        case PAGE_ARP:
            P.arp_rate = (uint8_t)((P.arp_rate + (d > 0 ? 1 : 4)) % 5);
            break;
        default:
            slot_pick(d > 0 ? 1 : -1);
            break;
        }
    }
    d = plat_enc(EN_K3);
    if (d) {
        switch (page) {
        case PAGE_GEN:
            P.gen[selected].pan = fm_clampf(P.gen[selected].pan + (float)d * 0.05f, -1.0f, 1.0f);
            break;
        case PAGE_ENV:
            if (env_sel) P.e2s = fm_clampf(P.e2s + (float)d * 0.03f, 0.0f, 1.0f);
            else P.s = fm_clampf(P.s + (float)d * 0.03f, 0.0f, 1.0f);
            break;
        case PAGE_LFO:
            if (lfo_sel) P.lfo2_amt = fm_clampf(P.lfo2_amt + (float)d * 0.05f, 0.0f, 1.0f);
            else P.lfo_amt = fm_clampf(P.lfo_amt + (float)d * 0.05f, 0.0f, 1.0f);
            break;
        case PAGE_MOD: {
            route_t *R = &P.route[route_sel];
            R->dst = (uint8_t)((R->dst + (d > 0 ? 1 : NGEN * 3 - 1)) % (NGEN * 3));
            break;
        }
        case PAGE_PRESET:
            preset_load((uint8_t)((preset_index + (d > 0 ? 8 : 24)) % 32));
            anim_start(ANIM_SWEEP, now);
            break;
        case PAGE_FX:
            P.fx[fx_sel].p2 = fm_clampf(P.fx[fx_sel].p2 + (float)d * 0.02f, 0.0f, 1.0f);
            break;
        case PAGE_ARP:
            P.arp_oct = (uint8_t)clamp((int32_t)P.arp_oct + (d > 0 ? 1 : -1), 1, 3);
            break;
        }
    }
    d = plat_enc(EN_K4);
    if (d) {
        switch (page) {
        case PAGE_GEN:
            P.gen[selected].tune = (int8_t)clamp((int32_t)P.gen[selected].tune + (d > 0 ? 1 : -1), -24, 24);
            break;
        case PAGE_ENV:
            if (env_sel) P.e2r = fm_clampf(P.e2r * fm_exp2f((float)d * 0.35f), 5.0f, 6000.0f);
            else P.r_ms = fm_clampf(P.r_ms * fm_exp2f((float)d * 0.35f), 5.0f, 6000.0f);
            break;
        case PAGE_LFO:
            if (!lfo_sel) P.lfo_tgt = (uint8_t)(P.lfo_tgt ? 0 : 1);
            break;
        case PAGE_MOD: {
            route_t *R = &P.route[route_sel];
            R->amt = fm_clampf(R->amt + (float)d * 0.05f, -1.0f, 1.0f);
            R->on = (uint8_t)(R->amt != 0.0f);
            break;
        }
        case PAGE_FX:
            P.fx[fx_sel].p3 = fm_clampf(P.fx[fx_sel].p3 + (float)d * 0.02f, 0.0f, 1.0f);
            break;
        case PAGE_ARP:
            P.arp_gate = fm_clampf(P.arp_gate + (float)d * 0.02f, 0.05f, 0.95f);
            break;
        }
    }

    lights_frame(keys, pressed, now);
    draw();
}

void app_ui_init(void) { lcd_fill(0, 0, 240, 240, C_BLACK); }

void app_init(void)
{
    int i;
    tables_build();
    for (i = 0; i < NV; i++) { V[i].active = 0; V[i].stage = 0; }
    selected = 0; page = PAGE_GEN; octave = 0; preset_index = 0;
    bank.magic = 0;
    slot_index = 0;
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
    if (!strcmp(what, "fxon")) { *got = P.fx[fx_sel].on; return 1; }
    if (!strcmp(what, "fxtype")) { *got = P.fx[fx_sel].type; return 1; }
    if (!strcmp(what, "fxsel")) { *got = fx_sel; return 1; }
    if (!strcmp(what, "slot")) { *got = slot_index; return 1; }
    if (!strcmp(what, "routeon")) { *got = P.route[route_sel].on; return 1; }
    if (!strcmp(what, "routesrc")) { *got = P.route[route_sel].src; return 1; }
    if (!strcmp(what, "routedst")) { *got = P.route[route_sel].dst; return 1; }
    if (!strcmp(what, "envsel")) { *got = env_sel; return 1; }
    if (!strcmp(what, "lfosel")) { *got = lfo_sel; return 1; }
    if (!strcmp(what, "arp")) { *got = P.arp_on; return 1; }
    if (!strcmp(what, "arppat")) { *got = P.arp_pattern; return 1; }
    if (!strcmp(what, "arprate")) { *got = P.arp_rate; return 1; }
    if (!strcmp(what, "bpm")) { *got = P.bpm; return 1; }
    if (!strcmp(what, "arpstep")) { *got = arp_step; return 1; }
    return 0;
}
#endif

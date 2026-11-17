/* SPDX-License-Identifier: MIT
 * PLANT-1 M2 — presets, envelopes, an LFO, patch storage, and a screen that shows the sound.
 *
 * M1 proved the four generator slots. M2 gives the device something to play: eight patches,
 * an editable ADSR with its curve, an LFO with its shape, a patch that survives a power cycle,
 * and a panel that draws each slot's waveform and the envelope/LFO instead of only numbers. */
#include "fastmath.h"

/* ------------------------------------------------------------- constants --- */
#define SR 44100.0f
#define NV 4
#define NGEN 4
#define UNI_MAX 3
#define WT_N 256
#define NTAB 4

enum { G_OFF, G_ANALOG, G_WT, G_FM, G_NOISE };
enum { W_SAW, W_SQUARE, W_PULSE, W_SINE };
enum { F_OFF, F_LP, F_HP, F_BP, F_NOTCH };
enum { PAGE_GEN, PAGE_ENV, PAGE_LFO, PAGE_MOD, PAGE_FX, PAGE_ARP, PAGE_SEQ, PAGE_MOTIF, PAGE_MACRO, PAGE_PRESET, NPAGES };
enum { SEQ_STEP, SEQ_SHAPE, SEQ_GROOVE };
enum { LFO_SIN, LFO_TRI, LFO_SAW, LFO_SQR };
enum { LT_CUT, LT_TREM };

/* ---------------------------------------------------------------- patch --- */
typedef struct {
    uint8_t on, type, wave, table, ftype;
    int8_t tune;
    float level, pan, cut, res;
    float pwm, morph, ratio, index, fb, color;
    uint8_t fm_src;                      /* 0 none, 1-4: that generator's output modulates this one */
    float fm_amt;
} gen_t;

#define FX_MAX 5
enum { FX_OFF, FX_DRIVE, FX_CHORUS, FX_DELAY, FX_REVERB, FX_COMP };

typedef struct { uint8_t on, type; float p1, p2, p3; } fx_t;

/* modulation: a source, a destination (slot * 3 + parameter) and a depth */
#define NROUTE 8
enum { SRC_OFF, SRC_ENV1, SRC_ENV2, SRC_LFO1, SRC_LFO2, SRC_VELO, SRC_KEY, SRC_RND, SRC_M1, SRC_M2, SRC_M3, SRC_M4, NSRC };
enum { DST_LVL, DST_CUT, DST_PITCH };
typedef struct { uint8_t on, src, dst; float amt; } route_t;

/* one step: on, note above the root, velocity, gate (percent of the step) */
typedef struct { uint8_t on, vel, gate, ratchet, prob, tie; int8_t note; } seqstep_t;

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
    uint8_t seq_on;
    seqstep_t seq[16];
    uint8_t genre, density, scale;
    uint16_t seed;
    uint8_t motif_mode;                  /* 0 style, 1 euclid, 2 groove */
    uint8_t e_steps, e_hits, e_rot;
    uint8_t chord;
    uint8_t seq_len, swing, key, evolve;
    float macro[4];
} patch_t;

static patch_t P;
static uint32_t mseed;
static uint32_t mrnd(void);
static uint8_t selected, page, preset_index, octave, fx_sel, env_sel, lfo_sel, route_sel;
static uint8_t seq_voice, seq_sel, seq_at, seq_mode, gen_mode, macro_sel;
static uint8_t osd_kind;
static uint32_t osd_until;
static uint32_t btn_down_at[NB];
static uint8_t hold_hits;
static uint8_t btn_hold_done[NB];
static uint8_t osd_page;                  /* for the PAGE / SLOT / PRESET readout */
#ifdef PLAT_HOST
#define BOOT_MS 1200u
#else
#define BOOT_MS 2000u
#endif
static uint32_t boot_t0;
static uint8_t booting = 1;

#define SCOPE_N 56
static int8_t scope_buf[SCOPE_N];
static uint8_t scope_w, scope_dec;
static float out_meter;
static float fx_in_peak;
static uint32_t wipe_t0;
static uint8_t wipe_on;

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
    P.seq_on = 0;
    P.genre = (uint8_t)(i % 6);
    P.density = (uint8_t)(5 + (i % 8));
    P.scale = (uint8_t)(i % 4);
    P.seed = (uint16_t)((i * 277 + 13) % 10000);
    P.motif_mode = 0;
    P.e_steps = 8; P.e_hits = 5; P.e_rot = 0;
    P.chord = 0;
    P.seq_len = 16; P.swing = 0; P.key = 0; P.evolve = 0;
    P.macro[0] = P.macro[1] = P.macro[2] = P.macro[3] = 0.0f;
    {
        static const int8_t seed[16] = {0, 3, 7, 10, 12, 10, 7, 3, 0, 7, 12, 15, 12, 7, 3, 0};
        for (k = 0; k < 16; k++) {
            P.seq[k].on = (uint8_t)((k % 2) == 0);
            P.seq[k].ratchet = 1; P.seq[k].prob = 100; P.seq[k].tie = 0;
            P.seq[k].note = seed[k];
            P.seq[k].vel = (uint8_t)((k % 4 == 0) ? 120 : 84);
            P.seq[k].gate = 55;
        }
    }
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
static uint8_t dly_on, cho_on;
static uint32_t dly_d, cho_d[2];
static float cho_inc = 0.0001f;

/* once a block: the delay time and the chorus taps, as whole samples */
static void fx_block_prepare(void)
{
    int i, ch;
    dly_on = cho_on = 0;
    for (i = 0; i < FX_MAX; i++) {
        fx_t *F = &P.fx[i];
        if (!F->on) continue;
        if (F->type == FX_DELAY) {
            dly_on = 1;
            dly_d = (uint32_t)((10.0f + F->p1 * 120.0f) * 0.001f * SR);
            if (dly_d >= DLY_N - 2) dly_d = DLY_N - 2;
        } else if (F->type == FX_CHORUS) {
            float depth = 0.012f + 0.010f * F->p2;
            cho_inc = (0.05f + F->p1 * 4.95f) / SR;
            cho_on = 1;
            for (ch = 0; ch < 2; ch++) {
                float d = depth * (0.5f + 0.5f * fm_sinf(cho_ph * FM_TWO_PI + (float)ch * 0.5f));
                cho_d[ch] = (uint32_t)((0.008f + d) * SR);
                if (cho_d[ch] >= CHO_N - 2) cho_d[ch] = CHO_N - 2;
            }
        }
    }
}

static inline int16_t ring_get(const int16_t *b, uint32_t n, uint32_t i, uint32_t d)
{
    uint32_t j = i >= d ? i - d : i + n - d;
    while (j >= n) j -= n;
    return b[j];
}

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
    while (i >= n) i -= n;
    b[i] = (int16_t)v;
}

static float fx_drive(float x, fx_t *F)
{
    float g = 1.0f + F->p1 * 14.0f;
    float t = x * g;
    float y = t / (1.0f + fm_fabsf(t));
    float tone = 0.02f + (1.0f - F->p2) * 0.5f;
    drv_lp[0] += (y - drv_lp[0]) * tone;
    return x + (drv_lp[0] - x) * F->p3 * 0.9f;
}

static float fx_chorus(float x, int ch, fx_t *F)
{
    float y = (float)ring_get(cho_b[ch], CHO_N, cho_i, cho_d[ch]) * (1.0f / 32768.0f);
    fx_write(cho_b[ch], CHO_N, cho_i, x);
    return x + y * F->p3;
}

static float fx_delay(float x, int ch, fx_t *F)
{
    float y = (float)ring_get(dly_b[ch], DLY_N, dly_i, dly_d) * (1.0f / 32768.0f);
    fx_write(dly_b[ch], DLY_N, dly_i, x + y * F->p2 * 0.75f);
    return x + y * F->p3;
}

static float fx_reverb(float x, int ch, fx_t *F)
{
    static const uint16_t CL[4] = {1116, 1145, 1188, 1219};
    static const uint16_t AP[2] = {225, 480};
    float g = 0.60f + F->p1 * 0.20f;
    float damp = 0.20f + F->p2 * 0.6f;
    float in = x * 0.22f;
    float sum = 0.0f;
    int c;

    for (c = 0; c < 4; c++) {
        uint32_t i = rv_i[ch][c];
        if (i >= CL[c]) { i = 0; rv_i[ch][c] = 0; }
        float y = (float)rv_b[ch][c][i] * (1.0f / 32768.0f);
        rv_lp[ch] += (y - rv_lp[ch]) * (1.0f - damp);
        rv_b[ch][c][i] = (int16_t)(fm_clampf(in + rv_lp[ch] * g, -1.0f, 1.0f) * 32767.0f);
        sum += y;
        rv_i[ch][c]++;
    }
    for (c = 0; c < 2; c++) {
        uint32_t i = rv_api[ch][c];
        if (i >= AP[c]) { i = 0; rv_api[ch][c] = 0; }
        float b = (float)rv_ap_b[ch][c][i] * (1.0f / 32768.0f);
        float y = b - sum;
        rv_ap_b[ch][c][i] = (int16_t)(fm_clampf(sum + b * 0.5f, -1.0f, 1.0f) * 32767.0f);
        sum = y;
        rv_api[ch][c]++;
    }
    return x + sum * F->p3 * 0.22f;
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
        x = x * (1.0f - (1.0f - comp_g) * (1.0f - 1.0f / ratio)) * (1.0f + F->p3 * 0.5f);
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
    cho_ph += cho_inc;
    if (cho_ph >= 1.0f) cho_ph -= 1.0f;
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
    float gout[NGEN], rnd;
    float inc[NGEN][UNI_MAX];
    float panL[NGEN][UNI_MAX], panR[NGEN][UNI_MAX];
    float norm;
    svf_t filt[NGEN];
} voice_t;

static voice_t V[NV];
static float env_k_a, env_k_d, env_k_r, env2_k_a, env2_k_d, env2_k_r;
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
        v->env += (1.0f - v->env) * env_k_a;
        if (v->env >= 0.999f) { v->env = 1.0f; v->stage = 2; }
        break;
    case 2:
        v->env += (P.s - v->env) * env_k_d;
        if (v->env <= P.s + 0.001f) { v->env = P.s; v->stage = 3; }
        break;
    case 3:
        v->env = P.s;
        break;
    case 4:
        v->env += (0.0f - v->env) * env_k_r;
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
        v->env2 += (1.0f - v->env2) * env2_k_a;
        if (v->env2 >= 0.999f) { v->env2 = 1.0f; st = 2; }
        break;
    case 2:
        v->env2 += (P.e2s - v->env2) * env2_k_d;
        if (v->env2 <= P.e2s + 0.001f) { v->env2 = P.e2s; st = 3; }
        break;
    case 3:
        v->env2 = P.e2s;
        break;
    case 4:
        v->env2 += (0.0f - v->env2) * env2_k_r;
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
    uint8_t was = v->active;
    v->active = 1;
    v->note = note;
    v->vel = vel;
    v->rnd = (float)(mrnd() % 1000u) / 1000.0f;
    v->target = (float)note + (float)octave * 12.0f;
    v->freq = v->target;
    if (v->env < 0.02f) v->env = 0.0f;      /* a reused voice keeps its level: no click */
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
        if (!was) v->filt[g].ic1 = v->filt[g].ic2 = 0.0f;
    }
}

static void voice_sample(voice_t *v, float *outL, float *outR)
{
    int g, u;
    float mixL = 0.0f, mixR = 0.0f;

    for (g = 0; g < NGEN; g++) {
        gen_t *G = &P.gen[g];
        float sumL = 0.0f, sumR = 0.0f;
        if (!G->on || G->type == G_OFF || G->level <= 0.0f) continue;
        {
        float fmo = G->fm_src ? v->gout[(G->fm_src - 1) % NGEN] * G->fm_amt * 0.5f : 0.0f;
        for (u = 0; u < P.uni; u++) {
            float dt = v->inc[g][u];
            float ph = v->phase[g][u];
            float s = 0.0f;

            if (fmo != 0.0f) { ph += fmo; ph -= (float)(int)ph; if (ph < 0.0f) ph += 1.0f; }
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
            sumL += s * v->panL[g][u];
            sumR += s * v->panR[g][u];
        }
        v->gout[g] = sumL - sumR;
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
    fx_block_prepare();
    env_k_a = ENV_K(P.a_ms); env_k_d = ENV_K(P.d_ms); env_k_r = ENV_K(P.r_ms);
    env2_k_a = ENV_K(P.e2a); env2_k_d = ENV_K(P.e2d); env2_k_r = ENV_K(P.e2r);

    for (v = 0; v < NV; v++) {
        voice_t *vv = &V[v];
        if (!vv->active) continue;
        int r;
        float src[NSRC];
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
        src[SRC_KEY] = fm_clampf(((float)vv->note - 48.0f) / 24.0f, -1.0f, 1.0f);
        src[SRC_RND] = vv->rnd;
        src[SRC_M1] = P.macro[0];
        src[SRC_M2] = P.macro[1];
        src[SRC_M3] = P.macro[2];
        src[SRC_M4] = P.macro[3];
        for (g = 0; g < NGEN * 3; g++) vv->mod[g] = 0.0f;
        for (r = 0; r < NROUTE; r++)
            if (P.route[r].on && P.route[r].dst < NGEN * 3)
                vv->mod[P.route[r].dst] += src[P.route[r].src % NSRC] * P.route[r].amt;
        {
            float base = 8.1757989f * fm_exp2f(vv->freq * (1.0f / 12.0f));
            float lsum = 0.0f;
            for (g = 0; g < NGEN; g++)
                if (P.gen[g].on && P.gen[g].type != G_OFF) lsum += P.gen[g].level;
            vv->norm = 0.9f / (lsum > 0.9f ? lsum : 0.9f);
            for (g = 0; g < NGEN; g++) {
                gen_t *G = &P.gen[g];
                float f;
                int u;
                if (!G->on || G->type == G_OFF) continue;
                f = base * fm_exp2f(((float)G->tune + vv->mod[g * 3 + DST_PITCH] * 24.0f) * (1.0f / 12.0f));
                for (u = 0; u < P.uni; u++) {
                    float sp = P.uni == 1 ? 0.0f : ((float)u / (float)(P.uni - 1) * 2.0f - 1.0f);
                    float fu = f * fm_exp2f(sp * P.detune * (1.0f / 1200.0f));
                    float pan = fm_clampf(G->pan + sp * P.spread, -1.0f, 1.0f);
                    vv->inc[g][u] = fu / SR;
                    vv->panL[g][u] = 0.7071f + 0.2929f * pan;
                    vv->panR[g][u] = 0.7071f - 0.2929f * pan;
                }
            }
        }
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
            L += sl * e * vv->vel * 0.105f * vv->norm * trem;
            R += sr * e * vv->vel * 0.105f * vv->norm * trem;
        }
        static float g_now = -1.0f;
        static float lim = 1.0f;
        float pk;
        { float pk = fm_maxf(fm_fabsf(L), fm_fabsf(R)); if (pk > fx_in_peak) fx_in_peak = pk; }
        fx_run(&L, &R);
        pk = fm_maxf(fm_fabsf(L), fm_fabsf(R));
        if (pk > 0.92f) {
            float t = 0.92f / pk;
            if (t < lim) lim = t;
        } else {
            lim += (1.0f - lim) * 0.0008f;
        }
        L *= lim;
        R *= lim;
        if (++scope_dec >= 6) {
            int32_t smp = (int32_t)(L * 1200.0f);
            scope_buf[scope_w] = (int8_t)(smp > 127 ? 127 : smp < -127 ? -127 : smp);
            scope_w = (uint8_t)((scope_w + 1) % SCOPE_N);
            scope_dec = 0;
        }
        {
            float pk = fm_fabsf(L) * 4.0f;
            if (pk > out_meter) out_meter = pk;
            else out_meter *= 0.9993f;
        }
        L = fm_tanhf(L * 0.75f) * 0.97f;
        R = fm_tanhf(R * 0.75f) * 0.97f;
        if (g_now < 0.0f) g_now = gain;
        g_now += (gain - g_now) * 0.0035f;
        lr[2 * i] = (int32_t)(L * g_now);
        lr[2 * i + 1] = (int32_t)(R * g_now);
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
    bank.ver = 11;
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
    if (bank.magic != SAVE_MAGIC || bank.ver != 11 || bank.cur >= NSLOT) return 0;
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
static const char *const PAGE_NAME[NPAGES] = {"GEN", "ENV", "LFO", "MOD", "FX", "ARP", "SEQ", "MOT", "MAC", "PCH"};
static const char *const FX_NAME[6] = {"OFF", "DRIVE", "CHORUS", "DELAY", "REVERB", "COMP"};
static const char *const LFO_NAME[4] = {"SIN", "TRI", "SAW", "SQR"};
static const char *const SRC_NAME[NSRC] = {"--", "ENV1", "ENV2", "LFO1", "LFO2", "VELO", "KEY", "RND", "M1", "M2", "M3", "M4"};
static const char *const DST_NAME[3] = {"LVL", "CUT", "PIT"};
static const char *const PAT_NAME[4] = {"UP", "DOWN", "UPDN", "RAND"};
static const char *const RATE_NAME[5] = {"1/4", "1/8", "1/8T", "1/16", "1/16T"};
static const char *const BANK_NAME[4] = {"BELLS", "BASS", "LEAD", "PADS"};
static const char *const GENRE_NAME[6] = {"TECHNO", "HOUSE", "ACID", "BREAKS", "AMBIENT", "TRANCE"};
static const char *const SCALE_NAME[4] = {"MINOR", "DORIAN", "PENTA", "MAJOR"};
static const int8_t SCALE_STEPS[4][7] = {
    {0, 2, 3, 5, 7, 8, 10},
    {0, 2, 3, 5, 7, 9, 10},
    {0, 3, 5, 7, 10, 12, 15},
    {0, 2, 4, 5, 7, 9, 11}
};
static const char *const K_LABEL[NPAGES][4] = {
    {"TYPE", "LEVEL", "PAN", "TUNE"},
    {"A", "D", "S", "R"},
    {"SHAPE", "RATE", "AMT", "TARGET"},
    {"SRC", "DST", "DEPTH", "-"},
    {"SLOT", "A", "B", "C"},
    {"PATTERN", "RATE", "OCT", "GATE"},
    {"STEP", "NOTE", "VEL", "GATE"},
    {"STYLE", "DENSITY", "SCALE", "ROLL"},
    {"M1", "M2", "M3", "M4"},
    {"PRESET", "SLOT", "BANK", "-"}
};

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

static void draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 - x0, dy = y1 - y0;
    int32_t n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    int32_t i;
    if (n <= 0) { cv_rect(x0, y0, 1, 1, c); return; }
    for (i = 0; i <= n; i++)
        cv_rect(x0 + dx * i / n, y0 + dy * i / n, 1, 1, c);
}

static void draw_card(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x + 2, y, w - 4, 1, c);
    cv_rect(x + 2, y + h - 1, w - 4, 1, c);
    cv_rect(x, y + 2, 1, h - 4, c);
    cv_rect(x + w - 1, y + 2, 1, h - 4, c);
    cv_rect(x + 1, y + 1, 1, 1, c);
    cv_rect(x + w - 2, y + 1, 1, 1, c);
    cv_rect(x + 1, y + h - 2, 1, 1, c);
    cv_rect(x + w - 2, y + h - 2, 1, 1, c);
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
    float total, fa, fd, fs;
    int32_t xa, xd, xs, xr, i;

    if (a < 1.0f) a = 1.0f;
    if (d < 1.0f) d = 1.0f;
    if (r < 1.0f) r = 1.0f;
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    total = (a + d + r) * 1.35f;               /* the sounding part gets a third of the width */
    fa = a / total;
    fd = (a + d) / total;
    fs = (a + d + (a + d + r) * 0.35f) / total;
    xr = x + w - 2;
    xa = x + (int32_t)((float)w * fa);
    xd = x + (int32_t)((float)w * fd);
    xs = x + (int32_t)((float)w * fs);
    if (xa < x + 2) xa = x + 2;
    if (xd < xa + 2) xd = xa + 2;
    if (xs < xd + 4) xs = xd + 4;
    if (xs > xr - 2) xs = xr - 2;
    if (xd > xs - 2) xd = xs - 2;
    if (xa > xd - 2) xa = xd - 2;

    cv_rect(x, y + h - 1, w, 1, RGB(24, 33, 43));     /* the floor */
    cv_rect(x, y, 1, h, RGB(24, 33, 43));

    for (i = x; i <= xa; i++) {                        /* attack: 0 -> 1 */
        float k = (float)(i - x) / (float)(xa - x + 1);
        int32_t yy = y + h - 1 - (int32_t)((float)(h - 2) * k);
        cv_rect(i, yy, 1, y + h - 1 - yy, C_LINE);
        cv_rect(i, yy, 1, 1, C_HI);
    }
    for (i = xa; i <= xd; i++) {                       /* decay: 1 -> s */
        float k = (float)(i - xa) / (float)(xd - xa + 1);
        float v = s + (1.0f - s) * fm_exp2f(-4.0f * k);
        int32_t yy = y + h - 1 - (int32_t)((float)(h - 2) * v);
        cv_rect(i, yy, 1, y + h - 1 - yy, C_LINE);
        cv_rect(i, yy, 1, 1, C_HI);
    }
    for (i = xd; i <= xs; i++) {                       /* sustain */
        int32_t yy = y + h - 1 - (int32_t)((float)(h - 2) * s);
        cv_rect(i, yy, 1, y + h - 1 - yy, C_LINE);
        cv_rect(i, yy, 1, 1, C_HI);
    }
    for (i = xs; i <= xr; i++) {                       /* release: s -> 0 */
        float k = (float)(i - xs) / (float)(xr - xs + 1);
        float v = s * fm_exp2f(-4.0f * k);
        int32_t yy = y + h - 1 - (int32_t)((float)(h - 2) * v);
        cv_rect(i, yy, 1, y + h - 1 - yy, C_LINE);
        cv_rect(i, yy, 1, 1, C_HI);
    }
}

static void draw_lfo_graph(int32_t x, int32_t y, int32_t w, int32_t h)
{
    int32_t i;
    for (i = 0; i < w; i++) {
        float v = lfo_shape(P.lfo_shape, (float)i / (float)w);
        int32_t yy = y + h / 2 - (int32_t)(v * (float)(h / 2 - 1));
        int32_t top = yy < y + h / 2 ? yy : y + h / 2;
        int32_t hh = yy < y + h / 2 ? y + h / 2 - yy : yy - (y + h / 2);
        if (hh > 0) cv_rect(x + i, top, 1, hh, C_LINE);
        cv_rect(x + i, yy, 1, 1, C_HI);
    }
}

/* a tree grows out of the ground, then the name, then a splash line: the shrub boot, in our palette */
static float boot_sway;

static void boot_branch(int32_t x, int32_t y, float ang, float len, int depth, float grow)
{
    float reach = 1.7f - (float)depth * 0.22f;
    float k = grow * reach;
    int32_t x1, y1;
    uint16_t c = depth == 0 ? C_HI : (depth == 1 ? C_WHITE : C_GRAY);

    ang += boot_sway * (0.4f + 0.35f * (float)depth);
    if (k > 1.0f) k = 1.0f;
    if (k <= 0.0f) return;
    x1 = x + (int32_t)(fm_cosf(ang) * len * k);
    y1 = y + (int32_t)(fm_sinf(ang) * len * k);
    if (depth == 0) {                        /* a soft halo behind the trunk */
        draw_line(x - 1, y, x1 - 1, y1, RGB(18, 44, 56));
        draw_line(x + 1, y, x1 + 1, y1, RGB(18, 44, 56));
    }
    draw_line(x, y, x1, y1, c);
    if (k < 0.99f) return;
    if (depth >= 4) {
        cv_rect(x1 - 1, y1 - 1, 3, 3, C_HI);
        if ((mrnd() & 1u) != 0u) cv_rect(x1, y1 - 3, 1, 2, C_WHITE);
        return;
    }
    {
        float a = 0.46f + 0.24f * (float)(mrnd() % 100) / 100.0f;
        float l = len * (0.66f + 0.10f * (float)(mrnd() % 100) / 100.0f);
        boot_branch(x1, y1, ang - a, l, depth + 1, grow);
        boot_branch(x1, y1, ang + a, l, depth + 1, grow);
    }
}

static void boot_roots(float g)
{
    int i;
    for (i = 0; i < 3; i++) {
        int32_t x = 112 + i * 8;
        int32_t len = (int32_t)(10.0f * g) + 2;
        draw_line(x, 137, x - 4 + i * 4, 137 + len, RGB(22, 34, 40));
    }
}

static void boot_sparkle(uint32_t dt)
{
    int i;
    for (i = 0; i < 12; i++) {
        int32_t x = 24 + (i * 53 + (int32_t)dt / 40) % 192;
        int32_t y = 20 + (i * 37 + (int32_t)dt / 55) % 110;
        if ((((uint32_t)i * 31u + dt / 120u) % 5u) == 0u)
            cv_rect(x, y, 1, 1, (i & 1) ? C_WHITE : C_HI);
    }
}

static void boot_pollen(uint32_t dt)
{
    int i;
    for (i = 0; i < 14; i++) {
        int32_t y = 190 - (int32_t)((dt / 10u + (uint32_t)(i * 37)) % 150u);
        int32_t x = 30 + (i * 23 + (int32_t)(fm_sinf((float)dt * 0.003f + (float)i) * 12.0f)) % 180;
        if (x < 4) x = 4;
        cv_rect(x, y, 1, 1, (i & 1) ? C_DIM : C_GRAY);
    }
}

static void boot_ring(uint32_t dt)
{
    int32_t i;
    for (i = 0; i < 20; i++) {
        float a = (float)i / 20.0f * 6.2831853f - 1.5708f;
        int32_t on = (uint32_t)i * 20u < (dt % 1200u) / 3u;
        int32_t x = 120 + (int32_t)(fm_cosf(a) * 9.0f);
        int32_t y = 224 + (int32_t)(fm_sinf(a) * 9.0f);
        cv_rect(x, y, 2, 2, on ? C_HI : RGB(22, 31, 40));
    }
}

static void draw_boot(uint32_t dt)
{
    float g = (float)dt / 500.0f;             /* the tree is already grown: it only settles */
    float w = dt > 450 ? (float)(dt - 450) / 450.0f : 0.0f;
    int i;

    if (g > 1.0f) g = 1.0f;
    if (w > 1.0f) w = 1.0f;
    mseed = 0x51EDu;

    cv_begin(240, 140, C_BLACK);
    {
        int32_t k;
        boot_sway = fm_sinf((float)dt * 0.0024f) * 0.045f;
        cv_rect(58, 136, 124, 1, RGB(30, 42, 54));
        for (k = 0; k < 26; k++) {                   /* a cone of light behind the trunk */
            int32_t y = 36 + k * 4;
            int32_t half = 5 + (134 - y) / 3;
            int32_t g = 6 + (k % 4) * 3 + (int32_t)(fm_sinf((float)dt * 0.004f + (float)k * 0.6f) * 2.0f);
            cv_rect(120 - half, y, half * 2, 3, RGB(g / 2, g, g + 10));
        }
        boot_roots(1.0f);
        boot_branch(120, 134, -1.5708f, 40.0f, 0, 1.0f);
        boot_pollen(dt);
        boot_sparkle(dt);
    }
    cv_blit(0, 0);

    /* the name, the splash line and the dots below it */
    cv_begin(240, 100, C_BLACK);
    if (w > 0.0f) {
        const char *name = "PLANT-1";
        int32_t tw = text_w(&FONT_L, name);
        int32_t x0 = 120 - tw / 2;
        int32_t y0 = 4 + (int32_t)((1.0f - w) * 5.0f);
        cv_text(x0, y0, &FONT_L, name, C_WHITE);
        if (w > 0.5f) {
            const char *sub = "a modular synth";
            int32_t sw = text_w(&FONT_T, sub);
            cv_text(120 - sw / 2, y0 + 36, &FONT_T, sub, C_GRAY);
        }
    }
    if (dt > 800u) {
        const char *splash = "grows on you";
        int32_t sw = text_w(&FONT_B, splash);
        int32_t x0 = 120 - sw / 2;
        float bob = fm_sinf((float)dt * 0.012f) * 1.6f;
        for (i = 0; splash[i]; i++) {
            char c[2];
            c[0] = splash[i];
            c[1] = 0;
            cv_text(x0 + i * 8, 66 + (int32_t)(bob + (i & 1 ? 1 : 0)), &FONT_B, c, C_AMB);
        }
    }
    boot_ring(dt);
    cv_blit(0, 140);
}
static void draw(void)
{
    char t[16], v[12];
    int i;
    int32_t n = -1;
    uint32_t now = plat_ms();

    for (i = 0; i < NV; i++) if (V[i].active) { if (n < 0) n = V[i].note; break; }

    /* header: name, patch, note, scope */
    cv_begin(240, 46, C_BLACK);
    cv_text(8, 1, &FONT_B, "PLANT-1", C_WHITE);
    note_text(t, n);
    cv_text(196 - text_w(&FONT_B, t), 1, &FONT_B, t, n >= 0 ? C_HI : C_DIM);
    cv_text(8, 17, &FONT_T, PRESET_NAME[preset_index], C_GRAY);
    num_text(v, preset_index + 1);
    cv_text(150, 17, &FONT_T, "P", C_DIM);
    cv_text(157, 17, &FONT_T, v, C_GRAY);
    cv_text(171, 17, &FONT_T, "/32", C_DIM);
    num_text(v, selected + 1);
    cv_text(199, 17, &FONT_T, "G", C_DIM);
    cv_text(207, 17, &FONT_T, v, C_GRAY);
    {
        int32_t yc = 38;
        cv_rect(8, 38, 168, 1, RGB(20, 28, 36));
        for (i = 0; i < 56; i++) {
            int8_t smp = scope_buf[(scope_w + i) % SCOPE_N];
            int32_t y = yc - (int32_t)((int32_t)smp * 6 / 127);
            if (y < 31) y = 31;
            if (y > 45) y = 45;
            cv_rect(8 + i * 3, y < yc ? y : yc, 2, (y < yc ? yc - y : y - yc) < 1 ? 1 : (y < yc ? yc - y : y - yc), smp >= 0 ? C_HI : C_GRAY);
        }
        cv_rect(186, 32, 44, 4, RGB(24, 33, 43));
        cv_rect(186, 32, (int32_t)(fm_clampf(out_meter, 0.0f, 1.0f) * 44.0f), 4, out_meter > 0.85f ? C_WHITE : C_HI);
    }
    cv_blit(0, 0);

    /* one page fills the screen: 240 x 144 */
    cv_begin(240, 144, C_BLACK);
    switch (page) {
    case PAGE_GEN: {
        static const char *const LBL[5][4] = {
            {"-", "-", "-", "-"},
            {"WAVE", "PWM", "CUT", "RES"},
            {"TABLE", "MORPH", "CUT", "RES"},
            {"RATIO", "INDEX", "FB", "CUT"},
            {"COLOR", "CUT", "RES", "LEVEL"}
        };
        static const char *const WAV[4] = {"SAW", "SQR", "PULSE", "SINE"};
        gen_t *G = &P.gen[selected];
        float val[4];
        char txt[8];
        int k;

        cv_text(8, 1, &FONT_T, gen_mode ? "CROSS FM" : "GENERATORS", C_GRAY);
        num_text(v, selected + 1);
        cv_text(120, 1, &FONT_T, "G", C_DIM);
        cv_text(128, 1, &FONT_T, v, C_HI);
        cv_text(142, 1, &FONT_T, GEN_NAME[G->type], C_WHITE);
        if (gen_mode) {
            int k2;
            for (k2 = 0; k2 < NGEN; k2++) {
                gen_t *E = &P.gen[k2];
                int32_t cx = (k2 & 1) ? 121 : 2;
                int32_t cy = (k2 >> 1) ? 74 : 14;
                draw_card(cx, cy, 117, 56, k2 == selected ? C_HI : RGB(22, 31, 40));
                cv_text(cx + 6, cy + 4, &FONT_T, k2 == 0 ? "G1 FM" : k2 == 1 ? "G2 FM" : k2 == 2 ? "G3 FM" : "G4 FM", C_HI);
                cv_text(cx + 6, cy + 18, &FONT_T, "SRC", C_GRAY);
                cv_text(cx + 40, cy + 16, &FONT_S, E->fm_src ? (E->fm_src == 1 ? "G1" : E->fm_src == 2 ? "G2" : E->fm_src == 3 ? "G3" : "G4") : "--", C_WHITE);
                num_text(txt, (int32_t)(E->fm_amt * 100.0f));
                cv_text(cx + 6, cy + 38, &FONT_T, "AMT", C_GRAY);
                cv_text(cx + 40, cy + 36, &FONT_S, txt, E->fm_src ? C_HI : C_DIM);
            }
            break;
        }
        for (i = 0; i < NGEN; i++) {
            gen_t *E = &P.gen[i];
            int32_t cx = (i & 1) ? 121 : 2;
            int32_t cy = (i >> 1) ? 63 : 15;
            int32_t j, lit;
            draw_card(cx, cy, 117, 44, i == selected ? C_HI : RGB(22, 31, 40));
            txt[0] = 'G'; txt[1] = (char)('1' + i); txt[2] = 0;
            cv_text(cx + 6, cy + 4, &FONT_T, txt, i == selected ? C_WHITE : C_GRAY);
            cv_text(cx + 26, cy + 3, &FONT_XS, GEN_NAME[E->type], E->on ? C_HI : C_DIM);
            draw_slot_wave(cx + 6, cy + 18, 58, 14, i, E->on ? C_HI : C_DIM);
            lit = E->on ? (int32_t)(E->level * 5.0f + 0.5f) : 0;
            for (j = 0; j < 5; j++)
                cv_rect(cx + 72 + j * 7, cy + 28, 5, 5, j < lit ? (E->on ? C_HI : C_DIM) : RGB(22, 31, 40));
            if (E->ftype) cv_text(cx + 72, cy + 18, &FONT_T, FTYPE_NAME[E->ftype], C_GRAY);
            if (E->tune) { num_text(txt, E->tune); cv_text(cx + 6, cy + 33, &FONT_T, txt, C_AMB); }
        }
        if (G->type == G_ANALOG) {
            val[0] = (float)G->wave; val[1] = G->pwm * 100.0f; val[2] = G->cut; val[3] = G->res * 100.0f;
        } else if (G->type == G_WT) {
            val[0] = (float)G->table; val[1] = G->morph * 100.0f; val[2] = G->cut; val[3] = G->res * 100.0f;
        } else if (G->type == G_FM) {
            val[0] = G->ratio * 100.0f; val[1] = G->index * 100.0f; val[2] = G->fb * 100.0f; val[3] = G->cut;
        } else {
            val[0] = G->color * 100.0f; val[1] = G->cut; val[2] = G->res * 100.0f; val[3] = G->level * 100.0f;
        }
        for (k = 0; k < 4; k++) {
            int32_t cx = 2 + k * 60;
            draw_card(cx, 112, 57, 30, RGB(22, 31, 40));
            cv_text(cx + 5, 115, &FONT_T, LBL[G->type][k], C_GRAY);
            if (G->type == G_ANALOG && k == 0) cv_text(cx + 5, 125, &FONT_S, WAV[G->wave & 3], C_HI);
            else { num_text(txt, (int32_t)val[k]); cv_text(cx + 5, 125, &FONT_S, txt, C_HI); }
        }
        break;
    }
    case PAGE_ENV: {
        float ea = env_sel ? P.e2a : P.a_ms, ed = env_sel ? P.e2d : P.d_ms;
        float es = env_sel ? P.e2s : P.s, er = env_sel ? P.e2r : P.r_ms;
        static const char *const L[4] = {"A", "D", "S", "R"};
        float vals[4];
        int k;
        vals[0] = ea; vals[1] = ed; vals[2] = es * 100.0f; vals[3] = er;
        cv_text(8, 1, &FONT_T, env_sel ? "MOD ENV" : "AMP ENV", env_sel ? C_HI : C_WHITE);
        draw_card(2, 13, 236, 80, RGB(22, 31, 40));
        draw_env_graph(10, 18, 220, 70, ea, ed, es, er);
        for (k = 0; k < 4; k++) {
            int32_t cx = 2 + k * 59;
            draw_card(cx, 97, 57, 44, RGB(22, 31, 40));
            cv_text(cx + 5, 100, &FONT_T, L[k], C_GRAY);
            num_text(v, (int32_t)vals[k]);
            cv_text(cx + 5, 112, &FONT_S, v, C_HI);
        }
        break;
    }
    case PAGE_LFO:
        cv_text(8, 1, &FONT_T, lfo_sel ? "LFO2" : "LFO1", lfo_sel ? C_HI : C_WHITE);
        cv_text(50, 1, &FONT_T, LFO_NAME[(lfo_sel ? P.lfo2_shape : P.lfo_shape) & 3], C_GRAY);
        draw_card(2, 13, 236, 76, RGB(22, 31, 40));
        draw_lfo_graph(10, 18, 220, 66);
        draw_card(2, 93, 76, 48, RGB(22, 31, 40));
        cv_text(7, 96, &FONT_T, "RATE", C_GRAY);
        hz_text(v, lfo_sel ? P.lfo2_rate : P.lfo_rate);
        cv_text(7, 108, &FONT_S, v, C_HI);
        draw_card(82, 93, 76, 48, RGB(22, 31, 40));
        cv_text(87, 96, &FONT_T, "AMOUNT", C_GRAY);
        num_text(v, (int32_t)((lfo_sel ? P.lfo2_amt : P.lfo_amt) * 100.0f));
        cv_text(87, 108, &FONT_S, v, C_HI);
        draw_card(162, 93, 76, 48, RGB(22, 31, 40));
        cv_text(167, 96, &FONT_T, "TARGET", C_GRAY);
        cv_text(167, 108, &FONT_S, lfo_sel ? "MATRIX" : (P.lfo_tgt == LT_CUT ? "CUTOFF" : "TREM"), C_HI);
        break;
    case PAGE_MOD: {
        int r;
        cv_text(8, 1, &FONT_T, "ROUTES", C_GRAY);
        for (r = 0; r < NROUTE; r++) {
            route_t *R = &P.route[r];
            int32_t y = 13 + r * 16;
            draw_card(2, y, 236, 15, r == route_sel ? C_HI : RGB(22, 31, 40));
            num_text(v, r + 1);
            cv_text(7, y + 2, &FONT_T, v, C_DIM);
            cv_text(20, y + 1, &FONT_T, SRC_NAME[R->src], R->on ? C_HI : C_DIM);
            cv_text(62, y + 3, &FONT_T, ">", C_DIM);
            num_text(v, R->dst / 3 + 1);
            cv_text(74, y + 1, &FONT_T, "G", C_GRAY);
            cv_text(84, y + 1, &FONT_T, v, R->on ? C_HI : C_DIM);
            cv_text(96, y + 1, &FONT_T, DST_NAME[R->dst % 3], R->on ? C_HI : C_DIM);
            cv_rect(140, y + 6, 92, 4, RGB(22, 31, 40));
            if (R->on) cv_rect(140, y + 6, (int32_t)(fm_clampf(fm_fabsf(R->amt), 0.0f, 1.0f) * 92.0f), 4, C_GRAY);
        }
        break;
    }
    case PAGE_FX:
        cv_text(8, 1, &FONT_T, "CHAIN", C_GRAY);
        for (i = 0; i < FX_MAX; i++) {
            fx_t *F = &P.fx[i];
            int32_t y = 13 + i * 21;
            draw_card(2, y, 236, 19, i == fx_sel ? C_HI : RGB(22, 31, 40));
            num_text(v, i + 1);
            cv_text(7, y + 5, &FONT_T, v, C_DIM);
            cv_text(20, y + 4, &FONT_S, FX_NAME[F->type], F->on ? C_HI : C_DIM);
            cv_text(120, y + 5, &FONT_T, F->on ? "ON" : "--", F->on ? C_HI : C_DIM);
            cv_rect(150, y + 8, 80, 4, RGB(22, 31, 40));
            if (F->on) cv_rect(150, y + 8, (int32_t)(fm_clampf(F->p3, 0.0f, 1.0f) * 80.0f), 4, C_GRAY);
        }
        {
            fx_t *F = &P.fx[fx_sel];
            draw_card(2, 121, 236, 20, RGB(22, 31, 40));
            num_text(v, (int32_t)(F->p1 * 100.0f));
            cv_text(8, 126, &FONT_T, "A", C_GRAY); cv_text(22, 124, &FONT_S, v, C_HI);
            num_text(v, (int32_t)(F->p2 * 100.0f));
            cv_text(80, 126, &FONT_T, "B", C_GRAY); cv_text(94, 124, &FONT_S, v, C_HI);
            num_text(v, (int32_t)(F->p3 * 100.0f));
            cv_text(150, 126, &FONT_T, "C", C_GRAY); cv_text(164, 124, &FONT_S, v, C_HI);
        }
        break;
    case PAGE_ARP:
        draw_card(2, 13, 117, 52, RGB(22, 31, 40));
        cv_text(8, 16, &FONT_T, "PATTERN", C_GRAY);
        cv_text(8, 28, &FONT_S, PAT_NAME[P.arp_pattern & 3], C_HI);
        draw_card(121, 13, 117, 52, RGB(22, 31, 40));
        cv_text(127, 16, &FONT_T, "RATE", C_GRAY);
        cv_text(127, 28, &FONT_S, RATE_NAME[P.arp_rate % 5], C_HI);
        draw_card(2, 69, 117, 52, RGB(22, 31, 40));
        cv_text(8, 72, &FONT_T, "OCTAVES", C_GRAY);
        num_text(v, P.arp_oct ? P.arp_oct : 1);
        cv_text(8, 84, &FONT_S, v, C_HI);
        draw_card(121, 69, 117, 52, RGB(22, 31, 40));
        cv_text(127, 72, &FONT_T, "GATE", C_GRAY);
        num_text(v, (int32_t)(P.arp_gate * 100.0f));
        cv_text(127, 84, &FONT_S, v, C_HI);
        cv_text(8, 128, &FONT_T, P.arp_on ? "ARP RUNNING" : "ARP OFF", P.arp_on ? C_HI : C_DIM);
        num_text(v, P.bpm);
        cv_text(160, 128, &FONT_T, "TEMPO", C_GRAY);
        cv_text(212, 126, &FONT_S, v, C_HI);
        break;
    case PAGE_SEQ: {
        static const char *const M[3] = {"STEP", "SHAPE", "GROOVE"};
        cv_text(8, 1, &FONT_T, M[seq_mode], C_GRAY);
        if (seq_mode == SEQ_GROOVE) {
            num_text(v, P.seq_len);
            draw_card(2, 13, 117, 52, RGB(22, 31, 40));
            cv_text(8, 16, &FONT_T, "LENGTH", C_GRAY);
            cv_text(8, 28, &FONT_S, v, C_HI);
            num_text(v, P.swing);
            draw_card(121, 13, 117, 52, RGB(22, 31, 40));
            cv_text(127, 16, &FONT_T, "SWING", C_GRAY);
            cv_text(127, 28, &FONT_S, v, C_HI);
            {
                static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                draw_card(2, 69, 117, 52, RGB(22, 31, 40));
                cv_text(8, 72, &FONT_T, "KEY", C_GRAY);
                cv_text(8, 84, &FONT_S, N[P.key % 12], C_HI);
            }
            draw_card(121, 69, 117, 52, RGB(22, 31, 40));
            cv_text(127, 72, &FONT_T, "EVOLVE", C_GRAY);
            cv_text(127, 84, &FONT_S, P.evolve ? "ON" : "OFF", P.evolve ? C_HI : C_DIM);
            break;
        }
        for (i = 0; i < 16; i++) {
            seqstep_t *st = &P.seq[i];
            int32_t x = (i % 8) * 29 + 1;
            int32_t y = 13 + (i / 8) * 29;
            draw_card(x, y, 27, 27, i == seq_sel ? C_HI : RGB(22, 31, 40));
            if (P.seq_on && i == seq_at) cv_rect(x + 2, y + 24, 23, 2, C_WHITE);
            num_text(v, st->note);
            cv_text(x + 4, y + 2, &FONT_XS, v, st->on ? C_HI : C_DIM);
            cv_rect(x + 4, y + 18, 19, 4, RGB(22, 31, 40));
            if (st->on) cv_rect(x + 4, y + 18, (int32_t)((float)st->vel / 127.0f * 19.0f), 4, C_GRAY);
        }
        {
            seqstep_t *st = &P.seq[seq_sel];
            static const char *const L1[3] = {"NOTE", "VEL", "GATE"};
            static const char *const L2[3] = {"RATCHET", "PROB", "TIE"};
            int k;
            for (k = 0; k < 3; k++) {
                int32_t cx = 2 + k * 79;
                int32_t val;
                draw_card(cx, 74, 77, 56, RGB(22, 31, 40));
                if (seq_mode == SEQ_STEP) {
                    cv_text(cx + 5, 78, &FONT_T, L1[k], C_GRAY);
                    val = k == 0 ? st->note : k == 1 ? st->vel : st->gate;
                } else {
                    cv_text(cx + 5, 78, &FONT_T, L2[k], C_GRAY);
                    val = k == 0 ? st->ratchet : k == 1 ? st->prob : st->tie;
                }
                num_text(v, val);
                cv_text(cx + 5, 90, &FONT_S, v, C_HI);
            }
        }
        break;
    }
    case PAGE_MOTIF: {
        static const char *const L[4] = {"STYLE", "DENSITY", "SCALE", "ROLL"};
        cv_text(8, 1, &FONT_T, P.motif_mode == 0 ? "MOTIF STYLE" : "MOTIF EUCLID", P.motif_mode ? C_HI : C_WHITE);
        if (P.motif_mode == 0) {
            draw_card(2, 13, 117, 52, RGB(22, 31, 40));
            cv_text(7, 16, &FONT_T, L[0], C_GRAY);
            cv_text(7, 28, &FONT_S, GENRE_NAME[P.genre % 6], C_HI);
            num_text(v, P.density ? P.density : 1);
            draw_card(121, 13, 117, 52, RGB(22, 31, 40));
            cv_text(127, 16, &FONT_T, L[1], C_GRAY);
            cv_text(127, 28, &FONT_S, v, C_HI);
            draw_card(2, 69, 117, 52, RGB(22, 31, 40));
            cv_text(7, 72, &FONT_T, L[2], C_GRAY);
            cv_text(7, 84, &FONT_S, SCALE_NAME[P.scale & 3], C_HI);
            num_text(v, P.seed % 10000);
            draw_card(121, 69, 117, 52, RGB(22, 31, 40));
            cv_text(127, 72, &FONT_T, L[3], C_GRAY);
            cv_text(127, 84, &FONT_S, v, C_HI);
        } else {
            num_text(v, P.e_steps);
            draw_card(2, 13, 117, 52, RGB(22, 31, 40));
            cv_text(7, 16, &FONT_T, "STEPS", C_GRAY);
            cv_text(7, 28, &FONT_S, v, C_HI);
            num_text(v, P.e_hits);
            draw_card(121, 13, 117, 52, RGB(22, 31, 40));
            cv_text(127, 16, &FONT_T, "HITS", C_GRAY);
            cv_text(127, 28, &FONT_S, v, C_HI);
            num_text(v, P.e_rot);
            draw_card(2, 69, 117, 52, RGB(22, 31, 40));
            cv_text(7, 72, &FONT_T, "ROTATE", C_GRAY);
            cv_text(7, 84, &FONT_S, v, C_HI);
            draw_card(121, 69, 117, 52, RGB(22, 31, 40));
            cv_text(127, 72, &FONT_T, "WRITE", C_GRAY);
            cv_text(127, 84, &FONT_S, "K4", C_HI);
        }
        for (i = 0; i < 16; i++) {
            int32_t x = 8 + i * 14;
            int32_t h = P.seq[i].on ? 4 + (P.seq[i].note % 8) : 1;
            if (h > 14) h = 14;
            cv_rect(x, 138 - h, 10, h, P.seq[i].on ? C_HI : RGB(22, 31, 40));
        }
        break;
    }
    case PAGE_MACRO: {
        int k;
        cv_text(8, 1, &FONT_T, "MACROS", C_GRAY);
        cv_text(150, 1, &FONT_T, "a route source", C_DIM);
        for (k = 0; k < 4; k++) {
            int32_t cx = 2 + (k & 1) * 119;
            int32_t cy = 14 + (k >> 1) * 62;
            draw_card(cx, cy, 117, 56, k == macro_sel ? C_HI : RGB(22, 31, 40));
            cv_text(cx + 6, cy + 4, &FONT_T, k == 0 ? "M1" : k == 1 ? "M2" : k == 2 ? "M3" : "M4", C_GRAY);
            num_text(v, (int32_t)(P.macro[k] * 100.0f));
            cv_text(cx + 6, cy + 16, &FONT_S, v, C_HI);
            cv_rect(cx + 6, cy + 40, 100, 6, RGB(22, 31, 40));
            cv_rect(cx + 6, cy + 40, (int32_t)(fm_clampf(P.macro[k], 0.0f, 1.0f) * 100.0f), 6, C_HI);
        }
        break;
    }
    default: {
        static const char *const NM[3] = {"PATCH", "SLOT", "BANK"};
        int k;
        cv_text(8, 4, &FONT_L, PRESET_NAME[preset_index], C_HI);
        for (k = 0; k < 3; k++) {
            int32_t cx = 2 + k * 79;
            draw_card(cx, 44, 77, 52, RGB(22, 31, 40));
            cv_text(cx + 5, 47, &FONT_T, NM[k], C_GRAY);
            if (k == 0) { num_text(v, preset_index + 1); cv_text(cx + 5, 58, &FONT_S, v, C_HI); cv_text(cx + 5, 72, &FONT_T, "/ 32", C_DIM); }
            else if (k == 1) { num_text(v, slot_index + 1); cv_text(cx + 5, 58, &FONT_S, v, C_HI); cv_text(cx + 5, 72, &FONT_T, "/ 4", C_DIM); }
            else { num_text(v, preset_index / 8 + 1); cv_text(cx + 5, 58, &FONT_S, v, C_HI); cv_text(cx + 5, 72, &FONT_T, BANK_NAME[preset_index / 8], C_DIM); }
        }
        cv_text(8, 104, &FONT_T, slot_index < NSLOT && bank.slot[slot_index].used ? "SLOT USED - SAVE overwrites" : "SLOT EMPTY - SAVE stores", C_DIM);
        break;
    }
    }

    if (wipe_on) {
        uint32_t dt = now - wipe_t0;
        if (dt > 160u) wipe_on = 0;
        else cv_rect((int32_t)((float)dt / 160.0f * 236.0f), 2, 2, 140, C_HI);
    }
    cv_blit(0, 46);

    /* the four knobs' names for this page */
    cv_begin(240, 26, C_BLACK);
    {
        const char *kl[4];
        for (i = 0; i < 4; i++) kl[i] = K_LABEL[page][i];
        if (page == PAGE_GEN && gen_mode) { kl[0] = "SRC"; kl[1] = "AMT"; kl[2] = "-"; kl[3] = "-"; }
        else if (page == PAGE_MACRO) { kl[0] = "M1"; kl[1] = "M2"; kl[2] = "M3"; kl[3] = "M4"; }
        else if (page == PAGE_SEQ) {
            if (seq_mode == SEQ_SHAPE) { kl[0] = "STEP"; kl[1] = "RATCHET"; kl[2] = "PROB"; kl[3] = "TIE"; }
            else if (seq_mode == SEQ_GROOVE) { kl[0] = "LENGTH"; kl[1] = "SWING"; kl[2] = "KEY"; kl[3] = "EVOLVE"; }
        } else if (page == PAGE_MOTIF) {
            if (P.motif_mode == 0) { kl[0] = "STYLE"; kl[1] = "DENSITY"; kl[2] = "SCALE"; kl[3] = "ROLL"; }
            else { kl[0] = "STEPS"; kl[1] = "HITS"; kl[2] = "ROTATE"; kl[3] = "WRITE"; }
        }
        for (i = 0; i < 4; i++) {
            int32_t x = 4 + i * 59;
            char kn[4];
            kn[0] = 'K'; kn[1] = (char)('1' + i); kn[2] = 0;
            cv_text(x, 7, &FONT_T, kn, C_DIM);
            cv_text(x + 13, 7, &FONT_T, kl[i], C_GRAY);
        }
    }
    cv_blit(0, 190);

    /* page tabs */
    cv_begin(240, 24, C_BLACK);
    for (i = 0; i < NPAGES; i++) {
        int32_t x = i * 24;
        if (i == page) {
            cv_rect(x + 1, 3, 23, 18, C_HI);
            cv_text(x + 2, 6, &FONT_T, PAGE_NAME[i], C_BLACK);
        } else {
            cv_text(x + 2, 7, &FONT_T, PAGE_NAME[i], C_DIM);
        }
    }
    cv_blit(0, 216);

    /* the encoder readout replaces the knob strip for a moment: the preset name stays put */
    if (osd_kind && plat_ms() < osd_until) {
        cv_begin(240, 26, C_BLACK);
        if (osd_kind == 1) {
            num_text(v, preset_index + 1);
            cv_text(8, 7, &FONT_T, "PRESET", C_HI);
            cv_text(58, 7, &FONT_T, v, C_WHITE);
            cv_text(84, 7, &FONT_T, PRESET_NAME[preset_index], C_GRAY);
        } else if (osd_kind == 2) {
            num_text(v, selected + 1);
            cv_text(8, 7, &FONT_T, "SLOT", C_HI);
            cv_text(40, 7, &FONT_T, "G", C_GRAY);
            cv_text(48, 7, &FONT_T, v, C_WHITE);
        } else if (osd_kind == 3) {
            cv_text(8, 7, &FONT_T, "PAGE", C_HI);
            cv_text(40, 7, &FONT_T, PAGE_NAME[page], C_WHITE);
        } else if (osd_kind == 4) {
            cv_text(8, 7, &FONT_T, "PANIC", C_HI);
            cv_text(52, 7, &FONT_T, "all notes off", C_GRAY);
        } else if (osd_kind == 10) {
            cv_text(8, 7, &FONT_T, "MOTIF", C_HI);
            cv_text(52, 7, &FONT_T, P.motif_mode == 0 ? "STYLE" : "EUCLID", C_WHITE);
        } else if (osd_kind == 11) {
            cv_text(8, 7, &FONT_T, "CHORD", C_HI);
            cv_text(52, 7, &FONT_T, P.chord ? "ON" : "OFF", C_WHITE);
        } else if (osd_kind == 12) {
            cv_text(8, 7, &FONT_T, "MUTATE", C_HI);
            cv_text(58, 7, &FONT_T, "a variation of the pattern", C_GRAY);
        } else if (osd_kind == 14) {
            cv_text(8, 7, &FONT_T, "SEQ", C_HI);
            cv_text(36, 7, &FONT_T, seq_mode == 0 ? "STEP" : seq_mode == 1 ? "SHAPE" : "GROOVE", C_WHITE);
        } else if (osd_kind >= 6) {
            int k = osd_kind - 6;
            cv_text(8, 7, &FONT_T, "K", C_HI);
            num_text(v, k + 1);
            cv_text(20, 7, &FONT_T, v, C_HI);
            cv_text(34, 7, &FONT_T, K_LABEL[page][k], C_WHITE);
        } else {
            cv_text(8, 7, &FONT_T, "SAVE", C_HI);
            cv_text(40, 7, &FONT_T, "stored to slot", C_GRAY);
            num_text(v, slot_index + 1);
            cv_text(140, 7, &FONT_T, v, C_WHITE);
        }
        cv_blit(0, 190);
    }
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

/* --------------------------------------------------------------- motif --- */

static uint32_t mrnd(void)
{
    mseed = mseed * 1664525u + 1013904223u;
    return (mseed >> 8) & 0xFFFFu;
}

/* a style and a seed become sixteen steps: rhythm first, then a note from the scale */
static void motif_generate(void)
{
    uint32_t seed = (uint32_t)P.seed * 2654435761u ^ ((uint32_t)P.genre + 1u) * 40503u;
    const int8_t *sc = SCALE_STEPS[P.scale & 3];
    int dens = P.density < 1 ? 1 : P.density;
    int i;
    int deg = 2;

    mseed = seed | 1u;
    for (i = 0; i < 16; i++) {
        int hit = 0, accent = 0;
        switch (P.genre) {
        case 0:                                     /* TECHNO: four on the floor */
            hit = (i % 4) == 0 || (mrnd() % 100) < (uint32_t)dens * 5u;
            accent = (i % 4) == 0;
            break;
        case 1:                                     /* HOUSE: offbeat */
            hit = (i % 4) == 2 || (mrnd() % 100) < (uint32_t)dens * 4u;
            accent = (i % 4) == 2;
            break;
        case 2:                                     /* ACID: dense sixteenths */
            hit = (mrnd() % 100) < 60u + (uint32_t)dens * 2u;
            accent = (i % 8) == 0;
            break;
        case 3:                                     /* BREAKS: syncopated */
            hit = (i % 8) == 0 || (i % 8) == 3 || (i % 8) == 6 || (mrnd() % 100) < (uint32_t)dens * 4u;
            accent = (i % 8) == 0;
            break;
        case 4:                                     /* AMBIENT: sparse */
            hit = (mrnd() % 100) < (uint32_t)dens * 2u;
            break;
        default:                                    /* TRANCE: eighths, arpeggio */
            hit = (i % 2) == 0;
            accent = (i % 8) == 0;
            break;
        }
        P.seq[i].on = (uint8_t)hit;
        P.seq[i].ratchet = 1; P.seq[i].prob = 100; P.seq[i].tie = 0;
        P.seq[i].ratchet = 1; P.seq[i].prob = 100; P.seq[i].tie = 0;
        if (!hit) { P.seq[i].note = 0; P.seq[i].vel = 0; P.seq[i].gate = 0; continue; }
        if (P.genre == 5) {
            deg = (deg + 1) % 7;                    /* an arpeggio walks the scale */
        } else if (P.genre == 4) {
            if ((mrnd() % 3) == 0) deg = (int)(mrnd() % 7);
        } else {
            int step = (int)(mrnd() % 5) - 2;       /* mostly stepwise */
            deg += step;
            if (deg < 0) deg += 7;
            deg %= 7;
        }
        P.seq[i].note = (int8_t)(sc[deg] + ((mrnd() % 4) == 0 ? 12 : 0));
        P.seq[i].vel = (uint8_t)(accent ? 120 : 78 + (int)(mrnd() % 26));
        P.seq[i].gate = (uint8_t)(35 + (mrnd() % 55));
        if (P.genre == 4) P.seq[i].gate = 90;
    }
}

/* euclid: hits spread over steps, rotated; the notes come from the scale */
static void euclid_generate(void)
{
    const int8_t *sc = SCALE_STEPS[P.scale & 3];
    int steps = P.e_steps < 2 ? 2 : (P.e_steps > 16 ? 16 : P.e_steps);
    int hits = P.e_hits < 1 ? 1 : (P.e_hits > steps ? steps : P.e_hits);
    int rot = P.e_rot % steps;
    int i, deg = 0;

    for (i = 0; i < 16; i++) {
        int hit = 0;
        if (i < steps) {
            int j = (i + rot) % steps;
            hit = ((j * hits) % steps) < hits;
        }
        P.seq[i].on = (uint8_t)hit;
        P.seq[i].ratchet = 1; P.seq[i].prob = 100; P.seq[i].tie = 0;
        P.seq[i].ratchet = 1; P.seq[i].prob = 100; P.seq[i].tie = 0;
        if (!hit) { P.seq[i].note = 0; P.seq[i].vel = 0; P.seq[i].gate = 0; continue; }
        deg = (deg + ((i % 3) == 0 ? 2 : 1)) % 7;
        P.seq[i].note = (int8_t)(sc[deg] + ((i % 8) == 0 ? 12 : 0));
        P.seq[i].vel = (uint8_t)((i % 4) == 0 ? 118 : 88);
        P.seq[i].gate = 45;
    }
}

/* mutate: keep the rhythm, nudge a few steps */
static void motif_mutate(void)
{
    int k, n = 0;
    mseed = (uint32_t)P.seed * 2246822519u + 7u;
    for (k = 0; k < 16; k++) {
        uint32_t r = mrnd() % 100u;
        if (r < 18u && n < 5) {
            n++;
            P.seq[k].on = (uint8_t)!P.seq[k].on;
            P.seq[k].ratchet = 1; P.seq[k].prob = 100; P.seq[k].tie = 0;
            if (P.seq[k].on) {
                const int8_t *sc = SCALE_STEPS[P.scale & 3];
                P.seq[k].note = (int8_t)(sc[mrnd() % 7] + ((mrnd() % 3) == 0 ? 12 : 0));
                P.seq[k].vel = (uint8_t)(80 + mrnd() % 40);
                P.seq[k].gate = (uint8_t)(40 + mrnd() % 50);
            }
        } else if (r < 30u && P.seq[k].on) {
            P.seq[k].vel = (uint8_t)clamp((int32_t)P.seq[k].vel + (int32_t)(mrnd() % 21) - 10, 8, 127);
        }
    }
}

/* ----------------------------------------------------------------- seq --- */
static void seq_rel(void)
{
    if (V[seq_voice].active && V[seq_voice].stage != 0 && V[seq_voice].stage != 4)
        V[seq_voice].stage = 4;
}

static void seq_stop(void)
{
    seq_rel();
    seq_at = 0xFF;
}

static uint8_t seq_ch_voice[3];
static uint8_t sub_left;                 /* ratchet sub-hits still to play */
static uint32_t sub_next, sub_ms;
static uint8_t sub_note, sub_vel;

static void seq_chord_rel(void)
{
    int k;
    for (k = 0; k < 3; k++)
        if (V[seq_ch_voice[k]].active && V[seq_ch_voice[k]].stage != 0 && V[seq_ch_voice[k]].stage != 4)
            V[seq_ch_voice[k]].stage = 4;
}

void app_seq_frame(uint32_t keys, uint32_t now)
{
    static uint32_t next_at, off_at;
    static uint8_t step;
    uint32_t ms = 15000u / (P.bpm ? P.bpm : 120u);
    uint8_t len = P.seq_len < 1 ? 1 : (P.seq_len > 16 ? 16 : P.seq_len);
    int i;
    static uint32_t bars;

    if (!P.seq_on) return;
    if (seq_at == 0xFF) { step = 0; next_at = now; off_at = 0; seq_at = 0; bars = 0; }
    if (off_at && now >= off_at) { seq_rel(); if (P.chord) seq_chord_rel(); off_at = 0; }
    if (sub_left && now >= sub_next) {
        sub_left--;
        sub_next += sub_ms;
        seq_voice = (uint8_t)((seq_voice + 1) % NV);
        voice_start(&V[seq_voice], sub_note, (float)sub_vel / 127.0f);
        off_at = now + sub_ms * 60u / 100u;
        return;
    }
    if (now >= next_at) {
        seqstep_t *st = &P.seq[step];
        uint8_t root = (uint8_t)(48 + P.key);
        for (i = 0; i < NKEYS; i++)
            if (keys & (1u << i)) { root = (uint8_t)(53 + i + P.key); break; }
        seq_rel();
        if (st->on && (uint32_t)(mrnd() % 100u) >= (uint32_t)st->prob) st->on = 2; /* skipped this pass */
        if (st->on == 2) st->on = 1;
        if (st->on) {
            const int8_t *sc = SCALE_STEPS[P.scale & 3];
            int third = P.chord ? sc[2] : 0;
            int fifth = P.chord ? sc[4] : 0;
            seq_voice = (uint8_t)((seq_voice + 1) % NV);
            voice_start(&V[seq_voice], (uint8_t)(root + st->note), (float)st->vel / 127.0f);
            if (P.chord) {
                seq_ch_voice[0] = seq_voice;
                seq_ch_voice[1] = (uint8_t)((seq_voice + 1) % NV);
                seq_ch_voice[2] = (uint8_t)((seq_voice + 2) % NV);
                voice_start(&V[seq_ch_voice[1]], (uint8_t)(root + st->note + third), (float)st->vel / 127.0f);
                voice_start(&V[seq_ch_voice[2]], (uint8_t)(root + st->note + fifth), (float)st->vel / 127.0f);
                seq_voice = seq_ch_voice[2];
            } else {
                seq_ch_voice[0] = seq_voice;
                seq_ch_voice[1] = seq_ch_voice[2] = seq_voice;
            }
            off_at = now + ms * (st->tie ? 98u : (uint32_t)fm_clampf((float)st->gate, 5.0f, 95.0f)) / 100u;
            if (st->ratchet > 1) {
                sub_left = (uint8_t)(st->ratchet - 1);
                sub_ms = ms / (uint32_t)st->ratchet;
                sub_next = now + sub_ms;
                sub_note = (uint8_t)(root + st->note);
                sub_vel = st->vel;
            }
        }
        next_at = now + ms + ((step & 1u) ? (ms * (uint32_t)P.swing) / 100u : 0u);
        step = (uint8_t)((step + 1) % len);
        seq_at = (uint8_t)step;
        if (++bars >= (uint32_t)len * 2u) {
            bars = 0;
            if (P.evolve) motif_mutate();
        }
    }
}

/* ---------------------------------------------------------------- lights --- */
#define ANIM_NONE 0
#define ANIM_SWEEP 1
#define ANIM_BLINK 2

static float key_glow[NKEYS];
static uint32_t anim_t0, anim_kind;

static const uint8_t PAGE_BTN[NPAGES] = {B_SEL, B_ENV, B_LFO, B_GLO, B_FX, B_ARP, B_SEQ, B_REC, B_SAVE};

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
    if (booting) {
        if (!boot_t0) boot_t0 = now;
        if (now - boot_t0 < BOOT_MS) {
            draw_boot(now - boot_t0);
            if ((btns || keys) && now - boot_t0 > 250u) {
                booting = 0;                      /* a touch skips the rest of the animation */
            } else {
                btn_prev = btns;
                return;
            }
        } else {
            booting = 0;
        }
    }
    uint32_t prev_btns = btn_prev;
    btn_prev = btns;
    if (P.seq_on) app_seq_frame(keys, now);
    else if (P.arp_on) app_arp_frame(keys, now);
    else key_edges(keys);
    plat_lights(0, keys, 0, 0, 1);

    /* ---- tap: a section button goes there, then walks its groups; hold: that section's action ---- */
    {
        uint32_t released = prev_btns & ~btns;
        uint32_t held = btns;
        int i;
        for (i = 0; i < NB; i++) {
            uint32_t bit = 1u << i;
            if (pressed & bit) { btn_down_at[i] = now; btn_hold_done[i] = 0; }
            if ((held & bit) && !btn_hold_done[i] && now - btn_down_at[i] > 650u) {
                btn_hold_done[i] = 1;
                hold_hits++;
                switch (i) {
                case B_HOME:                                  /* hold HOME: everything off */
                case B_PLAY:
                    P.seq_on = 0; P.arp_on = 0;
                    seq_stop(); arp_reset();
                    osd_kind = 4; osd_until = now + 900u;
                    break;
                case B_SAVE:                                  /* hold SAVE: store */
                    patch_save();
                    osd_kind = 5; osd_until = now + 900u;
                    anim_start(ANIM_SWEEP, now);
                    break;
                case B_FX: {                                  /* hold FX: on/off of the chosen effect */
                    fx_t *F = &P.fx[fx_sel];
                    F->on = (uint8_t)!F->on;
                    if (F->on && F->type == FX_OFF) F->type = FX_DRIVE;
                    osd_kind = 0;
                    break;
                }
                case B_REC:
                    if (page == PAGE_SEQ) {
                        P.chord = (uint8_t)!P.chord;
                        osd_kind = 11; osd_until = now + 900u;
                    }
                    break;
                case B_SEL:
                    if (page == PAGE_GEN) {
                        gen_mode = (uint8_t)!gen_mode;
                        osd_kind = 13; osd_until = now + 900u;
                    }
                    break;
                case B_GLO: {
                    if (page == PAGE_MOTIF) {
                        motif_mutate();
                        osd_kind = 12; osd_until = now + 900u;
                        anim_start(ANIM_SWEEP, now);
                        break;
                    }
                    {
                        route_t *R = &P.route[route_sel];
                        R->on = (uint8_t)!R->on;
                        if (R->on && R->src == SRC_OFF) { R->src = SRC_ENV2; R->dst = DST_CUT; R->amt = 0.30f; }
                    }
                    break;
                }
                case B_ARP:
                    P.arp_on = (uint8_t)!P.arp_on;
                    if (P.arp_on) { P.seq_on = 0; seq_stop(); }
                    else arp_reset();
                    break;
                case B_SEQ:
                    P.seq_on = (uint8_t)!P.seq_on;
                    if (P.seq_on) { P.arp_on = 0; arp_reset(); seq_stop(); }
                    else seq_stop();
                    break;
                default:
                    break;
                }
            } else if ((released & bit) && !btn_hold_done[i]) {
                switch (i) {
                case B_HOME:
                    page = PAGE_GEN; selected = 0;
                    anim_start(ANIM_BLINK, now);
                    break;
                case B_SEL:
                    selected = (uint8_t)((selected + 1) % NGEN);
                    osd_kind = 2; osd_until = now + 900u;
                    break;
                case B_EDIT:
                    page = (uint8_t)((page + 1) % NPAGES);
                    anim_start(ANIM_BLINK, now);
                    wipe_on = 1; wipe_t0 = now;
                    break;
                case B_GLO:
                    if (page != PAGE_MOD) { page = PAGE_MOD; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else route_sel = (uint8_t)((route_sel + 1) % NROUTE);
                    break;
                case B_ENV:
                    if (page != PAGE_ENV) { page = PAGE_ENV; env_sel = 0; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else env_sel = (uint8_t)!env_sel;
                    break;
                case B_LFO:
                    if (page != PAGE_LFO) { page = PAGE_LFO; lfo_sel = 0; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else lfo_sel = (uint8_t)!lfo_sel;
                    break;
                case B_FX:
                    if (page != PAGE_FX) { page = PAGE_FX; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else fx_sel = (uint8_t)((fx_sel + 1) % FX_MAX);
                    break;
                case B_ARP:
                    if (page != PAGE_ARP) { page = PAGE_ARP; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else {
                        P.arp_on = (uint8_t)!P.arp_on;
                        if (P.arp_on) { P.seq_on = 0; seq_stop(); } else arp_reset();
                    }
                    break;
                case B_SEQ:
                    if (page != PAGE_SEQ) { page = PAGE_SEQ; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else {
                        P.seq_on = (uint8_t)!P.seq_on;
                        if (P.seq_on) { P.arp_on = 0; arp_reset(); seq_stop(); } else seq_stop();
                    }
                    break;
                case B_SAVE:
                    if (page != PAGE_PRESET) { page = PAGE_PRESET; anim_start(ANIM_BLINK, now); wipe_on = 1; wipe_t0 = now; }
                    else {
                        slot_index = (uint8_t)((slot_index + 1) % NSLOT);
                        if (bank.magic == SAVE_MAGIC && bank.slot[slot_index].used) {
                            P = bank.slot[slot_index].p;
                            preset_index = bank.slot[slot_index].preset % 32;
                        }
                    }
                    break;
                case B_PLAY:
                    if (P.seq_on || P.arp_on) { P.seq_on = 0; P.arp_on = 0; seq_stop(); arp_reset(); }
                    else { P.seq_on = 1; P.arp_on = 0; seq_stop(); }
                    anim_start(ANIM_BLINK, now);
                    break;
                case B_REC: {
                    static uint32_t last_tap;
                    if (page == PAGE_MOTIF) {
                        P.motif_mode = (uint8_t)((P.motif_mode + 1) % 2);
                        osd_kind = 10; osd_until = now + 900u;
                        break;
                    }
                    if (page == PAGE_SEQ) {
                        seq_mode = (uint8_t)((seq_mode + 1) % 3);
                        osd_kind = 14; osd_until = now + 900u;
                        break;
                    }
                    if (last_tap && now - last_tap < 2000u) {
                        uint32_t ms = now - last_tap;
                        int32_t bpm = (int32_t)(60000u / (ms ? ms : 1u));
                        P.bpm = (uint16_t)clamp(bpm, 60, 200);
                        anim_start(ANIM_BLINK, now);
                    }
                    last_tap = now;
                    break;
                }
                case B_OCTUP: if (octave < 2) octave++; break;
                case B_OCTDN: if (octave > -2) octave--; break;
                default:
                    break;
                }
            }
        }
    }

    d = plat_enc(EN_PRESET);
    if (d) {
        preset_load((uint8_t)((preset_index + (d > 0 ? 1 : 31)) % 32));
        arp_reset();
        anim_start(ANIM_SWEEP, now);
        osd_kind = 1; osd_until = now + 1100u;
    }
    d = plat_enc(EN_SELECT);
    if (d) {
        page = (uint8_t)((page + (d > 0 ? 1 : NPAGES - 1)) % NPAGES);
        anim_start(ANIM_BLINK, now);
        wipe_on = 1; wipe_t0 = now;
        osd_kind = 3; osd_until = now + 1100u;
    }
    d = plat_enc(EN_ALGO);
    if (d) {
        selected = (uint8_t)((selected + (d > 0 ? 1 : NGEN - 1)) % NGEN);
        osd_kind = 2; osd_until = now + 1100u;
    }

    d = plat_enc(EN_K1);
    if (d) {
        osd_kind = 6; osd_until = now + 900u;
        switch (page) {
        case PAGE_GEN: {
            gen_t *G = &P.gen[selected];
            if (gen_mode == 0) {
                G->type = (uint8_t)(((G->type - 1 + (d > 0 ? 1 : 3)) % 4) + 1);
                G->on = 1;
                G->ftype = G->ftype ? G->ftype : F_LP;
            } else G->fm_src = (uint8_t)((G->fm_src + (d > 0 ? 1 : 4)) % 5);
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
        case PAGE_MACRO:
            macro_sel = (uint8_t)((macro_sel + (d > 0 ? 1 : 3)) & 3);
            break;
        case PAGE_ARP:
            P.arp_pattern = (uint8_t)((P.arp_pattern + (d > 0 ? 1 : 3)) & 3);
            break;
        case PAGE_SEQ:
            if (seq_mode == SEQ_GROOVE) P.seq_len = (uint8_t)clamp((int32_t)P.seq_len + (d > 0 ? 1 : -1), 1, 16);
            else seq_sel = (uint8_t)((seq_sel + (d > 0 ? 1 : 15)) & 15);
            break;
        case PAGE_MOTIF:
            if (P.motif_mode == 0) {
                P.genre = (uint8_t)((P.genre + (d > 0 ? 1 : 5)) % 6);
                motif_generate();
            } else if (P.motif_mode == 1) {
                P.e_steps = (uint8_t)clamp((int32_t)P.e_steps + (d > 0 ? 1 : -1), 2, 16);
                euclid_generate();
            } else {
                P.seq_len = (uint8_t)clamp((int32_t)P.seq_len + (d > 0 ? 1 : -1), 1, 16);
            }
            break;
        default:
            preset_load((uint8_t)((preset_index + (d > 0 ? 1 : 31)) % 32));
            arp_reset();
            anim_start(ANIM_SWEEP, now);
            break;
        }
    }
    d = plat_enc(EN_K2);
    if (d) {
        osd_kind = 7; osd_until = now + 900u;
        switch (page) {
        case PAGE_GEN: {
            gen_t *G = &P.gen[selected];
            if (gen_mode == 0) G->level = fm_clampf(G->level + (float)d * 0.02f, 0.0f, 1.0f);
            else G->fm_amt = fm_clampf(G->fm_amt + (float)d * 0.05f, 0.0f, 2.0f);
            break;
        }
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
            R->src = (uint8_t)((R->src + (d > 0 ? 1 : NSRC - 1)) % NSRC);
            if (R->src && !R->on) { R->on = 1; R->amt = 0.30f; }
            break;
        }
        case PAGE_FX: {
            fx_t *F = &P.fx[fx_sel];
            if (!F->on) { F->on = 1; if (F->type == FX_OFF) F->type = FX_DRIVE; }
            F->p1 = fm_clampf(F->p1 + (float)d * 0.02f, 0.0f, 1.0f);
            break;
        }
        case PAGE_MACRO:
            P.macro[macro_sel] = fm_clampf(P.macro[macro_sel] + (float)d * 0.05f, 0.0f, 1.0f);
            break;
        case PAGE_ARP:
            P.arp_rate = (uint8_t)((P.arp_rate + (d > 0 ? 1 : 4)) % 5);
            break;
        case PAGE_SEQ:
            if (seq_mode == SEQ_STEP) {
                int32_t n = (int32_t)P.seq[seq_sel].note + (d > 0 ? 1 : -1);
                P.seq[seq_sel].note = (int8_t)clamp(n, 0, 24);
                P.seq[seq_sel].on = 1;
            } else if (seq_mode == SEQ_SHAPE) {
                P.seq[seq_sel].ratchet = (uint8_t)clamp((int32_t)P.seq[seq_sel].ratchet + (d > 0 ? 1 : -1), 1, 4);
            } else {
                P.swing = (uint8_t)clamp((int32_t)P.swing + (d > 0 ? 5 : -5), 0, 60);
            }
            break;
        case PAGE_MOTIF:
            if (P.motif_mode == 0) {
                P.density = (uint8_t)clamp((int32_t)P.density + (d > 0 ? 1 : -1), 1, 16);
                motif_generate();
            } else if (P.motif_mode == 1) {
                P.e_hits = (uint8_t)clamp((int32_t)P.e_hits + (d > 0 ? 1 : -1), 1, P.e_steps);
                euclid_generate();
            } else {
                P.swing = (uint8_t)clamp((int32_t)P.swing + (d > 0 ? 5 : -5), 0, 60);
            }
            break;
        default:
            slot_pick(d > 0 ? 1 : -1);
            break;
        }
    }
    d = plat_enc(EN_K3);
    if (d) {
        osd_kind = 8; osd_until = now + 900u;
        switch (page) {
        case PAGE_GEN: {
            gen_t *G = &P.gen[selected];
            if (gen_mode == 0) G->pan = fm_clampf(G->pan + (float)d * 0.05f, -1.0f, 1.0f);
            else G->pwm = fm_clampf(G->pwm + (float)d * 0.02f, 0.05f, 0.95f);
            break;
        }
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
        case PAGE_MACRO:
            P.macro[macro_sel] = fm_clampf(P.macro[macro_sel] + (float)d * 0.05f, 0.0f, 1.0f);
            break;
        case PAGE_SEQ:
            if (seq_mode == SEQ_STEP) {
                int32_t vv = (int32_t)P.seq[seq_sel].vel + (d > 0 ? 8 : -8);
                P.seq[seq_sel].vel = (uint8_t)clamp(vv, 8, 127);
            } else if (seq_mode == SEQ_SHAPE) {
                P.seq[seq_sel].prob = (uint8_t)clamp((int32_t)P.seq[seq_sel].prob + (d > 0 ? 5 : -5), 0, 100);
            } else {
                P.key = (uint8_t)((P.key + (d > 0 ? 1 : 11)) % 12);
            }
            break;
        case PAGE_MOTIF:
            if (P.motif_mode == 0) {
                P.scale = (uint8_t)((P.scale + (d > 0 ? 1 : 3)) & 3);
                motif_generate();
            } else if (P.motif_mode == 1) {
                P.e_rot = (uint8_t)((P.e_rot + (d > 0 ? 1 : 15)) & 15);
                euclid_generate();
            } else {
                P.key = (uint8_t)((P.key + (d > 0 ? 1 : 11)) % 12);
            }
            break;
        }
    }
    d = plat_enc(EN_K4);
    if (d) {
        osd_kind = 9; osd_until = now + 900u;
        switch (page) {
        case PAGE_GEN: {
            gen_t *G = &P.gen[selected];
            if (gen_mode == 0) G->tune = (int8_t)clamp((int32_t)G->tune + (d > 0 ? 1 : -1), -24, 24);
            else G->cut = fm_clampf(G->cut * fm_exp2f((float)d * 0.2f), 20.0f, 18000.0f);
            break;
        }
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
        case PAGE_MACRO:
            P.macro[macro_sel] = fm_clampf(P.macro[macro_sel] + (float)d * 0.05f, 0.0f, 1.0f);
            break;
        case PAGE_SEQ:
            if (seq_mode == SEQ_STEP) {
                int32_t gt = (int32_t)P.seq[seq_sel].gate + (d > 0 ? 5 : -5);
                P.seq[seq_sel].gate = (uint8_t)clamp(gt, 5, 95);
            } else if (seq_mode == SEQ_SHAPE) {
                P.seq[seq_sel].tie = (uint8_t)!P.seq[seq_sel].tie;
            } else {
                P.evolve = (uint8_t)!P.evolve;
            }
            break;
        case PAGE_MOTIF:
            if (P.motif_mode == 0) {
                P.seed = (uint16_t)((P.seed + (d > 0 ? 1 : 9999)) % 10000);
                motif_generate();
                anim_start(ANIM_SWEEP, now);
            } else if (P.motif_mode == 1) {
                euclid_generate();
                anim_start(ANIM_SWEEP, now);
            } else {
                P.evolve = (uint8_t)!P.evolve;
            }
            break;
        }
    }

    lights_frame(keys, pressed, now);
    draw();
}

/* the first screen: the ground and a seed, so the tree can grow out of it */
void app_splash(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    mseed = 0x51EDu;
    cv_begin(240, 140, C_BLACK);
    cv_rect(58, 136, 124, 1, RGB(30, 42, 54));
    boot_branch(120, 134, -1.5708f, 40.0f, 0, 1.0f);
    cv_blit(0, 0);
    lcd_sync();
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
    if (!strcmp(what, "seq")) { *got = P.seq_on; return 1; }
    if (!strcmp(what, "genre")) { *got = P.genre; return 1; }
    if (!strcmp(what, "density")) { *got = P.density; return 1; }
    if (!strcmp(what, "seed")) { *got = P.seed; return 1; }
    if (!strcmp(what, "motif")) { motif_generate(); *got = 1; return 1; }
    if (!strcmp(what, "nroute")) { *got = NROUTE; return 1; }
    if (!strcmp(what, "nsrc")) { *got = NSRC; return 1; }
    if (!strcmp(what, "macro1")) { *got = (int32_t)(P.macro[0] * 100.0f); return 1; }
    if (!strcmp(what, "macro4")) { *got = (int32_t)(P.macro[3] * 100.0f); return 1; }
    if (!strcmp(what, "macsel")) { *got = macro_sel; return 1; }
    if (!strcmp(what, "fmsrc")) { *got = P.gen[selected].fm_src; return 1; }
    if (!strcmp(what, "fmamt")) { *got = (int32_t)(P.gen[selected].fm_amt * 100.0f); return 1; }
    if (!strcmp(what, "route")) { *got = route_sel; return 1; }
    if (!strcmp(what, "routesrc")) { *got = P.route[route_sel].src; return 1; }
    if (!strcmp(what, "seqmode")) { *got = seq_mode; return 1; }
    if (!strcmp(what, "genmode")) { *got = gen_mode; return 1; }
    if (!strcmp(what, "ratchet")) { *got = P.seq[seq_sel].ratchet; return 1; }
    if (!strcmp(what, "prob")) { *got = P.seq[seq_sel].prob; return 1; }
    if (!strcmp(what, "tie")) { *got = P.seq[seq_sel].tie; return 1; }
    if (!strcmp(what, "mmode")) { *got = P.motif_mode; return 1; }
    if (!strcmp(what, "esteps")) { *got = P.e_steps; return 1; }
    if (!strcmp(what, "ehits")) { *got = P.e_hits; return 1; }
    if (!strcmp(what, "chord")) { *got = P.chord; return 1; }
    if (!strcmp(what, "len")) { *got = P.seq_len; return 1; }
    if (!strcmp(what, "swing")) { *got = P.swing; return 1; }
    if (!strcmp(what, "key")) { *got = P.key; return 1; }
    if (!strcmp(what, "evolve")) { *got = P.evolve; return 1; }
    if (!strcmp(what, "mmode2")) { *got = P.motif_mode; return 1; }
    if (!strcmp(what, "mutate")) { motif_mutate(); *got = 1; return 1; }
    if (!strcmp(what, "motifon")) { int c = 0, k; for (k = 0; k < 16; k++) if (P.seq[k].on) c++; *got = c; return 1; }
    if (!strcmp(what, "seqstep")) { *got = seq_at; return 1; }
    if (!strcmp(what, "seqsel")) { *got = seq_sel; return 1; }
    if (!strcmp(what, "seqnote")) { *got = P.seq[seq_sel].note; return 1; }
    if (!strcmp(what, "holds")) { *got = hold_hits; return 1; }
    if (!strcmp(what, "fxin")) { *got = (int)(fx_in_peak * 1000.0f); fx_in_peak = 0.0f; return 1; }
    return 0;
}
#endif

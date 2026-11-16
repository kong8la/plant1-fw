/* SPDX-License-Identifier: MIT
 * PLANT-1 M0 — one FM voice, one ADSR, 27 keys, one screen.
 *
 * The point of M0 is the whole chain: build -> package -> flash -> a key makes a sound.
 * The engine is deliberately tiny; M1 replaces voice() with the real generator/filter code. */
#include "fastmath.h"

/* ---------------------------------------------------------------- state --- */
static int32_t note = -1;          /* the note the voice plays, -1 silent */
static float phase, mphase;        /* carrier and modulator phase, 0..1 */

/* ADSR, milliseconds */
#define ENV_A_MS 4.0f
#define ENV_D_MS 260.0f
#define ENV_S 0.55f
#define ENV_R_MS 220.0f
static float env;                  /* 0..1 */
static int env_stage;              /* 0 idle, 1 attack, 2 decay, 3 sustain, 4 release */

/* the voice's own knobs (M0: fixed; M1 reads them from the panel) */
#define FM_RATIO 2.0f
#define FM_INDEX 3.2f
#define VOICE_GAIN 0.22f

static uint32_t lit_keys;          /* for the LEDs */

/* ---------------------------------------------------------------- names --- */
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* --------------------------------------------------------------- engine --- */
static void env_gate(int on)
{
    if (on) {
        env_stage = 1;
    } else if (env_stage != 0) {
        env_stage = 4;
    }
}

static float env_tick(void)
{
    /* exponential segments, per sample: k = 1 - exp(-1/(ms*44.1)) */
    const float ka = 1.0f - fm_exp2f(-1.0f / (ENV_A_MS * 44.1f) * 1.4427f);   /* ln(2) ~ 1.4427 for exp2 */
    const float kd = 1.0f - fm_exp2f(-1.0f / (ENV_D_MS * 44.1f) * 1.4427f);
    const float kr = 1.0f - fm_exp2f(-1.0f / (ENV_R_MS * 44.1f) * 1.4427f);
    switch (env_stage) {
    case 1:
        env += (1.0f - env) * ka;
        if (env >= 0.999f) {
            env = 1.0f;
            env_stage = 2;
        }
        break;
    case 2:
        env += (ENV_S - env) * kd;
        if (env <= ENV_S + 0.001f) {
            env = ENV_S;
            env_stage = 3;
        }
        break;
    case 3:
        env = ENV_S;
        break;
    case 4:
        env += (0.0f - env) * kr;
        if (env < 0.0005f) {
            env = 0.0f;
            env_stage = 0;
        }
        break;
    default:
        env = 0.0f;
        break;
    }
    return env;
}

void app_init(void)
{
    note = -1;
    phase = mphase = 0.0f;
    env = 0.0f;
    env_stage = 0;
    lit_keys = 0;
}

void app_panic(void)
{
    note = -1;
    env = 0.0f;
    env_stage = 0;
    phase = mphase = 0.0f;
}

/* the audio interrupt: n stereo frames, 24-bit, MASTER already a gain */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    float inc = note >= 0 ? 440.0f * fm_exp2f((float)(note - 69) / 12.0f) / 44100.0f : 0.0f;
    for (i = 0; i < n; i++) {
        float e = env_tick();
        float s, mod;
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;
        mphase += inc * FM_RATIO;
        if (mphase >= 1.0f)
            mphase -= 1.0f;
        mod = fm_sinf(mphase * 6.2831853f);
        s = fm_sinf(phase * 6.2831853f + FM_INDEX * mod) * e * VOICE_GAIN * gain;
        lr[2 * i] = (int32_t)s;
        lr[2 * i + 1] = (int32_t)s;
    }
}

/* ------------------------------------------------------------------ UI --- */
void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
}

static void note_text(char *out, int32_t m)
{
    int32_t oct, k = 0;
    if (m < 0) {
        out[0] = '-';
        out[1] = 0;
        return;
    }
    while (NOTE_NAME[((m % 12) + 12) % 12][k]) {   /* "C" .. "A#": copy the name, then the octave */
        out[k] = NOTE_NAME[((m % 12) + 12) % 12][k];
        k++;
    }
    oct = m / 12 - 1;
    out[k] = (char)('0' + (oct < 0 ? 0 : oct));
    out[k + 1] = 0;
}

void app_frame(void)
{
    uint32_t keys = plat_keys();
    uint32_t i;
    int32_t n = -1;
    char t[8];

    for (i = 0; i < NKEYS; i++) {
        if (keys & (1u << i)) {
            n = (int32_t)(53 + i);         /* key 0 is F3 */
            break;
        }
    }
    if (n != note) {
        note = n;
        env_gate(n >= 0 ? 1 : 0);
    }

    if (keys != lit_keys) {
        lit_keys = keys;
        plat_lights(0, keys, 0, 0, 1);
    }

    /* the screen: bands through the canvas, then a blit each (gfx.c) */
    note_text(t, note);
    cv_begin(240, 60, C_BLACK);
    cv_text(12, 6, &FONT_B, "PLANT-1 M0", C_WHITE);
    cv_text(12, 28, &FONT_S, "FM VOICE", C_GRAY);
    cv_text(12, 44, &FONT_XS, note >= 0 ? "KEY DOWN" : "READY", note >= 0 ? C_HI : C_DIM);
    cv_blit(0, 0);

    cv_begin(240, 80, C_BLACK);
    cv_text(12, 10, &FONT_L, t, note >= 0 ? C_HI : C_DIM);
    cv_text(150, 20, &FONT_S, "ADSR", C_GRAY);
    cv_text(150, 40, &FONT_XS, "FM 1:2", C_DIM);
    cv_blit(0, 66);

    cv_begin(240, 60, C_BLACK);
    cv_text(12, 2, &FONT_XS, "LEVEL", C_GRAY);
    cv_rect(12, 18, 216, 8, C_LINE);
    cv_rect(12, 18, (int32_t)(env * 216.0f), 8, C_HI);
    cv_text(12, 36, &FONT_S, "KEYS 27 / 1 VOICE", C_GRAY);
    cv_blit(0, 156);
}

void app_input_only(void) {}

void app_say(const char *a, const char *b)
{
    (void)a;
    (void)b;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "note")) {
        *got = (int)note;
        return 1;
    }
    if (!strcmp(what, "stage")) {
        *got = env_stage;
        return 1;
    }
    if (!strcmp(what, "env")) {
        *got = (int)(env * 1000.0f);
        return 1;
    }
    return 0;
}
#endif

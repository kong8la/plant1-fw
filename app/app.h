/* SPDX-License-Identifier: MIT
 * PLANT-1 M8: every button and encoder does its label; declicked voices and a soft output. */
#pragma once
#define APP_PALETTE 2u          /* gfx.c PALETTES: CYAN, the browser PLANT-1 accent */
#include <stdint.h>
#include "plat.h"

#define APP_NAME "PLANT1M16"
#define APP_SPLASH_RGB RGB(56, 189, 248)
#define APP_BLOCK 256u

enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#ifdef PLAT_HOST
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#endif

/*
 * i_system_esp32.c - ESP32 platform layer for GBADoom
 *
 * Implements all functions declared in i_system_e32.h plus I_Error.
 * Replaces i_system_gba.cpp (GBA) and i_system_e32.cpp (Windows/Qt).
 *
 * Display: ST7789P3 240x320 RGB565 via bsp_doom
 * Input:   3 ADC buttons (UP/DOWN/OK) via bsp_doom
 * Timing:  esp_timer for I_GetTime (via clock() in r_hotpath.iwram.c)
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomdef.h"
#include "doomtype.h"
#include "d_event.h"
#include "d_main.h"
#include "global_data.h"
#include "i_system_e32.h"
#include "lprintf.h"

#include "bsp_doom.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "i_sound.h"

static const char *TAG = "doom_plat";

/* ---- Framebuffers ---- */
// SCREENWIDTH=120, SCREENHEIGHT=160, unsigned short = 2 bytes
// Single buffer: 120 * 160 * 2 = 38,400 bytes (~37.5 KB)
// No need for double-buffer since we blit to SPI (no page flip)
static unsigned short *s_backbuffer;

/* ---- Palette ---- */
// Store palette as RGB565 shorts (matching ST7789 native format)
static unsigned short s_palette[256];

/* ---- Input state ---- */
static int s_last_btn = -1;   // -1 = no button

/* ---- Display geometry ----
 * Doom renders at 120x160, screen is 240x320.
 * Center the viewport: x_offset=60, y_offset=80.
 */
#define DOOM_X_OFFSET  ((BSP_LCD_W - SCREENWIDTH) / 2)
#define DOOM_Y_OFFSET  ((BSP_LCD_H - SCREENHEIGHT) / 2)

/* ---- Byte swap for SPI (LE CPU -> BE display) ---- */
static inline unsigned short bswap16(unsigned short v)
{
    return (unsigned short)((v >> 8) | (v << 8));
}

// ==============================================================
// Platform interface (i_system_e32.h)
// ==============================================================

// I_Init: called before Z_Init, inits sound subsystem.
// (Replaces the one in i_main.c which also has main() we can't use)
void I_Init(void)
{
    I_InitSound();
}

void I_InitScreen_e32(void)
{
    // BSP already initialized in app_main(), nothing extra needed
    ESP_LOGI(TAG, "Platform screen init (BSP already up)");
}

void I_CreateBackBuffer_e32(void)
{
    s_backbuffer = malloc(SCREENWIDTH * SCREENHEIGHT * sizeof(unsigned short));

    if (!s_backbuffer) {
        ESP_LOGE(TAG, "Framebuffer alloc failed!");
        abort();
    }

    memset(s_backbuffer, 0, SCREENWIDTH * SCREENHEIGHT * 2);

    ESP_LOGI(TAG, "Framebuffer: %p (%d bytes)",
             s_backbuffer, SCREENWIDTH * SCREENHEIGHT * 2);

    // Clear both pages on the display
    I_FinishUpdate_e32(NULL, NULL, 0, 0);
}

int I_GetVideoWidth_e32(void)
{
    return SCREENWIDTH;   // 120
}

int I_GetVideoHeight_e32(void)
{
    return SCREENHEIGHT;  // 160
}

unsigned short* I_GetBackBuffer(void)
{
    return s_backbuffer;
}

unsigned short* I_GetFrontBuffer(void)
{
    // Single buffer mode: front == back
    return s_backbuffer;
}

/*
 * I_SetPallete_e32 - Convert Doom's 256-color RGB palette to RGB565.
 *
 * Input: 768 bytes (256 * 3: R, G, B each 8-bit).
 * Output: 256 RGB565 shorts stored in s_palette[].
 * The renderer writes these shorts directly into the backbuffer.
 */
void I_SetPallete_e32(const byte *palette)
{
    if (!palette) return;

    for (int i = 0; i < 256; i++) {
        unsigned int r = *palette++;
        unsigned int g = *palette++;
        unsigned int b = *palette++;

        // RGB565: 5-bit red, 6-bit green, 5-bit blue
        // Byte-swap for SPI big-endian display
        unsigned short rgb565 = (unsigned short)(
            ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        );
        s_palette[i] = bswap16(rgb565);
    }
}

/*
 * I_FinishUpdate_e32 - Flush backbuffer to ST7789 display.
 *
 * The backbuffer already contains RGB565 data (with correct byte order)
 * because the palette was pre-converted. We just need to blit it.
 */
void I_FinishUpdate_e32(const byte *srcBuffer, const byte *palette,
                        const unsigned int width, const unsigned int height)
{
    if (!s_backbuffer) return;

    // Blit 120x160 framebuffer centered on 240x320 display
    bsp_display_draw_bitmap(
        DOOM_X_OFFSET, DOOM_Y_OFFSET,
        SCREENWIDTH, SCREENHEIGHT,
        s_backbuffer
    );
}

/*
 * I_ProcessKeyEvents - Poll ADC buttons and post Doom events.
 *
 * Button mapping (normal mode):
 *   UP   -> KEYD_UP    (forward)
 *   DOWN -> KEYD_DOWN  (backward)
 *   OK   -> KEYD_A     (fire/use)
 *
 * TODO Phase 4: Long-press OK (500ms) toggles "turn mode"
 * where UP=KEYD_LEFT, DOWN=KEYD_RIGHT.
 */
void I_ProcessKeyEvents(void)
{
    bsp_btn_t btn;
    int cur = bsp_button_read(&btn);

    if (cur != s_last_btn) {
        event_t ev;

        // Release previous button
        if (s_last_btn >= 0) {
            ev.type = ev_keyup;
            switch (s_last_btn) {
                case BSP_BTN_UP:   ev.data1 = KEYD_UP;   break;
                case BSP_BTN_DOWN: ev.data1 = KEYD_DOWN; break;
                case BSP_BTN_OK:   ev.data1 = KEYD_A;    break;
                default: return;
            }
            D_PostEvent(&ev);
        }

        // Press new button
        if (cur >= 0) {
            ev.type = ev_keydown;
            switch (cur) {
                case BSP_BTN_UP:   ev.data1 = KEYD_UP;   break;
                case BSP_BTN_DOWN: ev.data1 = KEYD_DOWN; break;
                case BSP_BTN_OK:   ev.data1 = KEYD_A;    break;
                default: return;
            }
            D_PostEvent(&ev);
        }

        s_last_btn = cur;
    }
}

/*
 * I_Error - Fatal error handler.
 * Prints to serial console and halts.
 */
#define MAX_MESSAGE_SIZE 512

void I_Error(const char *error, ...)
{
    char msg[MAX_MESSAGE_SIZE];
    va_list v;
    va_start(v, error);
    vsnprintf(msg, sizeof(msg), error, v);
    va_end(v);

    ESP_LOGE("DOOM", "I_Error: %s", msg);

    // Draw red error screen
    if (s_backbuffer) {
        for (int i = 0; i < SCREENWIDTH * SCREENHEIGHT; i++) {
            // Red in byte-swapped RGB565: 0xF800 -> bswap -> 0x00F8
            s_backbuffer[i] = bswap16(0xF800);
        }
        bsp_display_draw_bitmap(
            DOOM_X_OFFSET, DOOM_Y_OFFSET,
            SCREENWIDTH, SCREENHEIGHT,
            s_backbuffer
        );
    }

    // Halt forever
    while (1) {
        vTaskDelay(portMAX_DELAY);
    }
}

/*
 * I_Quit_e32 - Quit handler (no-op, engine loop never exits).
 */
void I_Quit_e32(void)
{
    ESP_LOGW(TAG, "I_Quit_e32 called (no-op)");
}

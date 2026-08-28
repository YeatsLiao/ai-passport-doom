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
#include <time.h>

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
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i_sound.h"

static const char *TAG = "doom_plat";

/* ---- Time base ----
 * GBADoom's non-GBA I_GetTime() derives game tics from clock() (newlib).
 * On ESP-IDF, newlib's clock() always returns 0, which makes the engine's
 * timing loops (TryRunTics / D_Wipe) busy-spin forever and starve the IDLE
 * task (task watchdog). Redirect clock() here via the linker --wrap option
 * (see CMakeLists.txt). Must return MICROseconds: I_GetTime divides the
 * clock value by CLOCKS_PER_SEC/TICRATE (~28571), so anything coarser than
 * microseconds rounds down to 0 and the engine sees time stand still.
 */
clock_t __wrap_clock(void)
{
    return (clock_t)esp_timer_get_time();
}

/* ---- Framebuffers ---- */
// The engine renders 240x160 8-BIT palette indices (SCREENWIDTH=120 in
// doomdef.h is measured in shorts; byte pitch is 240). s_backbuffer holds
// those indices; s_rgb565 is the converted 16bpp staging buffer we blit.
static unsigned short *s_backbuffer;   // 240*160 bytes (engine writes here)
static unsigned short *s_rgb565;       // 240*160 shorts (converted pixels)

/* ---- Palette ---- */
// Store palette as RGB565 shorts (matching ST7789 native format)
static unsigned short s_palette[256];

/* ---- Input state ---- */
static int s_last_btn = -1;   // -1 = no button

/* ---- Display geometry ----
 * Doom renders at 240x160 (8bpp), screen is 240x320.
 * Full width, vertically centered: y_offset=80.
 */
#define DOOM_FB_W      240
#define DOOM_FB_H      160
#define DOOM_X_OFFSET  ((BSP_LCD_W - DOOM_FB_W) / 2)
#define DOOM_Y_OFFSET  ((BSP_LCD_H - DOOM_FB_H) / 2)

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
    // Engine framebuffer: 240x160 bytes of palette indices
    s_backbuffer = malloc(DOOM_FB_W * DOOM_FB_H);
    // Staging buffer: converted RGB565 pixels for SPI blit
    s_rgb565 = malloc(DOOM_FB_W * DOOM_FB_H * sizeof(unsigned short));

    if (!s_backbuffer || !s_rgb565) {
        ESP_LOGE(TAG, "Framebuffer alloc failed!");
        abort();
    }

    memset(s_backbuffer, 0, DOOM_FB_W * DOOM_FB_H);
    memset(s_rgb565, 0, DOOM_FB_W * DOOM_FB_H * 2);

    ESP_LOGI(TAG, "Framebuffer: %p (%d bytes 8bpp) + %p (rgb565)",
             s_backbuffer, DOOM_FB_W * DOOM_FB_H, s_rgb565);

    // Clear the whole 240x320 panel (leftovers from previous firmware)
    for (int y = 0; y < BSP_LCD_H; y += DOOM_FB_H) {
        bsp_display_draw_bitmap(0, y, BSP_LCD_W, DOOM_FB_H, s_rgb565);
    }

    I_FinishUpdate_e32(NULL, NULL, 0, 0);
}

int I_GetVideoWidth_e32(void)
{
    return DOOM_FB_W;     // 240 (bytes; engine counts 120 shorts)
}

int I_GetVideoHeight_e32(void)
{
    return DOOM_FB_H;     // 160
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
 * I_FinishUpdate_e32 - Convert the 240x160 8bpp index buffer to RGB565
 * and flush it to the ST7789 display.
 */
void I_FinishUpdate_e32(const byte *srcBuffer, const byte *palette,
                        const unsigned int width, const unsigned int height)
{
    if (!s_backbuffer || !s_rgb565) return;

    // D_DoomLoop runs the engine flat-out; yield once every few frames so
    // the IDLE task can feed the task watchdog.
    static unsigned int frame_count;
    if ((++frame_count & 0x3) == 0) {
        vTaskDelay(1);
    }

    // 8bpp palette indices -> RGB565 via the preconverted palette
    const byte *src = (const byte *)s_backbuffer;
    unsigned short *dst = s_rgb565;
    const int npix = DOOM_FB_W * DOOM_FB_H;

    for (int i = 0; i < npix; i++) {
        dst[i] = s_palette[src[i]];
    }

    // Blit 240x160 framebuffer vertically centered on 240x320 display
    bsp_display_draw_bitmap(
        DOOM_X_OFFSET, DOOM_Y_OFFSET,
        DOOM_FB_W, DOOM_FB_H,
        s_rgb565
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

    // Draw red error screen (fill the rgb565 staging buffer)
    if (s_rgb565) {
        for (int i = 0; i < DOOM_FB_W * DOOM_FB_H; i++) {
            // Red in byte-swapped RGB565: 0xF800 -> bswap -> 0x00F8
            s_rgb565[i] = bswap16(0xF800);
        }
        bsp_display_draw_bitmap(
            DOOM_X_OFFSET, DOOM_Y_OFFSET,
            DOOM_FB_W, DOOM_FB_H,
            s_rgb565
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

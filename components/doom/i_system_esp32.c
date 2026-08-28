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
// those indices. We convert to RGB565 in strips to save RAM.
static unsigned short *s_backbuffer;   // FB_W * FB_H bytes (engine writes here)
static unsigned short *s_linebuf;      // FB_W * STRIP_H shorts (strip staging)

/* ---- Palette ---- */
// Store palette as RGB565 shorts (matching ST7789 native format)
static unsigned short s_palette[256];

/* ---- Input state ---- */
static int s_last_btn = -1;   // -1 = no button

/* ---- Display geometry ----
 * Doom renders 240x160 (8bpp), screen is 240x320.
 * Full width, vertically centered at y=80.
 */
#define FB_W           240    // engine framebuffer width (bytes)
#define FB_H           160    // engine framebuffer height
#define DISP_X_OFF     0      // full width
#define DISP_Y_OFF     ((BSP_LCD_H - FB_H) / 2)  // 80
#define STRIP_H        10     // source rows per blit strip

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
    s_backbuffer = malloc(FB_W * FB_H);
    // Strip staging buffer: FB_W * STRIP_H shorts (4.7KB per strip)
    s_linebuf = malloc(FB_W * STRIP_H * sizeof(unsigned short));

    if (!s_backbuffer || !s_linebuf) {
        ESP_LOGE(TAG, "Framebuffer alloc failed! backbuffer=%p linebuf=%p",
                 s_backbuffer, s_linebuf);
        abort();
    }

    memset(s_backbuffer, 0, FB_W * FB_H);

    ESP_LOGI(TAG, "FB: %p (%d bytes 8bpp) + %p (linebuf %dx%d shorts)",
             s_backbuffer, FB_W * FB_H,
             s_linebuf, FB_W, STRIP_H);

    // Clear the whole 240x320 panel (leftovers from previous firmware)
    memset(s_linebuf, 0, FB_W * STRIP_H * sizeof(unsigned short));
    for (int y = 0; y < BSP_LCD_H; y += STRIP_H) {
        int h = (y + STRIP_H > BSP_LCD_H) ? BSP_LCD_H - y : STRIP_H;
        bsp_display_draw_bitmap(0, y, BSP_LCD_W, h, s_linebuf);
    }

    I_FinishUpdate_e32(NULL, NULL, 0, 0);
}

int I_GetVideoWidth_e32(void)
{
    return FB_W;     // 240 (bytes; engine counts 120 shorts)
}

int I_GetVideoHeight_e32(void)
{
    return FB_H;     // 160
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
 * and flush it to the ST7789 display in strips (no rotation).
 */
void I_FinishUpdate_e32(const byte *srcBuffer, const byte *palette,
                        const unsigned int width, const unsigned int height)
{
    if (!s_backbuffer || !s_linebuf) return;

    // D_DoomLoop runs the engine flat-out; yield once every few frames so
    // the IDLE task can feed the task watchdog.
    static unsigned int frame_count;
    if ((++frame_count & 0x3) == 0) {
        vTaskDelay(1);
    }

    const byte *src = (const byte *)s_backbuffer;

    // Convert and blit in strips of STRIP_H rows (no rotation)
    for (int y = 0; y < FB_H; y += STRIP_H) {
        int sh = (y + STRIP_H > FB_H) ? FB_H - y : STRIP_H;
        int npix = FB_W * sh;
        const byte *row_src = src + y * FB_W;

        for (int i = 0; i < npix; i++) {
            s_linebuf[i] = s_palette[row_src[i]];
        }

        bsp_display_draw_bitmap(
            DISP_X_OFF, DISP_Y_OFF + y,
            FB_W, sh,
            s_linebuf
        );
    }
}

/*
 * I_ProcessKeyEvents - Poll ADC buttons and post Doom events.
 *
 * Long-press flip scheme: each button has 2 actions, no mode toggle needed.
 * Short press = primary key (immediate). Hold > LONG_FLIP_US = secondary key.
 * Release resets. This gives 6 actions from 3 buttons naturally.
 *
 *   UP   short: KEYD_UP     (forward)     long: KEYD_DOWN  (backward)
 *   DOWN short: KEYD_RIGHT  (turn right)  long: KEYD_LEFT  (turn left)
 *   OK   short: KEYD_B      (fire)        long: KEYD_A     (use / door)
 *
 * Note: KEYD_A doubles as key_speed (run) in GBADoom g_game.c,
 *       so long-press OK also makes you run while held.
 */
#define LONG_FLIP_US  500000  // 500 ms

void I_ProcessKeyEvents(void)
{
    bsp_btn_t btn;
    int cur = bsp_button_read(&btn);

    static int64_t press_start  = 0;
    static bool    is_secondary = false;
    static int     active_key   = -1;   // currently held doom key

    // --- detect button change ---
    if (cur != s_last_btn) {
        // Release previous key
        if (active_key >= 0) {
            event_t ev = { .type = ev_keyup, .data1 = active_key };
            D_PostEvent(&ev);
            active_key = -1;
        }
        is_secondary = false;

        if (cur >= 0) {
            // New button pressed: send primary key immediately
            press_start = esp_timer_get_time();
            int key;
            switch (cur) {
                case BSP_BTN_UP:   key = KEYD_UP;    break;
                case BSP_BTN_DOWN: key = KEYD_RIGHT; break;
                case BSP_BTN_OK:   key = KEYD_B;     break;
                default: return;
            }
            event_t ev = { .type = ev_keydown, .data1 = key };
            D_PostEvent(&ev);
            active_key = key;
        } else {
            press_start = 0;
        }
        s_last_btn = cur;
        return;
    }

    // --- same button still held: check for long-press flip ---
    if (cur >= 0 && !is_secondary && press_start > 0) {
        if (esp_timer_get_time() - press_start > LONG_FLIP_US) {
            is_secondary = true;
            // Release primary key
            if (active_key >= 0) {
                event_t ev = { .type = ev_keyup, .data1 = active_key };
                D_PostEvent(&ev);
            }
            // Send secondary key
            int key;
            switch (cur) {
                case BSP_BTN_UP:   key = KEYD_DOWN; break;
                case BSP_BTN_DOWN: key = KEYD_LEFT; break;
                case BSP_BTN_OK:   key = KEYD_A;    break;
                default: return;
            }
            event_t ev = { .type = ev_keydown, .data1 = key };
            D_PostEvent(&ev);
            active_key = key;
        }
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

    // Draw red error screen (strip-based, no rotation)
    if (s_linebuf) {
        for (int i = 0; i < FB_W * STRIP_H; i++) {
            s_linebuf[i] = bswap16(0xF800);
        }
        for (int y = 0; y < FB_H; y += STRIP_H) {
            int sh = (y + STRIP_H > FB_H) ? FB_H - y : STRIP_H;
            bsp_display_draw_bitmap(
                DISP_X_OFF, DISP_Y_OFF + y,
                FB_W, sh,
                s_linebuf
            );
        }
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

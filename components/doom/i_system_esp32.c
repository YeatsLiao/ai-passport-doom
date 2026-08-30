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
// ESP32: static framebuffer + linebuf in .bss (avoids heap pressure)
// FB_W=240, FB_H=160 defined below; hardcode here for file-scope array sizes
static byte s_backbuffer_data[240 * 160];           // 38400 bytes
static unsigned short s_linebuf_data[240 * 16];     // 7680 bytes (16 rows max)
static unsigned short *s_backbuffer = (unsigned short *)s_backbuffer_data;
static unsigned short *s_linebuf = s_linebuf_data;

/* ---- Palette ---- */
// Store palette as RGB565 shorts (matching ST7789 native format)
static unsigned short s_palette[256];

/* ---- Input state ---- */
static int s_last_btn = -1;   // -1 = no button

/* ---- Display geometry ----
 * Doom renders 240x160 (8bpp), physical panel is 240x320.
 * We scale 2x VERTICALLY so the game fills the whole panel: each game row
 * is drawn as two display rows (VSCALE=2). Fullscreen removes the centered
 * window boundary that produced the bottom HUD overlap artifact.
 */
#define FB_W           240    // engine framebuffer width (bytes)
#define FB_H           160    // engine framebuffer height
#define DISP_X_OFF     0      // full width
#define VSCALE         2      // vertical scale: 160 game rows -> 320 display
#define STRIP_H        8      // source (game) rows converted per blit strip
#define DISP_BOTTOM_BLACK 16  // display rows masked by black bar at bottom

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
    // ESP32: framebuffer and linebuf are static arrays, no malloc needed
    memset(s_backbuffer_data, 0, sizeof(s_backbuffer_data));
    memset(s_linebuf_data, 0, sizeof(s_linebuf_data));

    ESP_LOGI(TAG, "FB: %p (%d bytes 8bpp static) + %p (linebuf %dx%d shorts, %d bytes static)",
             s_backbuffer_data, FB_W * FB_H,
             s_linebuf_data, FB_W, 16, FB_W * 16 * 2);

    // Clear the whole 240x320 panel (leftovers from previous firmware)
    memset(s_linebuf_data, 0, sizeof(s_linebuf_data));
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
 * draw_text_on_blackbar - Render "AI-PASSPORT-DOOM" in red on the bottom
 * black bar. Uses pre-rendered bitmap to avoid font rendering issues.
 */
static void draw_text_on_blackbar(unsigned short *linebuf, int bar_w, int bar_h)
{
    // Pre-rendered "AI-PASSPORT-DOOM" as a 95x7 bitmap (1 bit per pixel)
    // Each row is 12 bytes (95 bits rounded up to 96 = 12 bytes)
    // Red color: RGB565 0xF800, byte-swapped for SPI
    const unsigned short red = bswap16(0xF800);
    
    // Clear to black
    memset(linebuf, 0, bar_w * bar_h * sizeof(unsigned short));
    
    // Text dimensions and position
    const int text_w = 95;   // 16 chars * 5px + 15 gaps
    const int text_h = 7;
    const int x0 = (bar_w - text_w) / 2;   // 72
    const int y0 = (bar_h - text_h) / 2;   // 4
    
    // Simple hardcoded character bitmaps for A,I,P,S,O,R,T,D,M,-
    // Each char is 5 wide x 7 tall, stored as 7 rows of 5-bit values
    // Using direct pixel setting to avoid any font lookup issues
    
    // Helper: set a pixel in linebuf
    #define SET_PIX(x,y) linebuf[(y) * bar_w + (x)] = red
    
    // Draw "A" at x0+0
    {
        int bx = x0 + 0;
        // Row 0: _XXXX_ -> cols 1,2,3,4
        SET_PIX(bx+1,y0+0); SET_PIX(bx+2,y0+0); SET_PIX(bx+3,y0+0); SET_PIX(bx+4,y0+0);
        // Row 1: X___X
        SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
        // Row 2: X___X
        SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
        // Row 3: XXXXX
        SET_PIX(bx+0,y0+3); SET_PIX(bx+1,y0+3); SET_PIX(bx+2,y0+3); SET_PIX(bx+3,y0+3); SET_PIX(bx+4,y0+3);
        // Row 4: X___X
        SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
        // Row 5: X___X
        SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
        // Row 6: X___X
        SET_PIX(bx+0,y0+6); SET_PIX(bx+4,y0+6);
    }
    
    // Draw "I" at x0+6
    {
        int bx = x0 + 6;
        // Row 0-6: XXX
        for (int r = 0; r < 7; r++) {
            SET_PIX(bx+1,y0+r); SET_PIX(bx+2,y0+r); SET_PIX(bx+3,y0+r);
        }
    }
    
    // Draw "-" at x0+12
    {
        int bx = x0 + 12;
        // Row 3: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
    }
    
    // Draw "P" at x0+18
    {
        int bx = x0 + 18;
        // Row 0: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
        // Row 1: X___X
        SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
        // Row 2: X___X
        SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
        // Row 3: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
        // Row 4: X____
        SET_PIX(bx+0,y0+4);
        // Row 5: X____
        SET_PIX(bx+0,y0+5);
        // Row 6: X____
        SET_PIX(bx+0,y0+6);
    }
    
    // Draw "A" at x0+24
    {
        int bx = x0 + 24;
        SET_PIX(bx+1,y0+0); SET_PIX(bx+2,y0+0); SET_PIX(bx+3,y0+0); SET_PIX(bx+4,y0+0);
        SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
        SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
        SET_PIX(bx+0,y0+3); SET_PIX(bx+1,y0+3); SET_PIX(bx+2,y0+3); SET_PIX(bx+3,y0+3); SET_PIX(bx+4,y0+3);
        SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
        SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
        SET_PIX(bx+0,y0+6); SET_PIX(bx+4,y0+6);
    }
    
    // Draw "S" at x0+30
    {
        int bx = x0 + 30;
        // Row 0: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
        // Row 1: X____
        SET_PIX(bx+0,y0+1);
        // Row 2: X____
        SET_PIX(bx+0,y0+2);
        // Row 3: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
        // Row 4: ____X
        SET_PIX(bx+4,y0+4);
        // Row 5: ____X
        SET_PIX(bx+4,y0+5);
        // Row 6: XXXXX
        for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+6);
    }
    
    // Draw "S" at x0+36
    { int bx = x0 + 36;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+0,y0+2);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
      SET_PIX(bx+4,y0+4); SET_PIX(bx+4,y0+5);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+6); }
    
    // Draw "P" at x0+42
    { int bx = x0 + 42;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+0,y0+5); SET_PIX(bx+0,y0+6); }
    
    // Draw "O" at x0+48
    { int bx = x0 + 48;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      SET_PIX(bx+0,y0+3); SET_PIX(bx+4,y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+6); }
    
    // Draw "R" at x0+54
    { int bx = x0 + 54;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+2,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+3,y0+5);
      SET_PIX(bx+0,y0+6); SET_PIX(bx+4,y0+6); }
    
    // Draw "T" at x0+60
    { int bx = x0 + 60;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      for (int r = 1; r < 7; r++) SET_PIX(bx+2, y0+r); }
    
    // Draw "-" at x0+66
    { int bx = x0 + 66;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+3); }
    
    // Draw "D" at x0+72
    { int bx = x0 + 72;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      SET_PIX(bx+0,y0+3); SET_PIX(bx+4,y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
      for (int c = 0; c < 4; c++) SET_PIX(bx+c, y0+6); }
    
    // Draw "O" at x0+78
    { int bx = x0 + 78;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      SET_PIX(bx+0,y0+3); SET_PIX(bx+4,y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+6); }
    
    // Draw "O" at x0+84
    { int bx = x0 + 84;
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+4,y0+2);
      SET_PIX(bx+0,y0+3); SET_PIX(bx+4,y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
      for (int c = 0; c < 5; c++) SET_PIX(bx+c, y0+6); }
    
    // Draw "M" at x0+90
    { int bx = x0 + 90;
      SET_PIX(bx+0,y0+0); SET_PIX(bx+4,y0+0);
      SET_PIX(bx+0,y0+1); SET_PIX(bx+1,y0+1); SET_PIX(bx+3,y0+1); SET_PIX(bx+4,y0+1);
      SET_PIX(bx+0,y0+2); SET_PIX(bx+2,y0+2); SET_PIX(bx+4,y0+2);
      SET_PIX(bx+0,y0+3); SET_PIX(bx+4,y0+3);
      SET_PIX(bx+0,y0+4); SET_PIX(bx+4,y0+4);
      SET_PIX(bx+0,y0+5); SET_PIX(bx+4,y0+5);
      SET_PIX(bx+0,y0+6); SET_PIX(bx+4,y0+6); }
    
    #undef SET_PIX
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

    // Convert and blit in strips of STRIP_H game rows, scaled VSCALE x vertically.
    // Render all FB_H (160) game rows to fill the full 320 display rows,
    // then overlay a black bar at the bottom to mask the panel's duplicated band.
    for (int y = 0; y < FB_H; y += STRIP_H) {
        int sh = (y + STRIP_H > FB_H) ? FB_H - y : STRIP_H;
        const byte *row_src = src + y * FB_W;

        // Expand each game row into VSCALE identical display rows.
        for (int r = 0; r < sh; r++) {
            const byte *sr = row_src + r * FB_W;
            uint16_t *dst = s_linebuf + (r * VSCALE) * FB_W;
            for (int x = 0; x < FB_W; x++) {
                uint16_t c = s_palette[sr[x]];
                for (int v = 0; v < VSCALE; v++)
                    dst[v * FB_W + x] = c;
            }
        }

        bsp_display_draw_bitmap(
            DISP_X_OFF, y * VSCALE,
            FB_W, sh * VSCALE,
            s_linebuf
        );
    }

    // Black bar with "AI-PASSPORT-DOOM" text overlay
    // Bottom DISP_BOTTOM_BLACK display rows: black background + red text
    draw_text_on_blackbar(s_linebuf, FB_W, DISP_BOTTOM_BLACK);
    bsp_display_draw_bitmap(
        DISP_X_OFF, BSP_LCD_H - DISP_BOTTOM_BLACK,
        FB_W, DISP_BOTTOM_BLACK,
        s_linebuf
    );
}

/*
 * I_ProcessKeyEvents - Poll ADC buttons and post Doom events.
 *
 * Simple 3-button mapping, no modes, no long press:
 *   UP   -> KEYD_UP           (forward)
 *   DOWN -> KEYD_RIGHT + KEYD_A  (turn right + use/confirm/menu enter)
 *   OK   -> KEYD_B            (fire)
 *
 * KEYD_A is shared between DOWN (use/confirm) and menu navigation.
 * On the title screen or in menus, pressing DOWN confirms selection.
 * In gameplay, DOWN turns right and also triggers BT_USE near doors.
 */
void I_ProcessKeyEvents(void)
{
    bsp_btn_t btn;
    int cur = bsp_button_read(&btn);

    // Track which extra doom keys are held
    static int down_a_held = 0;  // KEYD_A from DOWN button

    if (cur != s_last_btn) {
        // --- Button changed: release old, press new ---

        // Release keys from previous button
        if (s_last_btn == BSP_BTN_UP) {
            event_t ev = { .type = ev_keyup, .data1 = KEYD_UP };
            D_PostEvent(&ev);
        } else if (s_last_btn == BSP_BTN_DOWN) {
            event_t ev_r = { .type = ev_keyup, .data1 = KEYD_RIGHT };
            D_PostEvent(&ev_r);
            if (down_a_held) {
                event_t ev_a = { .type = ev_keyup, .data1 = KEYD_A };
                D_PostEvent(&ev_a);
                down_a_held = 0;
            }
        } else if (s_last_btn == BSP_BTN_OK) {
            event_t ev = { .type = ev_keyup, .data1 = KEYD_B };
            D_PostEvent(&ev);
        }

        // Press new button
        if (cur == BSP_BTN_UP) {
            event_t ev = { .type = ev_keydown, .data1 = KEYD_UP };
            D_PostEvent(&ev);
        } else if (cur == BSP_BTN_DOWN) {
            // Turn right + use/confirm
            event_t ev_r = { .type = ev_keydown, .data1 = KEYD_RIGHT };
            event_t ev_a = { .type = ev_keydown, .data1 = KEYD_A };
            D_PostEvent(&ev_r);
            D_PostEvent(&ev_a);
            down_a_held = 1;
        } else if (cur == BSP_BTN_OK) {
            // Fire only
            event_t ev = { .type = ev_keydown, .data1 = KEYD_B };
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

    // Draw red error screen (strip-based, VSCALE vertical, fullscreen)
    if (s_linebuf) {
        for (int i = 0; i < FB_W * STRIP_H * VSCALE; i++) {
            s_linebuf[i] = bswap16(0xF800);
        }
        for (int y = 0; y < FB_H; y += STRIP_H) {
            int sh = (y + STRIP_H > FB_H) ? FB_H - y : STRIP_H;
            bsp_display_draw_bitmap(
                DISP_X_OFF, y * VSCALE,
                FB_W, sh * VSCALE,
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

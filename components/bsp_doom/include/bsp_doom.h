// bsp_doom.h - Minimal BSP for Doom on AI-Passport (display + buttons only)
#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include <stdbool.h>
#include <stdint.h>

// --- Display ---
esp_err_t bsp_display_init(void);
esp_lcd_panel_handle_t bsp_display_panel(void);
void bsp_display_backlight(uint8_t percent);

// Draw a framebuffer region to screen.
// data: RGB565 pixels, x/y/w/h: region on screen.
esp_err_t bsp_display_draw_bitmap(int x, int y, int w, int h, const uint16_t *data);

// --- Buttons ---
typedef enum { BSP_BTN_UP = 0, BSP_BTN_DOWN = 1, BSP_BTN_OK = 2 } bsp_btn_t;

// Returns which button is currently pressed (-1 = none).
// Also returns raw ADC mV if mv_out is not NULL.
int bsp_button_read(bsp_btn_t *btn_out);

esp_err_t bsp_button_init(void);

// main.c - AI-Passport Doom: app entry point
// Spawns the GBADoom engine as a FreeRTOS task.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "bsp_doom.h"
#include "i_system_e32.h"

// GBADoom engine entry points (from i_main.c / d_main.c)
extern void I_PreInitGraphics(void);
extern void I_Init(void);
extern void Z_Init(void);
extern void InitGlobals(void);
extern void D_DoomMain(void);

// WAD partition init (from esp32_wad.c)
extern int doom_wad_init(void);

static const char *TAG = "doom_main";

// The GBADoom engine runs in this task (never returns).
static void doom_task(void *arg)
{
    ESP_LOGI(TAG, "Doom engine starting...");

    I_PreInitGraphics();   // -> I_InitScreen_e32()
    I_Init();              // sound init (no-op on ESP32, GBA-only code)
    Z_Init();              // zone memory allocator (~256KB malloc)
    InitGlobals();         // zero-init the global state struct
    D_DoomMain();          // game main loop (never returns)
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== AI-Passport Doom ===");

    // Init hardware
    esp_err_t e;

    e = bsp_display_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(e));
        return;
    }

    e = bsp_button_init();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(e));
        return;
    }

    // Load WAD from flash partition
    if (doom_wad_init() != 0) {
        ESP_LOGE(TAG, "WAD load failed!");
        return;
    }

    // Launch Doom engine on core 0 with 32KB stack
    xTaskCreatePinnedToCore(
        doom_task,
        "doom",
        32768,    // stack size
        NULL,
        5,        // priority (higher than idle)
        NULL,
        0         // core 0 (ESP32-C3 only has one core)
    );

    // app_main returns; Doom runs in its own task forever
}

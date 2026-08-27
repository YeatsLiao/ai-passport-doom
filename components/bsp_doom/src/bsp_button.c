// bsp_button.c - ADC button reading for Doom (simplified, no iot_button dependency)
#include "bsp_doom.h"
#include "bsp_pins.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "bsp_btn";

static const uint16_t BTN_MV[BSP_BTN_COUNT][2] = BSP_BTN_MV_TABLE;

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t         s_cali;

esp_err_t bsp_button_init(void) {
    const adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = BSP_BTN_ADC_UNIT };
    esp_err_t e = adc_oneshot_new_unit(&ucfg, &s_adc);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ADC unit init failed: %s", esp_err_to_name(e));
        return e;
    }

    const adc_oneshot_chan_cfg_t ccfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    e = adc_oneshot_config_channel(s_adc, BSP_BTN_ADC_CHANNEL, &ccfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ADC channel config failed: %s", esp_err_to_name(e));
        return e;
    }

    const adc_cali_curve_fitting_config_t cal = {
        .unit_id = BSP_BTN_ADC_UNIT, .chan = BSP_BTN_ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    adc_cali_create_scheme_curve_fitting(&cal, &s_cali);

    ESP_LOGI(TAG, "Buttons ready: ADC1_CH%d 3-key ladder", BSP_BTN_ADC_CHANNEL);
    return ESP_OK;
}

// Read which button is pressed. Returns 0=UP, 1=DOWN, 2=OK, -1=none.
int bsp_button_read(bsp_btn_t *btn_out) {
    if (!s_adc || !s_cali) return -1;

    int raw = 0, mv = 0;
    if (adc_oneshot_read(s_adc, BSP_BTN_ADC_CHANNEL, &raw) != ESP_OK) return -1;
    if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) return -1;

    for (int i = 0; i < BSP_BTN_COUNT; i++) {
        if (mv >= BTN_MV[i][0] && mv < BTN_MV[i][1]) {
            if (btn_out) *btn_out = (bsp_btn_t)i;
            return i;
        }
    }
    return -1; // no button pressed (mv ~ 3300)
}

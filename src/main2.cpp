#include <cmath>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ZMPT_RMS";

static constexpr gpio_num_t    kRelayPin    = GPIO_NUM_14;
static constexpr adc_unit_t    kAdcUnit     = ADC_UNIT_2;
static constexpr adc_channel_t kZmptChannel = ADC_CHANNEL_4;  // GPIO15

static constexpr int   kOffsetSamples = 1000;
static constexpr int   kRmsSamples   = 500;
static constexpr float kVcc          = 3.3f;
static constexpr float kScaleFactor  = 1.0f;  // calibrate with multimeter

extern "C" void app_main() {
    // ── Relay pin ────────────────────────────────────────────────────────────
    gpio_config_t relay_cfg = {};
    relay_cfg.pin_bit_mask  = (1ULL << kRelayPin);
    relay_cfg.mode          = GPIO_MODE_OUTPUT;
    relay_cfg.pull_up_en    = GPIO_PULLUP_DISABLE;
    relay_cfg.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    relay_cfg.intr_type     = GPIO_INTR_DISABLE;
    gpio_config(&relay_cfg);
    gpio_set_level(kRelayPin, 1);  // HIGH → relay ON

    static constexpr int64_t kRelayIntervalUs = 20LL * 1000000LL;  // 20 seconds
    bool     isRelayOn      = true;
    int64_t  lastRelayToggle = esp_timer_get_time();

    // ── ADC oneshot unit ─────────────────────────────────────────────────────
    adc_oneshot_unit_handle_t adc_handle;
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = kAdcUnit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, kZmptChannel, &chan_cfg));

    // ── DC offset calibration (average of 1000 idle samples) ─────────────────
    long suma = 0;
    for (int i = 0; i < kOffsetSamples; i++) {
        int raw = 0;
        adc_oneshot_read(adc_handle, kZmptChannel, &raw);
        suma += raw;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    const float offsetADC = static_cast<float>(suma) / static_cast<float>(kOffsetSamples);

    ESP_LOGI(TAG, "Offset ADC: %.2f", offsetADC);
    ESP_LOGI(TAG, "Iniciando medicion RMS...");

    // ── Main loop ─────────────────────────────────────────────────────────────
    while (true) {
        double sumaCuadrados = 0.0;
        for (int i = 0; i < kRmsSamples; i++) {
            int raw = 0;
            adc_oneshot_read(adc_handle, kZmptChannel, &raw);
            const float valor = static_cast<float>(raw) - offsetADC;
            sumaCuadrados += static_cast<double>(valor) * static_cast<double>(valor);
            vTaskDelay(pdMS_TO_TICKS(2));  // ~500 Hz sample rate
        }

        const float rmsADC      = std::sqrt(static_cast<float>(sumaCuadrados / kRmsSamples));
        const float voltSensor  = (rmsADC / 4095.0f) * kVcc;
        const float voltReal    = voltSensor * kScaleFactor;

        ESP_LOGI(TAG, "RMS ADC: %.2f  Voltaje Sensor: %.3f V  Voltaje Real: %.2f V",
                 rmsADC, voltSensor, voltReal);

        if (esp_timer_get_time() - lastRelayToggle >= kRelayIntervalUs) {
            isRelayOn = !isRelayOn;
            gpio_set_level(kRelayPin, isRelayOn ? 1 : 0);
            ESP_LOGI(TAG, "Relay -> %s", isRelayOn ? "ON" : "OFF");
            lastRelayToggle = esp_timer_get_time();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

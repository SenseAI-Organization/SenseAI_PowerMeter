#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PowerMeter";

constexpr gpio_num_t kVoltagePin = GPIO_NUM_15;
constexpr gpio_num_t kCurrentPin = GPIO_NUM_16;
constexpr gpio_num_t kRelayPin   = GPIO_NUM_14;

extern "C" void app_main() {
    // Configure relay pin as output
    gpio_config_t relay_cfg = {};
    relay_cfg.pin_bit_mask = (1ULL << kRelayPin);
    relay_cfg.mode         = GPIO_MODE_OUTPUT;
    relay_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    relay_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    relay_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&relay_cfg);

    // Configure ADC width and attenuation for voltage (GPIO15 → ADC2_CH4)
    adc2_config_channel_atten(ADC2_CHANNEL_4, ADC_ATTEN_DB_12);
    // Configure ADC width and attenuation for current (GPIO16 → ADC2_CH5)
    adc2_config_channel_atten(ADC2_CHANNEL_5, ADC_ATTEN_DB_12);

    gpio_set_level(kRelayPin, 0);  // Start with relay OFF

    uint32_t relay_state = 0;
    TickType_t last_toggle = xTaskGetTickCount();

    while (true) {
        int voltage = 0;
        int current = 0;

        adc2_get_raw(ADC2_CHANNEL_4, ADC_WIDTH_BIT_12, &voltage);
        adc2_get_raw(ADC2_CHANNEL_5, ADC_WIDTH_BIT_12, &current);

        ESP_LOGI(TAG, "Voltage raw: %d | Current raw: %d", voltage, current);

        if ((xTaskGetTickCount() - last_toggle) >= pdMS_TO_TICKS(30000)) {
            relay_state ^= 1;
            gpio_set_level(kRelayPin, relay_state);
            ESP_LOGI(TAG, "Relay: %s", relay_state ? "ON" : "OFF");
            last_toggle = xTaskGetTickCount();
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

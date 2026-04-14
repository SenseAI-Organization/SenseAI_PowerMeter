#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "voltage_divider_sense.hpp"

static const char *TAG = "PowerMeter";

constexpr gpio_num_t kVoltagePin  = GPIO_NUM_15;
constexpr gpio_num_t kCurrentPin  = GPIO_NUM_16;
constexpr gpio_num_t kVoltage2Pin = GPIO_NUM_13;
constexpr gpio_num_t kCurrent2Pin = GPIO_NUM_12;
constexpr gpio_num_t kRelayPin    = GPIO_NUM_14;

extern "C" void app_main() {

    // Configure relay pin as output (no output class in sensors library)
    gpio_config_t relay_cfg = {};
    relay_cfg.pin_bit_mask = (1ULL << kRelayPin);
    relay_cfg.mode         = GPIO_MODE_OUTPUT;
    relay_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    relay_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    relay_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&relay_cfg);
    gpio_set_level(kRelayPin, 0);  // Start with relay OFF

    // Analog sensors using VoltageDivider (concrete subclass of AnalogSensor)
    VoltageDivider voltage1(kVoltagePin,  ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider current1(kCurrentPin,  ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider voltage2(kVoltage2Pin, ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider current2(kCurrent2Pin, ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);

    ESP_ERROR_CHECK(voltage1.init());
    ESP_ERROR_CHECK(current1.init());
    ESP_ERROR_CHECK(voltage2.init());
    ESP_ERROR_CHECK(current2.init());

    constexpr uint32_t kLoopMs        = 500;
    constexpr uint32_t kRelayPulseMs  = 500;
    constexpr uint32_t kLoopsPerMin   = 60000 / kLoopMs;  // 120 iterations = 1 minute
    uint32_t loop_count = 0;

    while (true) {
        voltage1.measure();
        current1.measure();
        voltage2.measure();
        current2.measure();

        printf("V1 raw: %d (%d mV), I1 raw: %d (%d mV) | "
                 "V2 raw: %d (%d mV), I2 raw: %d (%d mV)\n",
                 voltage1.getValue(), voltage1.getCalibratedMv(),
                 current1.getValue(), current1.getCalibratedMv(),
                 voltage2.getValue(), voltage2.getCalibratedMv(),
                 current2.getValue(), current2.getCalibratedMv());

        loop_count++;
        if (loop_count % kLoopsPerMin == 0) {
            ESP_LOGI(TAG, "Activating relay");
            gpio_set_level(kRelayPin, 1);
            vTaskDelay(pdMS_TO_TICKS(kRelayPulseMs));
            gpio_set_level(kRelayPin, 0);
        } else {
            vTaskDelay(pdMS_TO_TICKS(kLoopMs));
        }
    }
}

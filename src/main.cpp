#include <cmath>

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

bool isRelayOn = false;

/**
 * @brief Measures AC RMS for a sensor by taking N samples, removing the DC
 *        offset (midpoint bias), and computing sqrt(mean(x^2)).
 *
 * @param sensor     VoltageDivider sensor, already init()'d.
 * @param samples    Number of ADC samples (500 covers 5 full 50 Hz cycles at 2 ms/sample).
 * @param delay_ms   Delay between samples in ms (2 ms → ~500 Hz sample rate).
 * @return           RMS value in mV (calibrated), or raw-scaled mV if calibration unavailable.
 */
static float measureRms(VoltageDivider &sensor, int samples = 500, int delay_ms = 2) {
    // Pass 1: compute DC offset (mean)
    double sum = 0.0;
    for (int i = 0; i < samples; i++) {
        sensor.measure();
        int mv = sensor.getCalibratedMv();
        // Fall back to raw-scaled value if calibration is not available
        if (mv == kAdcValueError) {
            mv = (sensor.getValue() * 3300) / 4095;
        }
        sum += mv;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    const float offset = static_cast<float>(sum / samples);

    // Pass 2: compute RMS around the offset
    double sumSq = 0.0;
    for (int i = 0; i < samples; i++) {
        sensor.measure();
        int mv = sensor.getCalibratedMv();
        if (mv == kAdcValueError) {
            mv = (sensor.getValue() * 3300) / 4095;
        }
        const float centered = mv - offset;
        sumSq += centered * centered;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
    return std::sqrt(static_cast<float>(sumSq / samples));
}

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

    constexpr int64_t kRelayIntervalUs = 20LL * 1000000LL;  // 20 seconds
    int64_t lastRelayToggle = esp_timer_get_time();

    while (true) {
        const float v1Rms = measureRms(voltage1);
        const float i1Rms = measureRms(current1);
        const float v2Rms = measureRms(voltage2);
        const float i2Rms = measureRms(current2);

        printf("V1 RMS: %.2f mV, I1 RMS: %.2f mV \n",
               v1Rms, i1Rms);

        if (esp_timer_get_time() - lastRelayToggle >= kRelayIntervalUs) {
            ESP_LOGI(TAG, "Toggling relay");
            gpio_set_level(kRelayPin, isRelayOn ? 0 : 1);
            isRelayOn = !isRelayOn;
            lastRelayToggle = esp_timer_get_time();
        }
    }
}

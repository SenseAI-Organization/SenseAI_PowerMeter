#include "ble_sense.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "voltage_divider_sense.hpp"

static const char *TAG = "PowerMeter";
static BLEServer *g_server = nullptr;

constexpr gpio_num_t kVoltagePin  = GPIO_NUM_15;
constexpr gpio_num_t kCurrentPin  = GPIO_NUM_16;
constexpr gpio_num_t kVoltage2Pin = GPIO_NUM_3;
constexpr gpio_num_t kCurrent2Pin = GPIO_NUM_6;
constexpr gpio_num_t kRelayPin    = GPIO_NUM_14;

extern "C" void app_main() {
    // NVS init (required by BLE)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

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
    // R1=0, R2=1 → pass-through; use getValue() for raw ADC, getCalibratedMv() for mV
    VoltageDivider voltage1(kVoltagePin,  ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider current1(kCurrentPin,  ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider voltage2(kVoltage2Pin, ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);
    VoltageDivider current2(kCurrent2Pin, ADC_ATTEN_DB_12, ADC_BITWIDTH_12, 0, 1);

    ESP_ERROR_CHECK(voltage1.init());
    ESP_ERROR_CHECK(current1.init());
    ESP_ERROR_CHECK(voltage2.init());
    ESP_ERROR_CHECK(current2.init());

    // BLE configuration
    ble_config_t config;
    ret = BLELibrary::createDefaultConfig(&config, kBleServerOnly);
    ESP_ERROR_CHECK(ret);
    strncpy(config.deviceName, "PowerMeter", BLE_MAX_DEVICE_NAME_LEN - 1);
    config.autoStart      = false;
    config.enableLogging  = true;
    config.logLevel       = ESP_LOG_INFO;

    BLELibrary *ble = new BLELibrary(config);
    ESP_ERROR_CHECK(ble->init());

    BLEServer *server = ble->getServer();
    g_server = server;
    if (server == nullptr) {
        ESP_LOGE(TAG, "BLE server not available");
        delete ble;
        return;
    }

    bleServerConfig_t serverConfig = server->getConfig();
    serverConfig.maxClients             = 4;
    serverConfig.enableNotifications    = true;
    serverConfig.autoStartAdvertising   = false;
    serverConfig.enableJsonCommands     = false;
    server->setConfig(serverConfig);

    // Relay control via BLE: 0x00 = OFF, 0x01 = ON
    server->setDataWrittenCallback(
        [](uint16_t connID, const uint8_t *data, uint16_t length) {
            if (length < 1) return;
            if (data[0] == 0x01) {
                gpio_set_level(kRelayPin, 1);
                ESP_LOGI(TAG, "BLE → Relay ON");
            } else if (data[0] == 0x00) {
                gpio_set_level(kRelayPin, 0);
                ESP_LOGI(TAG, "BLE → Relay OFF");
            } else {
                ESP_LOGW(TAG, "BLE → Unknown command: 0x%02X", data[0]);
            }
        });

    // Always restart advertising after a client disconnects
    server->setClientDisconnectedCallback([](uint16_t connID, int reason) {
        ESP_LOGI(TAG, "Client %d disconnected (reason %d), restarting advertising...", connID, reason);
        if (g_server) g_server->startAdvertising();
    });

    server->setClientConnectedCallback(
        [](uint16_t connID, const ble_device_info_t *clientInfo) {
            ESP_LOGI(TAG, "Client connected - ID: %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                     connID,
                     clientInfo->address[0], clientInfo->address[1],
                     clientInfo->address[2], clientInfo->address[3],
                     clientInfo->address[4], clientInfo->address[5]);
        });

    ESP_ERROR_CHECK(server->startAdvertising());
    ESP_LOGI(TAG, "BLE advertising started as 'PowerMeter'");

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

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

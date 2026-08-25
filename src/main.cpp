#include "esp_mac.h"
#include "espnow_sense.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "actuators_sense.hpp"
#include "power_meter_watchdog.hpp"
#include "esp_timer.h"

#define TAG "ESP_NOW_SERVER_TEST"

// 48:ca:43:15:ff:2c
// uint8_t serverMac[6] = {0x48, 0xCA, 0x43, 0x15, 0xFF, 0x4C};
uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr gpio_num_t kRelayPin = GPIO_NUM_14;

constexpr gpio_num_t kRGBPin = GPIO_NUM_2;

static bool kRelayForced = false;  // true = relay locked ON by ESP-NOW command
static int64_t lastMessageTime = 0;  // timestamp of last ON/OFF message
constexpr int64_t kMessageTimeOutUs = 2 * 60 * 1000000LL;  // 2 minutes in microseconds

extern "C" void app_main() {

    // Initialize watchdog system: 60 seconds timeout, restart every 2 mites
    power_meter_watchdog::init(60, 20);
    ESP_LOGI(TAG, "Watchdog system initialized");

    // Configure relay pin as output
    gpio_config_t relay_cfg = {};
    relay_cfg.pin_bit_mask = (1ULL << kRelayPin);
    relay_cfg.mode         = GPIO_MODE_OUTPUT;
    relay_cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    relay_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    relay_cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&relay_cfg);
    gpio_set_level(kRelayPin, 0);  // Start with relay OFF
    
    // Create ESP-NOW object as server, using channel 1
    EspNow espServer(6, 1, true);

    // Get and print MAC
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG, "Device MAC: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);

    espServer.initialize();
    espServer.addPeer(broadcastMac);

    while (true) {
        // Feed the watchdog to prove we're alive
        power_meter_watchdog::feed();

        // Check for new messages
        if (espServer.hasNewMessage()) {
            // Get the message
            std::string receivedData;
            espServer.receiveData(receivedData);
            ESP_LOGI(TAG, "Received data: %s", receivedData.c_str());
            
            if (receivedData == "ON") {
                gpio_set_level(kRelayPin, 1);  // Turn relay ON
                kRelayForced = true;
                lastMessageTime = esp_timer_get_time();  // Record timestamp
                ESP_LOGI(TAG, "ESP-NOW: Relay ON");
                gpio_set_level(kRelayPin, 1);  // Turn relay ON
                kRelayForced = true;
                espServer.sendBroadcast("ACK:ON");
            } else if (receivedData == "ALERT:ON") {
                ESP_LOGI(TAG, "Received ALERT:ON command.");
                gpio_set_level(kRelayPin, 1);
                kRelayForced = true;
                lastMessageTime = esp_timer_get_time();  // Record timestamp
                espServer.sendBroadcast("ACK:ALERT:ON");
            }
            else if (receivedData == "OFF") {
                gpio_set_level(kRelayPin, 0);  // Turn relay OFF
                kRelayForced = false;
                lastMessageTime = esp_timer_get_time();  // Record timestamp
                ESP_LOGI(TAG, "ESP-NOW: Relay OFF");
                espServer.sendBroadcast("ACK:OFF");
            }
            else if (receivedData == "ALERT:OFF") {
                ESP_LOGI(TAG, "Received ALERT:OFF command.");
                gpio_set_level(kRelayPin, 0);
                kRelayForced = false;
                lastMessageTime = esp_timer_get_time();  // Record timestamp
                espServer.sendBroadcast("ACK");
            }
            else {
                ESP_LOGW(TAG, "Unknown command: %s", receivedData.c_str());
            }
        }

        // Check for message timeout - turn off relay if no message for 2 minutes
        if (lastMessageTime > 0) {  // Only check if we've received at least one message
            int64_t currentTime = esp_timer_get_time();
            int64_t timeSinceLastMessage = currentTime - lastMessageTime;
            
            if (timeSinceLastMessage > kMessageTimeOutUs) {
                esp_restart();  // Restart the device to reset state
            }
        }

        // Short delay to prevent CPU hogging
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

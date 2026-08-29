/*******************************************************************************
 * @file main.cpp
 * @brief Actuador de válvula controlado por ESP-NOW, modelado como máquina de
 *        estados (ver diagrama de flujo del proyecto).
 *
 * Estados (cajas del diagrama):
 *   kIdle     -> "Idle": válvula cerrada, escuchando ESP-NOW.
 *   kStarting -> "Start Valve + Send ACK": acción única, pasa a kActive.
 *   kActive   -> "Active State": válvula abierta, escuchando + fail-safe.
 *   kStopping -> "Stop valve + send ACK": acción única, pasa a kIdle.
 *
 * Cada transición aplica los pines y el ACK UNA sola vez; el resto del tiempo
 * el lazo solo espera un mensaje nuevo (o el vencimiento del fail-safe).
 ******************************************************************************/
#include <string>

#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "espnow_sense.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "power_meter_watchdog.hpp"

#define TAG "POWER_METER_VALVE"

namespace {

// GPIO del relé que abre/cierra la válvula.
constexpr gpio_num_t kRelayPin = GPIO_NUM_14;

// MAC de broadcast: el emisor (BeeSense) manda por broadcast y espera el ACK.
uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Fail-safe: si la válvula lleva este tiempo abierta sin recibir NINGÚN comando
// válido, se cierra sola. El emisor está en deep sleep y reenvía ALERT:ON cada
// ~10 min mientras dure la alerta; 20 min cubre ~2 ciclos perdidos antes de
// asumir que el emisor quedó fuera de alcance o caído. Subir aquí si cambia el
// intervalo de despertar del emisor.
constexpr int64_t kActiveTimeoutUs = 20LL * 60 * 1000000;  // 20 min en us

// Cajas del diagrama de flujo. kStarting/kStopping son transitorios: ejecutan su
// acción y saltan de inmediato al estado estable siguiente.
enum class ValveState { kIdle, kStarting, kActive, kStopping };

// Intención del comando, sin distinguir la forma "pelada" de la forma "ALERT:".
enum class Command { kOn, kOff, kUnknown };

Command parseCommand(const std::string& msg) {
    if (msg == "ON" || msg == "ALERT:ON") return Command::kOn;
    if (msg == "OFF" || msg == "ALERT:OFF") return Command::kOff;
    return Command::kUnknown;
}

void configureRelayPin() {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << kRelayPin);
    cfg.mode         = GPIO_MODE_OUTPUT;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(kRelayPin, 0);  // arranca con la válvula cerrada
}

}  // namespace

extern "C" void app_main() {
    // Watchdog de cuelgue 60 s + reinicio de mantenimiento cada 24 h. El apagado
    // por inactividad de la válvula NO lo hace el watchdog, lo hace la FSM
    // (kActiveTimeoutUs), que se reinicia con cada comando recibido.
    power_meter_watchdog::init(60, 1440);
    ESP_LOGI(TAG, "Watchdog inicializado (60s / 24h)");

    configureRelayPin();

    EspNow espServer(6, 1, true);

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    ESP_LOGI(TAG, "MAC del equipo: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);

    espServer.initialize();
    espServer.addPeer(kBroadcastMac);

    ValveState state     = ValveState::kIdle;
    ValveState prevState  = ValveState::kStopping;  // fuerza la entrada a kIdle
    std::string rxMsg;                              // último comando recibido
    int64_t valveOnSince = 0;                       // marca del último ON confirmado

    while (true) {
        power_meter_watchdog::feed();

        // entering == true solo en el primer ciclo de cada estado: ahí se
        // aplican pines/log una única vez por transición.
        const bool entering = (state != prevState);
        prevState = state;

        switch (state) {
            // ---- Idle: válvula cerrada, esperando un ON ----
            case ValveState::kIdle: {
                if (entering) {
                    gpio_set_level(kRelayPin, 0);
                    ESP_LOGI(TAG, "Estado: IDLE (valvula OFF)");
                }
                if (espServer.hasNewMessage()) {
                    espServer.receiveData(rxMsg);
                    ESP_LOGI(TAG, "RX: %s", rxMsg.c_str());
                    switch (parseCommand(rxMsg)) {
                        case Command::kOn:
                            state = ValveState::kStarting;
                            break;
                        case Command::kOff:
                            // Ya está cerrada; se confirma igual para que el
                            // emisor no reintente.
                            espServer.sendBroadcast("ACK:OFF");
                            break;
                        case Command::kUnknown:
                            ESP_LOGW(TAG, "Comando desconocido: %s", rxMsg.c_str());
                            break;
                    }
                }
                break;
            }

            // ---- Start Valve + Send ACK (transitorio) ----
            case ValveState::kStarting: {
                gpio_set_level(kRelayPin, 1);
                espServer.sendBroadcast("ACK:" + rxMsg);
                valveOnSince = esp_timer_get_time();  // (re)arranca el fail-safe
                ESP_LOGI(TAG, "Valvula ON + ACK (%s)", rxMsg.c_str());
                state = ValveState::kActive;
                break;
            }

            // ---- Active: válvula abierta, escuchando + fail-safe ----
            case ValveState::kActive: {
                if (entering) {
                    ESP_LOGI(TAG, "Estado: ACTIVE (valvula ON)");
                }
                if (espServer.hasNewMessage()) {
                    espServer.receiveData(rxMsg);
                    ESP_LOGI(TAG, "RX: %s", rxMsg.c_str());
                    switch (parseCommand(rxMsg)) {
                        case Command::kOn:
                            // Reconfirmación: se vuelve por kStarting para reusar
                            // el único punto de entrada (pin + ACK + reinicio del
                            // fail-safe).
                            state = ValveState::kStarting;
                            break;
                        case Command::kOff:
                            state = ValveState::kStopping;
                            break;
                        case Command::kUnknown:
                            ESP_LOGW(TAG, "Comando desconocido: %s", rxMsg.c_str());
                            break;
                    }
                } else if (esp_timer_get_time() - valveOnSince > kActiveTimeoutUs) {
                    ESP_LOGW(TAG, "Fail-safe: %lld min sin comandos, cerrando",
                             kActiveTimeoutUs / 60000000LL);
                    rxMsg = "TIMEOUT";
                    state = ValveState::kStopping;
                }
                break;
            }

            // ---- Stop valve + send ACK (transitorio) ----
            case ValveState::kStopping: {
                gpio_set_level(kRelayPin, 0);
                espServer.sendBroadcast("ACK:OFF");
                ESP_LOGI(TAG, "Valvula OFF + ACK (motivo: %s)", rxMsg.c_str());
                state = ValveState::kIdle;
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/*******************************************************************************
 * @file power_meter_watchdog.cpp
 * @brief Watchdog system to prevent system hangs and enforce daily restarts
 ******************************************************************************/
#include "power_meter_watchdog.hpp"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

namespace power_meter_watchdog {
namespace {

constexpr const char* TAG = "PowerMeterWatchdog";

esp_timer_handle_t s_watchdogTimer = nullptr;
esp_timer_handle_t s_dailyRestartTimer = nullptr;
uint32_t s_watchdogTimeoutMs = 0;   // 0 means "not armed"
uint32_t s_dailyRestartMinutes = 0;   // 0 means "disabled"
int64_t s_dailyRestartStartTime = 0;

// Watchdog callback - fires if main loop stops feeding
void onWatchdogExpired(void* /*arg*/) {
    ESP_LOGE(TAG, " Watchdog timeout! No activity for %lu ms - restarting device",
             (unsigned long)s_watchdogTimeoutMs);
    esp_restart();
}

// Daily restart callback - fires once per day for preventive maintenance
void onDailyRestart(void* /*arg*/) {
    ESP_LOGI(TAG, " Daily restart triggered after %lu minutes - restarting device",
             (unsigned long)s_dailyRestartMinutes);
    esp_restart();
}

esp_err_t ensureWatchdogTimer(void) {
    if (s_watchdogTimer != nullptr) return ESP_OK;

    const esp_timer_create_args_t args = {
        .callback = &onWatchdogExpired,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "watchdog_timer",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&args, &s_watchdogTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create watchdog timer: %s", esp_err_to_name(err));
        s_watchdogTimer = nullptr;
    }
    return err;
}

esp_err_t ensureDailyRestartTimer(void) {
    if (s_dailyRestartTimer != nullptr) return ESP_OK;

    const esp_timer_create_args_t args = {
        .callback = &onDailyRestart,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "daily_restart_timer",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&args, &s_dailyRestartTimer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create daily restart timer: %s", esp_err_to_name(err));
        s_dailyRestartTimer = nullptr;
    }
    return err;
}

void restartWatchdog(uint32_t timeoutMs) {
    if (ensureWatchdogTimer() != ESP_OK) return;

    esp_timer_stop(s_watchdogTimer);  // no-op when not running
    esp_err_t err = esp_timer_start_once(s_watchdogTimer, (uint64_t)timeoutMs * 1000ULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Could not arm watchdog: %s", esp_err_to_name(err));
        s_watchdogTimeoutMs = 0;
        return;
    }
    s_watchdogTimeoutMs = timeoutMs;
}

}  // namespace

void init(uint32_t watchdogTimeoutS, uint32_t dailyRestartMinutes) {
    // Start watchdog timer
    restartWatchdog(watchdogTimeoutS * 1000);
    ESP_LOGI(TAG, " Watchdog armed: %lu ms timeout", (unsigned long)(watchdogTimeoutS * 1000));

    // Start daily restart timer if requested
    if (dailyRestartMinutes > 0) {
        if (ensureDailyRestartTimer() != ESP_OK) return;

        uint64_t restartIntervalUs = (uint64_t)dailyRestartMinutes * 60ULL * 1000000ULL;
        esp_err_t err = esp_timer_start_once(s_dailyRestartTimer, restartIntervalUs);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Could not arm daily restart: %s", esp_err_to_name(err));
            s_dailyRestartMinutes = 0;
            return;
        }
        s_dailyRestartMinutes = dailyRestartMinutes;
        s_dailyRestartStartTime = esp_timer_get_time();
        ESP_LOGI(TAG, "✓ Daily restart armed: every %lu minutes", (unsigned long)dailyRestartMinutes);
    }
}

void feed(void) {
    // Restart the watchdog timer to prevent timeout
    if (s_watchdogTimeoutMs == 0) return;
    restartWatchdog(s_watchdogTimeoutMs);
}

void setWatchdogTimeout(uint32_t timeoutMs) {
    restartWatchdog(timeoutMs);
    ESP_LOGI(TAG, "Watchdog timeout changed to %lu ms", (unsigned long)timeoutMs);
}

void disableWatchdog(void) {
    if (s_watchdogTimer == nullptr) return;
    esp_timer_stop(s_watchdogTimer);
    s_watchdogTimeoutMs = 0;
    ESP_LOGW(TAG, " Watchdog disabled");
}

void disableDailyRestart(void) {
    if (s_dailyRestartTimer == nullptr) return;
    esp_timer_stop(s_dailyRestartTimer);
    s_dailyRestartMinutes = 0;
    ESP_LOGW(TAG, " Daily restart disabled");
}

uint32_t getTimeUntilRestart(void) {
    if (s_dailyRestartMinutes == 0 || s_dailyRestartTimer == nullptr) return 0;
    
    int64_t elapsed = esp_timer_get_time() - s_dailyRestartStartTime;
    int64_t total = (int64_t)s_dailyRestartMinutes * 60LL * 1000000LL;
    int64_t remaining = total - elapsed;
    
    if (remaining < 0) return 0;
    return (uint32_t)(remaining / 1000000LL);  // Convert to seconds
}

}  // namespace power_meter_watchdog

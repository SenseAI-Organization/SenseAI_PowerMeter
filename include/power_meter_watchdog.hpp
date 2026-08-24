/*******************************************************************************
 * @file power_meter_watchdog.hpp
 * @brief Watchdog system to prevent system hangs and enforce daily restarts
 * 
 * Features:
 * - Watchdog timer: restarts if main loop stops feeding for too long
 * - Daily restart: automatic clean restart every 24 hours
 * - Feed mechanism: call feed() in main loop to prove system is alive
 ******************************************************************************/
#pragma once

#include <cstdint>

namespace power_meter_watchdog {

/**
 * @brief Initialize and start the watchdog system
 * @param watchdogTimeoutMs Watchdog timeout in milliseconds (default: 30000 = 30s)
 * @param dailyRestartHours Hours between automatic restarts (default: 24)
 */
void init(uint32_t watchdogTimeoutMs = 30000, uint32_t dailyRestartHours = 24);

/**
 * @brief Feed the watchdog - call this periodically in your main loop
 *        to prove the system is alive and running normally
 */
void feed(void);

/**
 * @brief Extend or change the watchdog timeout
 * @param timeoutMs New timeout in milliseconds
 */
void setWatchdogTimeout(uint32_t timeoutMs);

/**
 * @brief Disable the watchdog system (not recommended for production)
 */
void disableWatchdog(void);

/**
 * @brief Disable the daily restart feature
 */
void disableDailyRestart(void);

/**
 * @brief Get time remaining until next daily restart (in seconds)
 * @return Seconds until restart, or 0 if disabled
 */
uint32_t getTimeUntilRestart(void);

}  // namespace power_meter_watchdog

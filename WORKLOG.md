# WORKLOG — SenseAI_PowerMeter

Bitácora de continuidad entre sesiones. Anexar entradas, nunca reescribir.

---

## 2026-08-29 — FSM de válvula por ESP-NOW en `src/main.cpp`

**Hecho:** Reescrito `src/main.cpp` como máquina de estados de 4 estados según el
diagrama de flujo del proyecto, en lugar del `if/else` plano anterior:

- Estados: `kIdle` (válvula OFF, escuchando), `kStarting` (transitorio: relé ON +
  ACK), `kActive` (válvula ON, escuchando + fail-safe), `kStopping` (transitorio:
  relé OFF + ACK). Los pines y el ACK se aplican UNA vez por transición (patrón
  `entering = state != prevState`); el resto del lazo solo espera mensaje nuevo.
- `parseCommand()` mapea `ON`/`ALERT:ON` → encender y `OFF`/`ALERT:OFF` → apagar
  (misma semántica que antes, pero centralizada).
- Nuevo fail-safe: `kActiveTimeoutUs = 20 min`. Si la válvula lleva 20 min
  abierta sin recibir ningún comando válido, pasa a `kStopping` (cierra + ACK +
  vuelve a Idle, **sin reiniciar**). Se reinicia con cada comando recibido.
  Reconfirmación: `ALERT:ON` estando en `kActive` vuelve por `kStarting` para
  re-ACK y reiniciar el contador.
- `power_meter_watchdog::init(60, 20)` → `init(60, 1440)`. El watchdog de cuelgue
  de 60 s se mantiene; el "daily restart" pasa a 24 h. El apagado por inactividad
  ahora lo hace la FSM, no un `esp_restart()`.
- Eliminado el timeout de 2 min → `esp_restart()` del código anterior. Motivo:
  el emisor `Agritech_BeeSense` está en deep sleep y solo manda `ALERT:ON`
  mientras `distance > highThreshold` (cada ~10 min); en la banda de histéresis
  no manda nada. Un timeout de 2 min cerraba la válvula entre cada ciclo. Ver
  `Agritech_BeeSense/src/main.cpp` → `getAlertType()` (~L2990) y
  `sendAlertCommand()` (~L1372).
- Eliminado include `actuators_sense.hpp` y `kRGBPin` (código muerto). TAG
  renombrado `ESP_NOW_SERVER_TEST` → `POWER_METER_VALVE`.

**Evidencia:** `pio run` → `[SUCCESS]`. RAM 10.2%, Flash 63.6%
(`.pio/build/esp32-s3-devkitc-1/firmware.bin`).

**Artefactos:** `.pio/build/esp32-s3-devkitc-1/firmware.{elf,bin}` (solo salida
del compilador; no se archivó copia de distribución en esta sesión).

**Pendiente:**
- Probar en hardware: secuencia `ALERT:ON` → válvula abre + `ACK:ALERT:ON`;
  silencio 20 min → cierre por fail-safe; `ALERT:OFF` → cierre + `ACK:OFF`.
- Si el sensor puede quedarse legítimamente en la banda de histéresis más de
  20 min con la válvula abierta, subir `kActiveTimeoutUs`.

**Sin commitear:** `src/main.cpp` (reescrito), `WORKLOG.md` (nuevo). Rama
`feature/espnowActivation`. *(Commiteado por el usuario después de esta entrada.)*

---

## 2026-08-29 — LED RGB de estado + parpadeo por mensaje

**Hecho:** Reincorporado el LED RGB WS2812 (`RGB` de actuators-library) a la FSM
de `src/main.cpp`:

- Color de estado, seteado junto con el relé: **rojo** (`kLedLevel,0,0`) en
  `kIdle`/`kStopping` (válvula OFF), **verde** (`0,kLedLevel,0`) en `kStarting`
  (válvula ON). `kActive` hereda el verde de `kStarting`.
- `blinkAck(RGB&)`: parpadea `kAckBlinks` (2) veces sin cambiar el color de
  estado (`turnOff()` → `turnOn()` restaura el último `setColor`). Se llama
  **después de cada `sendBroadcast` de ACK**, ante cualquier mensaje ESP-NOW
  recibido (ON/OFF/desconocido). NO parpadea en el cierre por fail-safe
  (`rxMsg == "TIMEOUT"`), que no es un mensaje recibido.
- Constantes nuevas: `kLedPin = GPIO_NUM_2`, `kLedLevel = 32`, `kAckBlinks = 2`,
  `kAckBlinkMs = 80`. Re-agregado `#include "actuators_sense.hpp"`.
- La librería `actuators-library` ya trae el fix: el build resuelve
  `actuators-library @ ...sha.6c378dc` ("hotfix: RGB class and its example timing
  fixed"). Su copia en `.pio/libdeps` es idéntica a
  `C:\Users\escob\Desktop\SenseAI\LibreriasSense\actuators-library\actuators-library`
  (rama `dev`, sin commits sin pushear). No hizo falta tocar `platformio.ini`.

**Evidencia:** `pio run` → `[SUCCESS]`. RAM 10.5%, Flash 65.4%.

**Artefactos:** `.pio/build/esp32-s3-devkitc-1/firmware.{elf,bin}` (salida del
compilador; sin copia de distribución archivada).

**Pendiente:**
- Verificar en hardware el pin del WS2812 (`GPIO_NUM_2`, heredado del código
  viejo) y que rojo/verde se vean bien con `kLedLevel = 32`.
- Confirmar que el parpadeo de ~320 ms tras el ACK no molesta; ajustar
  `kAckBlinks`/`kAckBlinkMs` si hace falta.

**Sin commitear:** `src/main.cpp`, `WORKLOG.md`. Rama `feature/espnowActivation`.

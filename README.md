# Smart Prepaid Energy Meter (Arduino + RFID + LoRa)

A standalone, **prepaid electricity meter** built on an Arduino-class MCU. The meter measures real‑time voltage, current, power and power factor, deducts energy units as they are consumed, lets a user recharge their balance by tapping an RFID card, displays everything on a 16x2 I2C LCD, transmits telemetry over LoRa, and survives a sudden power loss without losing the user's balance ("dying gasp" save).

---

## ✨ Features

- ⚡ Real-time **voltage / current / power / power factor** measurement using EmonLib
- 💳 **RFID-based prepaid recharge** (MIFARE card, single data block used as a token)
- 🔌 **Automatic relay cutoff** when balance reaches zero, automatic reconnect on recharge
- 📟 **16x2 I2C LCD** that cycles between balance, electrical readings, and date/time
- 📡 **LoRa telemetry** (865 MHz, India ISM band) sent every few seconds to a base station/gateway
- 🛡️ **Dying-gasp protection** — a hardware interrupt detects mains loss and saves the live balance to EEPROM before the supply capacitor drains
- 🔋 **Adaptive RFID power-down** — the RFID reader's antenna/RST line is only powered when needed, to save energy
- 🕒 **RTC (DS3231)** for accurate timestamps on the LCD and in LoRa payloads
- 🧵 **Cooperative (non-blocking) task scheduler** — no RTOS, no `delay()` blocking, runs comfortably on a single core with very little RAM

---

## 🧰 Hardware / Bill of Materials

| Component | Purpose |
|---|---|
| Arduino Uno / Nano (ATmega328P) | Main controller |
| ZMPT101B (or similar) voltage sensor | AC voltage sensing |
| SCT-013 / ACS712-style current sensor | AC current sensing |
| 5V Relay module | Load connect/disconnect |
| MFRC522 RFID reader + MIFARE Classic card | Recharge token |
| LiquidCrystal_I2C 16x2 LCD | Local display |
| LoRa module (SX1278, 865 MHz) | Wireless telemetry |
| DS3231 RTC module | Real-time clock |
| 45KΩ / 45KΩ voltage divider | Mains-loss ("dying gasp") detection |
| External AC–DC step-down / DC-DC buck converter (5V) | Powers the whole board from mains (see [Power Supply Design](#-power-supply-design-journey)) |
| Small holdup capacitor | Keeps the 5V rail alive just long enough to finish the EEPROM write after mains loss |

---

## 🔌 Circuit / Pin Connections

| Signal | Arduino Pin | Notes |
|---|---|---|
| Relay control | D4 | `LOW` = load ON, `HIGH` = load OFF |
| Voltage sensor (ZMPT) | A0 | Calibrated via `VOLTAGE_CAL = 417.0` |
| Current sensor (SCT) | A1 | Calibrated via `CURRENT_CAL = 14.0` |
| RFID SDA/SS | D8 | MFRC522 chip select |
| RFID RST | D9 | Also used to hard-sleep the reader |
| RFID MOSI | D11 | Standard Uno SPI |
| RFID SCK | D13 | Standard Uno SPI |
| LoRa NSS | D10 | LoRa chip select |
| LoRa RST | D7 | |
| LoRa DIO0 | D2 | |
| Mains-loss sense | D3 (`POWER_SENSE_PIN`) | Fed from a 45K/45K divider off the 5V rail; triggers `runDyingGaspISR()` on `FALLING` edge |
| LCD SDA/SCL | A4/A5 (I2C) | Address `0x27` |
| RTC SDA/SCL | A4/A5 (shared I2C bus) | DS3231 |

<img width="970" height="680" alt="image" src="https://github.com/user-attachments/assets/9592062c-32f8-405f-bb91-580235710c07" />


## 🗂️ Project Structure

```
.
├── smart_meter.ino       # setup() + cooperative scheduler loop()
├── config.h               # Pin map, calibration constants, timing intervals
├── globals.h               # Shared hardware objects & volatile state (extern)
├── power_measure.cpp        # EmonLib readings, unit deduction, relay/RFID power logic
├── rfid_ops.cpp               # Card detection, authentication, recharge, UID check
├── lcd_display.cpp              # 4-screen LCD UI (balance / V-I / P-PF / date-time)
├── lora_ops.cpp                   # Telemetry packet build + send
└── powerManagement.cpp              # EEPROM save/recover + dying-gasp ISR
```

---

## 🧠 How It Works

1. **Boot**: RTC, RFID, LoRa, and EmonLib are initialized. The last saved balance is recovered from EEPROM. If a valid balance exists, the relay is closed and a `PWRRESTORE` LoRa packet is sent.
2. **Every loop iteration** (no `delay()` anywhere in the hot path), four independent tasks are checked against their own timers and only run when due:
   - **RFID task** (`RFID_INTERVAL_MS = 100ms`) — only polls for a card when `availableUnits <= 0`; otherwise the antenna is kept off to save power.
   - **Power task** (`POWER_INTERVAL_MS = 1000ms`) — samples V/I via EmonLib, computes power & PF, deducts consumed units from the balance, and drives the relay.
   - **LCD task** (`LCD_INTERVAL_MS = 1000ms`) — refreshes the current screen; screens auto-cycle every `LCD_SCREEN_CYCLE_MS = 2500ms`.
   - **LoRa task** (`LORA_INTERVAL_MS = 5000ms`) — packages V/I/P/PF/balance/timestamp and transmits it.
3. **Recharge**: tapping the authorized card reads a 2-byte unit value from a MIFARE block, adds it to the balance, wipes the token block (so it can't be reused), saves to EEPROM, and reconnects the relay.
4. **Power loss**: a hardware interrupt on `POWER_SENSE_PIN` fires `runDyingGaspISR()`, which immediately opens the relay, powers down the RFID antenna, writes the balance to EEPROM (~13.6 ms), and fires a final LoRa alert — all before the holdup capacitor's energy runs out.

---

## 🧩 Design Journey & Problems Solved

### 1. FreeRTOS → Cooperative (non-blocking) scheduling

The first version of this project was built on **FreeRTOS**, using separate tasks for RFID polling, power sampling, LCD refresh, and LoRa transmission. In practice this didn't fit well on an ATmega328P-class board:

- FreeRTOS's task control blocks, stacks (one per task), and kernel overhead consume a large slice of the chip's **2KB of SRAM**, leaving very little headroom for EmonLib's sampling buffers, the LoRa payload buffer, and the LCD/RFID library objects.
- The project doesn't need true preemption or priority scheduling — none of the tasks have hard real-time deadlines against each other, they just need to run "often enough."

**Fix:** the firmware was rewritten around a simple **`millis()`-based cooperative scheduler** in `loop()`. Each task (RFID, power, LCD, LoRa) tracks its own `lastXTime` and only executes once its interval has elapsed — no kernel, no per-task stacks, no blocking `delay()` calls anywhere in the loop. This freed up enough RAM to run reliably and made the timing behavior easy to reason about and debug.

### 2. Capacitor-only power → External DC-DC converter

Initially the plan was to power the *entire* board (Arduino, LCD, RFID, LoRa) from energy stored in a holdup capacitor charged off the mains sense circuit. This turned out to be both **insufficient and unnecessary**:

- A capacitor sized to run the full system (MCU + LCD backlight + RFID + LoRa radio) for any meaningful time would need to be physically large and expensive, and still couldn't guarantee a clean shutdown under load.
- It's also unnecessary — the system doesn't need to *keep running* after a mains failure, it only needs to **finish one EEPROM write and send one LoRa alert** before going dark.

**Fix:** normal operation is now powered by an **external AC-DC/DC-DC step-down converter** that takes the mains/line potential and regulates it down to a clean 5V rail for the whole board. The holdup capacitor's job was scoped down to exactly one thing: keep the 5V rail alive for the few tens of milliseconds the `runDyingGaspISR()` routine needs to cut the relay, write the balance to EEPROM, and transmit the power-loss alert — nothing more. This made the capacitor dramatically smaller and the power supply far more reliable.

> Note: the `CRASH_THRESHOLD` for the dying-gasp comparator was also corrected during this process — an earlier value (630) sat *above* the normal ADC reading (512) on the 45K/45K divider, which caused false triggers on every boot. It's now set to 500, comfortably between the normal 512 reading and the brown-out point.

---

## ✅ Pros

- Runs entirely on a single low-cost AVR MCU — no external RTOS dependency
- Survives power cuts without losing the customer's balance
- Low standby power draw (RFID antenna and reader are duty-cycled based on need)
- Fully field-serviceable: recharge happens with a physical card tap, no network dependency for the relay/balance logic
- Remote visibility via LoRa telemetry even where there's no Wi-Fi/GSM coverage
- Simple, auditable cooperative scheduler — easy to extend with new tasks without RTOS complexity

---

## ⚙️ Calibration

EmonLib calibration constants live in `config.h`:

```cpp
#define VOLTAGE_CAL   417.0
#define VOLTAGE_PHASE -0.6
#define CURRENT_CAL   14.0
```

Adjust these against a known reference meter/load for your specific sensor pair (ZMPT101B + SCT/ACS712) and burden resistor values.

---

## 🚀 Getting Started

1. Install the Arduino IDE and the following libraries via Library Manager:
   - `MFRC522`
   - `LiquidCrystal_I2C`
   - `RTClib`
   - `EmonLib`
   - `LoRa` (Sandeep Mistry)
2. Wire the hardware per the [Circuit / Pin Connections](#-circuit--pin-connections) table.
3. Set `AUTHORIZED_UID` in `rfid_ops.cpp` to match your own RFID card's UID.
4. Flash `smart_meter.ino` to the board.
5. Open Serial Monitor at `9600` baud to watch boot, recharge, and power-loss logs.

---

## 🔭 Future Improvements

- Move to a 32-bit MCU (e.g. ESP32) for more RAM headroom and native deep-sleep support
- Encrypt/sign the RFID token block instead of a raw unit value
- Add a LoRaWAN gateway integration for cloud dashboards
- Multi-card / multi-user support with per-user balances

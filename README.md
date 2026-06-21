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

<svg width="100%" viewBox="0 0 680 1010" xmlns="http://www.w3.org/2000/svg" role="img">
<title>Full smart meter circuit diagram</title>
<desc>Complete wiring of the smart prepaid energy meter: mains power stepping down through a DC-DC converter to a 5V rail, a holdup capacitor and voltage divider for mains-loss sensing, and an Arduino Uno wired to voltage and current sensors, a relay, an RFID reader, a LoRa radio, an LCD, and an RTC</desc>
<defs>
<marker id="arrow" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
<path d="M2 1L8 5L2 9" fill="none" stroke="#5F5E5A" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>
</marker>
</defs>
<style>
text { font-family: -apple-system, "Segoe UI", Arial, sans-serif; }
.th { font-size: 14px; font-weight: 600; }
.ts { font-size: 12px; font-weight: 400; }
.box { fill: #F1EFE8; stroke: #5F5E5A; }
.box .th { fill: #2C2C2A; }
.box .ts { fill: #5F5E5A; }
.amber { fill: #FAEEDA; stroke: #854F0B; }
.amber .th { fill: #412402; }
.amber .ts { fill: #854F0B; }
.teal { fill: #E1F5EE; stroke: #0F6E56; }
.teal .th { fill: #04342C; }
.teal .ts { fill: #0F6E56; }
.coral { fill: #FAECE7; stroke: #993C1D; }
.coral .th { fill: #4A1B0C; }
.coral .ts { fill: #993C1D; }
.purple { fill: #EEEDFE; stroke: #534AB7; }
.purple .th { fill: #26215C; }
.purple .ts { fill: #534AB7; }
.blue { fill: #E6F1FB; stroke: #185FA5; }
.blue .th { fill: #042C53; }
.blue .ts { fill: #185FA5; }
.line { fill: none; stroke: #73726C; stroke-width: 1; }
</style>

<rect x="40" y="40" width="600" height="300" rx="16" fill="none" stroke="#73726C" stroke-width="1" stroke-dasharray="6 4"/>
<text class="th" x="60" y="62" fill="#2C2C2A">External power supply</text>

<line x1="200" y1="108" x2="230" y2="108" class="line" marker-end="url(#arrow)"/>
<line x1="420" y1="108" x2="450" y2="108" class="line" marker-end="url(#arrow)"/>

<g class="amber">
<rect x="60" y="80" width="140" height="56" rx="8" stroke-width="1"/>
<text class="th" x="130" y="100" text-anchor="middle" dominant-baseline="central">Mains supply</text>
<text class="ts" x="130" y="118" text-anchor="middle" dominant-baseline="central">220V AC</text>
</g>

<g class="amber">
<rect x="230" y="80" width="190" height="56" rx="8" stroke-width="1"/>
<text class="th" x="325" y="100" text-anchor="middle" dominant-baseline="central">DC-DC buck converter</text>
<text class="ts" x="325" y="118" text-anchor="middle" dominant-baseline="central">Steps down to 5V</text>
</g>

<g class="amber">
<rect x="450" y="80" width="130" height="56" rx="8" stroke-width="1"/>
<text class="th" x="515" y="100" text-anchor="middle" dominant-baseline="central">5V rail</text>
<text class="ts" x="515" y="118" text-anchor="middle" dominant-baseline="central">Board supply</text>
</g>

<path d="M515,136 L515,180" class="line"/>
<path d="M450,136 L450,150 L280,150 L280,340" class="line" marker-end="url(#arrow)"/>

<g class="teal">
<rect x="440" y="180" width="150" height="56" rx="8" stroke-width="1"/>
<text class="th" x="515" y="200" text-anchor="middle" dominant-baseline="central">Holdup capacitor</text>
<text class="ts" x="515" y="218" text-anchor="middle" dominant-baseline="central">Bridges rail briefly</text>
</g>

<path d="M515,236 L515,260" class="line"/>

<g class="amber">
<rect x="440" y="260" width="150" height="56" rx="8" stroke-width="1"/>
<text class="th" x="515" y="280" text-anchor="middle" dominant-baseline="central">45K/45K divider</text>
<text class="ts" x="515" y="298" text-anchor="middle" dominant-baseline="central">Senses mains loss</text>
</g>

<path d="M515,316 L515,330 L420,330 L420,340" class="line" marker-end="url(#arrow)"/>

<line x1="280" y1="340" x2="280" y2="560" class="line" marker-end="url(#arrow)"/>
<line x1="420" y1="340" x2="420" y2="560" class="line" marker-end="url(#arrow)"/>
<text class="ts" x="290" y="470" text-anchor="start" fill="#5F5E5A">5V power</text>
<text class="ts" x="430" y="420" text-anchor="start" fill="#5F5E5A">D3 interrupt</text>

<g class="teal">
<rect x="40" y="400" width="190" height="70" rx="8" stroke-width="1"/>
<text class="th" x="135" y="426" text-anchor="middle" dominant-baseline="central">Voltage sensor</text>
<text class="ts" x="135" y="446" text-anchor="middle" dominant-baseline="central">ZMPT101B to A0</text>
</g>

<g class="teal">
<rect x="450" y="400" width="190" height="70" rx="8" stroke-width="1"/>
<text class="th" x="545" y="426" text-anchor="middle" dominant-baseline="central">Current sensor</text>
<text class="ts" x="545" y="446" text-anchor="middle" dominant-baseline="central">SCT-013 to A1</text>
</g>

<path d="M135,470 L250,560" class="line" marker-end="url(#arrow)"/>
<path d="M545,470 L450,560" class="line" marker-end="url(#arrow)"/>

<g class="box">
<rect x="230" y="560" width="260" height="90" rx="10" stroke-width="1"/>
<text class="th" x="360" y="593" text-anchor="middle" dominant-baseline="central">Arduino Uno</text>
<text class="ts" x="360" y="616" text-anchor="middle" dominant-baseline="central">ATmega328P, 5V logic</text>
</g>

<path d="M270,650 L135,760" class="line" marker-end="url(#arrow)"/>
<path d="M450,650 L545,760" class="line" marker-end="url(#arrow)"/>

<g class="coral">
<rect x="40" y="760" width="190" height="70" rx="8" stroke-width="1"/>
<text class="th" x="135" y="786" text-anchor="middle" dominant-baseline="central">Relay module</text>
<text class="ts" x="135" y="806" text-anchor="middle" dominant-baseline="central">D4 control</text>
</g>

<g class="purple">
<rect x="450" y="760" width="190" height="70" rx="8" stroke-width="1"/>
<text class="th" x="545" y="786" text-anchor="middle" dominant-baseline="central">MFRC522 RFID</text>
<text class="ts" x="545" y="806" text-anchor="middle" dominant-baseline="central">SPI plus D8/D9</text>
</g>

<path d="M360,650 L360,840" class="line"/>
<circle cx="360" cy="840" r="3" fill="#73726C"/>
<path d="M360,840 L130,840 L130,900" class="line" marker-end="url(#arrow)"/>
<path d="M360,840 L360,860" class="line"/>
<circle cx="360" cy="860" r="3" fill="#73726C"/>
<path d="M360,860 L340,860 L340,900" class="line" marker-end="url(#arrow)"/>
<path d="M360,860 L550,860 L550,900" class="line" marker-end="url(#arrow)"/>

<g class="purple">
<rect x="40" y="900" width="180" height="70" rx="8" stroke-width="1"/>
<text class="th" x="130" y="926" text-anchor="middle" dominant-baseline="central">LoRa SX1278</text>
<text class="ts" x="130" y="946" text-anchor="middle" dominant-baseline="central">SPI plus D10/D7/D2</text>
</g>

<g class="blue">
<rect x="250" y="900" width="180" height="70" rx="8" stroke-width="1"/>
<text class="th" x="340" y="926" text-anchor="middle" dominant-baseline="central">LCD 16x2</text>
<text class="ts" x="340" y="946" text-anchor="middle" dominant-baseline="central">I2C, address 0x27</text>
</g>

<g class="blue">
<rect x="460" y="900" width="180" height="70" rx="8" stroke-width="1"/>
<text class="th" x="550" y="926" text-anchor="middle" dominant-baseline="central">DS3231 RTC</text>
<text class="ts" x="550" y="946" text-anchor="middle" dominant-baseline="central">I2C, shared bus</text>
</g>
</svg>
---

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

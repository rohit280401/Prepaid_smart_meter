# 🔌 Sensor & Module Connections (Arduino + LoRa Shield System)

## 🧰 Hardware Used

* Arduino (UNO/Nano with ETS LoRa Shield)
* Relay Module (5V)
* ACS712 Current Sensor 
* ZMPT101B Voltage Sensor
* 16x2 LCD
* MFRC522 RFID Module

---

# ⚠️ IMPORTANT (READ FIRST)

* LoRa shield already uses **SPI (D10–D13)**
* RFID also uses SPI → **pin conflict possible**
* You must use **separate SS pins** carefully

---

# 📡 1. ACS712 Current Sensor

| ACS712 | Arduino |
| ------ | ------- |
| VCC    | 5V      |
| GND    | GND     |
| OUT    | A1      |

👉 Measures current

---

# ⚡ 2. ZMPT101B Voltage Sensor

| ZMPT | Arduino |
| ---- | ------- |
| VCC  | 5V      |
| GND  | GND     |
| OUT  | A0      |

👉 Measures AC voltage

---

# 🔌 3. Relay Module (Active LOW assumed)

| Relay | Arduino |
| ----- | ------- |
| VCC   | 5V      |
| GND   | GND     |
| IN    | D3      |

### AC Side:

| Wire    | Connection     |
| ------- | -------------- |
| Live    | COM            |
| NO      | Load           |
| Neutral | Direct to load |


---

# 📟 4. 16x2 LCD (I2C)

| LCD | Arduino |
| --- | ------- |
| VCC | 5V      |
| GND | GND     |
| SDA | A4      |
| SCL | A5      |

👉 Default I2C address: `0x27` (may vary)

---

# 🪪 5. MFRC522 RFID (SPI)

| RFID     | Arduino         |
| -------- | --------------- |
| SDA (SS) | D8  |
| SCK      | D13             |
| MOSI     | D11             |
| MISO     | D12             |
| RST      | D9              |
| GND      | GND             |
| 3.3V     | 3.3V ⚠️         |

---

## ⚠️ VERY IMPORTANT (SPI Sharing)

| Device | SS Pin |
| ------ | ------ |
| LoRa   | D10    |
| RFID   | D8     |

👉 Only one SPI device active at a time

---

# 🔋 Power Summary

| Component | Voltage      |
| --------- | ------------ |
| Arduino   | USB / 5V     |
| Relay     | 5V           |
| LCD       | 5V           |
| ACS712    | 5V           |
| ZMPT      | 5V           |
| RFID      | ⚠️ 3.3V ONLY |

---

# 🚨 Common Mistakes

## ❌ RFID on 5V

→ will damage module

## ❌ Same SS pin for LoRa & RFID

→ SPI conflict → nothing works

## ❌ Relay powering Arduino line

→ system shuts down when load OFF

## ❌ No common ground

→ unstable readings

---

# 🧠 System Overview

```text
Voltage → ZMPT → A0
Current → ACS712 → A1
RFID → SPI (D8)
LoRa → SPI (D10)
Relay → D3
LCD → I2C (A4, A5)
```

---

# 🎯 Final Notes

* Keep wiring short and clean
* Use common GND everywhere
* Test modules individually before integration

---

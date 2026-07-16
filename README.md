# IoT Prepaid Smart Energy Meter with Dynamic Auto-SF LoRa Architecture

An advanced, long-range, firmware-optimized prepaid smart electrical energy meter system built for resource-constrained microcontrollers (e.g., **ATmega328P**). The system features real-time AC telemetry extraction, over-the-air (OTA) remote balance recharges, dynamic grid tariff adjustments, and a zero-latency hardware **Dying Gasp** backup mechanism that guarantees non-volatile data preservation during sudden grid blackouts.

---

# 🌟 Key Architecture Features

### ⚡ Interrupt-Driven "Dying Gasp" Safeguard

Uses a hardware external interrupt during power collapse. By re-enabling global interrupts inside the ISR (`interrupts()`), the firmware avoids SPI/LoRa stack deadlocks and successfully:

- Transmits one final blackout notification packet.
- Saves energy accounting data into EEPROM.
- Completes both operations within an approximately **13 ms** capacitor-backed power window.

---

### 📶 Dynamic Spreading Factor (SF) Adaptation

The Utility Gateway continuously monitors

- RSSI
- SNR

Based on link quality, it computes the optimum LoRa Spreading Factor (SF7–SF12) and sends the new configuration back inside acknowledgment packets. This automatically improves communication reliability while minimizing airtime and power consumption.

---

### ⏱ Cooperative Non-Blocking Scheduler

Instead of using an RTOS, the firmware employs a lightweight `millis()`-based scheduler, allowing multiple tasks to execute cooperatively while remaining within the **2 KB SRAM** limitation of the ATmega328P.

---

### 🔒 Secure Packet Validation Engine

All LoRa packets use structured transaction frames enclosed within `<...>` together with monotonically increasing **Transaction IDs (TXID)** to provide:

- Duplicate packet rejection
- Replay attack protection
- Reliable OTA recharge processing

---

# 🛠 Hardware Pin Configuration

| Component | Pin | Description |
|------------|-----|-------------|
| Grid Relay | D4 | Controls consumer load (Active LOW) |
| Voltage Sensor | A0 | AC voltage measurement using EmonLib |
| Current Sensor | A1 | AC current measurement using EmonLib |
| LoRa CS | D10 | SPI Chip Select |
| LoRa Reset | D7 | SX1276 Reset |
| LoRa DIO0 | D2 | Packet Received Interrupt |
| Power Sense | D3 | External interrupt for Dying Gasp |
| LCD I2C | A4/A5 | SDA / SCL |

---

# 📦 System Architecture

```text
                 Telemetry Packets
+----------------------+ ----------------------> +----------------------+
|                      |                         |                      |
|   Consumer Meter     |                         |   Utility Gateway    |
|      (Node-1)        | <---------------------- |                      |
|                      |      ACK / Commands     |                      |
+----------------------+                         +----------------------+

        |                                                |
        |                                                |
        |                                                |
        v                                                v

  Dying Gasp Engine                            Dynamic SF Engine
  -----------------                            -----------------
  • Detects power loss                         • Monitors RSSI
  • Saves EEPROM                               • Monitors SNR
  • Sends final LoRa packet                    • Computes optimum SF
                                               • Sends SF update
```

---

# 🔄 System Workflow

## 1. Power Measurement

The smart meter continuously samples voltage and current using **EmonLib**, calculating:

- RMS Voltage
- RMS Current
- Active Power
- Power Factor
- Energy Consumption

---

## 2. Telemetry Transmission

The firmware periodically transmits operating information according to the configurable

```cpp
telemetryIntervalMs
```

parameter.

---

## 3. Gateway Processing

The Utility Gateway

- receives telemetry,
- evaluates RSSI and SNR,
- computes the optimum Spreading Factor,
- sends an acknowledgment containing configuration updates.

---

# 📡 Communication Protocol

## Meter → Gateway

```text
<Node1;
V:230.12;
I:4.25;
P:978.01;
PF:0.98;
BAL:450.50;
RATE:8.00;
LAST_ID:1042;
DT:2026-07-16_21:00:00;
SF:7>
```

---

## Gateway → Meter (Recharge)

```text
<Node1;
TXID:1043;
RECH:500.00;
SF:7>
```

---

## Gateway → Meter (Tariff Update)

```text
<Node1;
TXID:1044;
TARI:9.50;
SF:7>
```

---

## ACK Packet

```text
<ACK;
Node1;
TXID:1043;
STATUS:RECH_OK;
SF:7;
RSSI:-88;
SNR:8.5>
```

---

# 🚀 Setup

## Required Arduino Libraries

Install the following libraries from the Arduino Library Manager:

- EmonLib
- LoRa (Sandeep Mistry)
- RTClib (Adafruit)
- LiquidCrystal_I2C

---

## Frequency Configuration

The firmware is configured for

```
865 MHz
```

which complies with India's license-free LoRa band.

Change

```cpp
LORA_FREQ
```

inside `config.h` when deploying in another region.

---

# 📥 Deployment

### Consumer Meter

Upload

```
money_based_smart_meter.ino
```

to the consumer node.

### Utility Gateway

Upload

```
utility_center.ino
```

to the gateway hardware.

Open the Serial Monitor at

```
9600 baud
```

to monitor incoming telemetry and gateway operations.

---

# 📈 Features Summary

- ✅ Real-Time Energy Metering
- ✅ Prepaid Electricity Billing
- ✅ OTA Recharge
- ✅ Remote Tariff Updates
- ✅ Dynamic LoRa Spreading Factor
- ✅ EEPROM Data Protection
- ✅ Dying Gasp Backup
- ✅ Replay Attack Protection
- ✅ Transaction Validation
- ✅ Low-Memory Cooperative Scheduler

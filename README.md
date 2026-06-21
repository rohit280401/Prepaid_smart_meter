# IoT-Enabled Smart Prepaid Energy Meter

An advanced, ATmega328P-based prepaid electricity metering system featuring real-time AC power monitoring, secure RFID-based token recharges, non-blocking cooperative multitasking, a "Dying Gasp" power-failure protection mechanism, and long-range wireless telemetry via LoRa.

---

## 📋 System Overview & Features

This system functions as a digital Pay-As-You-Go (PAYG) electricity meter designed to monitor utility power lines dynamically and enforce credit limits via a physical relay switch.

* **Prepaid Balance Enforcement:** Power is delivered to the consumer load only if the meter maintains a positive balance. Credit is consumed dynamically based on computed watt-hours.
* **Real-Time AC Metrology:** Accurately samples voltage and current waves to calculate True RMS Voltage, RMS Current, Real Power (Watts), and Power Factor (PF) utilizing the `EmonLib` framework.
* **Wireless Telemetry:** Dispatches periodic status packets over LoRa (configured for the 865 MHz ISM band in India) containing current system health metrics and a unified timestamp.
* **Hardware-Level Data Preservation ("Dying Gasp"):** Monitors raw input rails via a hardware interrupt. If a primary power drop is caught, it sheds all peripheral loads instantly, runs a critical EEPROM dump to save user balance, and broadcasts an emergency LoRa alert before the system completely powers down.
* **Cooperative Scheduler:** Avoids CPU-blocking `delay()` instructions entirely, keeping RFID scanning, telemetry, power calculations, and the I2C LCD screen cycling at precise, independent intervals.

---

## 🔌 Hardware Connection Table

The following table outlines the complete wiring mapping for the microcontroller, sensing components, communications modules, and local user interfaces:

| Peripheral Module | Pin Name | Arduino Uno/Nano Pin | Signal Type / Notes |
| :--- | :--- | :--- | :--- |
| **Consumer Relay** | IN | `D4` | Digital Output (LOW = Load Connected, HIGH = Isolated) |
| **AC Voltage Sensor** | Analog Out | `A0` | Analog Input (Sensed via Step-down AC Transformer) |
| **AC Current Sensor** | Analog Out | `A1` | Analog Input (Sensed via Current Transformer - CT) |
| **Dying Gasp Monitor**| V_Sense | `D3` | Digital Input (Hardware Interrupt 1 / Connected to 45K/45K Divider) |
| **MFRC522 RFID** | SDA (SS) | `D8` | SPI Chip Select (Isolated on hardware sleep) |
| **MFRC522 RFID** | SCK | `D13` | SPI Clock |
| **MFRC522 RFID** | MOSI | `D11` | SPI Master Output, Slave Input |
| **MFRC522 RFID** | MISO | `D12` | SPI Master Input, Slave Output |
| **MFRC522 RFID** | RST | `D9` | Digital Output / Hard Reset line for power management |
| **SX1278 LoRa** | NSS (SS) | `D10` | SPI Chip Select |
| **SX1278 LoRa** | RST | `D7` | Digital Output / Module Reset |
| **SX1278 LoRa** | DIO0 | `D2` | Digital Input (Hardware Interrupt 0 / Packet TX/RX completion) |
| **DS3231 RTC** | SDA | `A4` (SDA) | I2C Data Line |
| **DS3231 RTC** | SCL | `A5` (SCL) | I2C Clock Line |
| **I2C 16x2 LCD** | SDA / SCL | Shared I2C | Addresses on `0x27` for secondary display loop |

---

## 🛠 Circuit Diagram Description

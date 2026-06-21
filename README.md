Here is a complete, production-ready `README.md` file for your GitHub repository. It compiles all your hardware configurations, features, engineering challenges, and solutions into a beautifully structured, professional format.

You can copy and paste the Markdown block below directly into your repository:

```markdown
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


```

```
              +-----------------------------------------+
              |            ATmega328P Master            |
              +-----------------------------------------+
                |       |       |       |       |     |
     +----------+       |       |       |       |     +----------+
     | (SPI)            | (I2C) | (Int) | (Int) | (GPIO)         | (Analog)
     v                  v       v       v       v                v

```

+--------------+    +--------+ +-----+ +-----+ +-------+    +--------------+
|  LoRa SX1278 |    | DS3231 | | RFID| |Power| | Relay |    | Emon AC Pins |
|  Transceiver |    |   RTC  | |RC522| |Sense| |Control|    | (Volt / Curr)|
+--------------+    +--------+ +-----+ +-----+ +-------+    +--------------+
|                  |        |                |
v                  v        v                v
+--------+          [45K/45K] [AC Line]     [Transformers/
|I2C LCD |          Divider]    Switch       Sensing Nets]
+--------+

```

The system topology is split into three layers:
1. **Sensing Layer:** The mains power grid passes through current and voltage sensing nets. These break down high voltages into low-voltage analog signals readable at `A0` and `A1`. Concurrently, a $45\,\text{k}\Omega / 45\,\text{k}\Omega$ voltage divider reduces the $5\text{V}$ rail input down into an early-warning signal mapped straight into external interrupt pin `D3`.
2. **Control & Storage Layer:** The microcontroller processes incoming computations in RAM. Storage tasks are offloaded to an internal EEPROM for tracking balance metadata, while real-time timestamps are pulled sequentially from the I2C DS3231 module. 
3. **Execution & Telemetry Layer:** Physical current isolation is triggered through a transistor driving a 5V relay line (`D4`). Continuous system updates are pushed over an SPI bus shared between the MFRC522 and SX1278 LoRa modules using individual chip-select controls (`D8` and `D10`).

---

## 📈 System Pros (Advantages)

* **Robust Power Failure Safeguards:** The combination of a hardware-driven interrupt paired with an external decoupling capacitor network ensures data corruption is impossible during sudden grid dropouts.
* **Low Idle Current Leakage:** Implements dynamic hardware power isolation. When the balance is completely safe, the system safely triggers a deep shutdown of the high-draw RFID antenna circuit, reducing overall standby consumption.
* **True Non-Blocking Traversal:** By utilizing structural microsecond scheduling, the system can compute highly demanding root-mean-square current calculations without freezing UI updates or missing short RFID tap windows.
* **Localized Temporal Validity:** Relying on a dedicated hardware DS3231 real-time clock ensures that timestamping remains completely accurate, even if wireless gateway connectivity is broken for extended durations.

---

## ⚠️ Challenges Faced & Engineering Solutions

### 1. The Broken Early-Warning Noise Gate
* **The Problem:** Early iterations utilized a crash threshold value of `630` for the voltage-divider crash sensing loop. Because a standard operational $5\text{V}$ line across a $45\,\text{k}\Omega / 45\,\text{k}\Omega$ divider only produces an ADC value of `512`, the early warning system was stuck in a permanent false-alarm loop, crashing the meter instantly during boot.
* **The Solution:** Adjusted the software crash threshold down to `500` (representing roughly $4.5\text{V}$). This establishes a reliable noise gate that ignores nominal grid ripples but acts quickly enough during real power drops to let the decoupling capacitors complete an EEPROM save.

### 2. Parasitic SPI Cross-Backpowering
* **The Problem:** To save power, the system pulled the RFID `RST_PIN` low when credit was available. However, because the system loop continued to call `mfrc522.PCD_AntennaOff()` over the SPI bus while the RFID chip was unpowered, current leaked into the dead peripheral via the shared MOSI/SCK lines. This caused systematic hardware freezes.
* **The Solution:** Centralized the power control routines into dedicated `shutdownRFIDReader()` and `wakeupRFIDReader()` blocks. When shutting down the module, the shared data lines are temporarily converted into high-impedance inputs (`INPUT`), decoupling parasitic current loops completely.

### 3. I2C Bus Deadlocks Inside the ISR
* **The Problem:** The initial "Dying Gasp" routine executed `rtc.now()` inside an Interrupt Service Routine (ISR) to timestamp the error log. Because the `Wire` (I2C) library relies on active system interrupts to clear hardware flags, running it inside an ISR (where nested interrupts are disabled) caused permanent CPU lockups before the EEPROM write could even begin.
* **The Solution:** Removed all I2C calls from the ISR. The Dying Gasp routine now only interacts with the raw hardware state, cuts the consumer load, writes the raw balance bytes directly to EEPROM (which does not depend on nested interrupts), and utilizes a swift `volatile` flag to let the main loop safely process the remaining telemetry commands.

### 4. The Token Duplicate Extraction Exploit
* **The Problem:** The RFID transaction routine added credit to the internal meter variable *before* verifying that the balance on the physical RFID card token block was wiped. If a user quickly removed their card mid-read, they could get credited on the meter without clearing the card, leading to an exploit for free, infinite energy recharges.
* **The Solution:** Structured the transaction logic so that it is strictly transaction-safe. The system now authenticates the card, reads the value, clears the card token memory block to zero, verifies that the block write was completely successful, and only *then* credits `availableUnits` in the meter's volatile RAM.

---

## 🚀 Deployment Instructions

1. **Library Dependencies:** Install the following libraries within your Arduino IDE environment:
   * `EmonLib`
   * `MFRC522`
   * `LiquidCrystal_I2C`
   * `LoRa` (by Sandeep Mistry)
   * `RTClib` (by Adafruit)
2. **Hardware Configuration:** Review `config.h` to make sure pins match your custom PCB or development board layout. Modify `LORA_FREQ` if deploying outside of India (865 MHz).
3. **Flashing the Node:** Compile and upload `smart_meter.ino` to your microchip target. On startup, check the Serial Monitor at `9600 baud` to verify that the EEPROM data recovery and peripheral checkouts pass successfully.

```

# 💧👁️ tuya-universal-sensors
**Universal ESPHome C++ custom component for TuyaMCU v0 battery-powered water leak and PIR sensors.**

Supports **ESP8266** (`TYWE3S`) and **BK7231N** (`CBU`) via [LibreTiny](https://github.com/libretiny-eu/libretiny).

Fixes two critical issues that break UART communication on battery-powered Tuya devices:
- Beken bootloader serial noise (garbage bytes on cold boot)
- Grouped frame buffer truncation (multiple frames sent in a 4–5s wakeup window)

> ⚠️ **Important Hardware Note:**  
> If a device behaves erratically after initial flashing, or if a button-triggered OTA attempt fails and the device does not recover on its own, a **manual power cycle is required**. Remove the batteries and wait for all capacitors on the board to fully discharge. To speed up the discharge, you can briefly short the power input pads with tweezers or a jumper wire. Once fully discharged, reinsert the batteries — the device will boot cleanly and enter the correct operating mode.

## 🚀 Why This Component?

Battery-powered Tuya sensors wake up for only 4–5 seconds per event. In this ultra-short window, any UART instability, bootloader noise, or frame parsing error breaks communication. This component was built specifically to survive that window.

Key advantages:
- **UART protocol repair**: reconstructs fragmented or concatenated TuyaMCU data frames.
- **Noise suppression**: filters Beken bootloader garbage and cleans GPIO lines before UART starts.
- **Universal codebase**: one component, three devices, shared `.cpp`/`.h` files.
- **OTA for ultra-low-power devices**: uses a physical button to keep Wi‑Fi powered for 60–180s for wireless updates.
- **MQTT-ready**: integrates with Home Assistant via MQTT or direct ESPHome API.

## 📋 Table of Contents

- [Features](#features)
- [Tested Devices](#tested-devices)
- [Quick Start](#quick-start)
- [ESPHome Version Compatibility](#esphome-version-compatibility)
- [Environment & Build Target](#environment--build-target)
- [First-Time Flashing (By Wire)](#first-time-flashing-flashing-by-wire)
- [Device Reset & OTA Firmware Update Logic](#device-reset--ota-firmware-update-logic)
- [Device-Specific Notes](#device-specific-notes)
- [Repository Structure](#repository-structure)
- [How It Works](#how-it-works)
- [Troubleshooting](#troubleshooting)
- [License](#license)

## ✨ Features

- **UART protocol repair** — reconstructs TuyaMCU data frames that arrive fragmented or concatenated due to the low-power wakeup cycle.
- **Beken bootloader noise filter** — strips garbage bytes injected by the BK7231N bootloader on cold boot, which corrupt the first UART frames.
- **Port noise suppression** — cleans GPIO lines and clears serial buffer garbage during `setup()` before UART communication begins.
- **Battery-compatible** — designed for devices where the Wi‑Fi module is fully unpowered between events (4–5s active window).
- **OTA support** — wireless firmware updates via physical button override (no disassembly needed after initial flash).
- **MQTT-ready** — integrates with Home Assistant via MQTT or direct ESPHome API.
- **Universal codebase** — one component, three devices, shared `.cpp` / `.h` files.

## 🧪 Tested Devices

| Device | MCU | Wi‑Fi Module | Protocol | Battery Monitoring |
|--------|-----|-------------|----------|-------------------|
| Tuya water leak sensor (battery) | TuyaMCU v0 | TYWE3S (ESP8266) | UART 9600 8N1 | Voltage divider (internal ADC) |
| Tuya PIR motion sensor (battery) | TuyaMCU v0 | TYWE3S (ESP8266) | UART 9600 8N1 | TuyaMCU DP report |
| Tuya PIR motion sensor (battery) | TuyaMCU v0 | CBU (BK7231N) | UART 9600 8N1 | TuyaMCU DP report |

> **Note:** Any TuyaMCU v0 battery device using the `55 AA` frame protocol over UART at 9600 baud should be compatible. Untested devices may require minor DP‑ID adjustments in the configuration.

## ⚡ Quick Start

1. Clone the repo and pick the config matching your hardware (ESP8266 or BK7231N).
2. Fill in your variables: device name, static IP, MQTT broker, Wi‑Fi credentials.
3. Flash the device:
   - **ESP8266**: `esptool.py --port /dev/ttyUSB0 write_flash 0x0 firmware.bin`
   - **BK7231N**: `ltchiptool flash firmware.bin` or the [LibreTiny flasher](https://github.com/libretiny-eu/libretiny)
4. Restore UART TX/RX lines between TuyaMCU and Wi‑Fi chip.
5. Use the reset button to open the OTA window for future updates.

## 🔧 Environment & Build Target

This component has been verified and compiled using the production release of ESPHome:

- **ESPHome Version:** `2026.6.6` (recommended)
- **Configuration Hash:** `0xa44d4e42`
- **Build Timestamp:** `2026-09-21 21:42:27 +0300`

## ⚡ First-Time Flashing (Flashing “By Wire”)

Battery-powered devices keep the Wi‑Fi module completely powered down until a physical sensor event or button press occurs. Performing the initial firmware installation requires temporary hardware modifications:

1. **Isolate the MCU:** Disconnect or desolder the `TX` and `RX` lines between the TuyaMCU and the Wi‑Fi module (`TYWE3S` or `CBU`) to prevent serial bus contention during programming.
2. **Apply Stable External Power:** Provide a solid, continuous `3.3V` power supply directly to the Wi‑Fi chip from your USB‑to‑UART adapter (ensure common ground `GND`). Do not rely on the device’s battery lines during this stage.
3. **Flash the Binary:** Use your standard serial flashing utility:
   - **ESP8266:** `esptool.py --port /dev/ttyUSB0 write_flash 0x0 firmware.bin`
   - **BK7231N:** `ltchiptool flash firmware.bin` or the [LibreTiny flasher](https://github.com/libretiny-eu/libretiny)
4. **Restore the Bus:** Once successfully flashed, reconnect the UART lines back to the TuyaMCU.

## 🔘 Device Reset & OTA Firmware Update Logic

Once the device is assembled and running on battery power, it is impossible to flash it over‑the‑air (OTA) under normal operation because the chip cuts its own power line within **4–5 seconds**.

To bypass this and push updates wirelessly, use the **physical reset / pairing button**:

1. **Trigger the timeout loop:** Press and hold the physical button inside the device until the status LEDs begin to blink rapidly.
2. **MCU hold‑on state:** The TuyaMCU detects this hardware override sequence and enters an autoconfiguration / pairing mode. Instead of shutting down after 4 seconds, the MCU will force‑keep the Wi‑Fi chip powered on for approximately **60–180 seconds**, waiting for a network handshake.
3. **Executing OTA:** Within this window, your automation scripts must initiate the wireless ESPHome OTA update process.
4. **Automatic reboot:** If the OTA fails or times out, the device will automatically reboot after ~60 seconds, cut the power rail, and return to its ultra‑low‑power event‑driven sleep routine.

### Button Sequence

- **First press** → OTA mode (LEDs blinking) — flash wirelessly within ~1–3 minutes.
- **Second press** → full device reset.

## 🔩 Device-Specific Notes

### PIR Motion Sensor (ESP8266 / TYWE3S)

- **Static IP is mandatory.** The firmware uses static IP addresses — this is the most reliable approach for battery‑powered devices where DHCP lease times are unpredictable.
- **OTA procedure:** Press and hold the reset button → LEDs start blinking → OTA window opens for ~1 minute. Flash wirelessly within this window.
- **MQTT hostname over IP:** In the MQTT configuration, use the device hostname (e.g., `Home System Local`) instead of a raw IP address. Hostname resolution is faster and more reliable than direct IP for these battery devices — likely due to the short wakeup window and ARP/MQTT broker handshake timing.
- **Button sequence:** First press → OTA mode (blinking).

### Water Leak Sensor (ESP8266 / TYWE3S)

- **Static IP** — same as PIR, static addressing is used in firmware for reliability.
- **OTA procedure:** Identical to PIR — press reset button → blinking → flash OTA within ~1 minute.
- **Battery monitoring via voltage divider:** The water leak sensor outputs a constant voltage from the battery line. The TuyaMCU always reports 100% battery, making the MCU-reported value useless. To get a real battery reading, a hardware voltage divider is used: **100 kΩ** on the high side and **47 kΩ** on the low side, feeding the ESP8266 ADC pin. This produces a measurable voltage (~1V range) that reflects the actual battery level.
- **Divider is optional:** If you prefer not to solder a divider, you can modify the configuration file to change the ADC pin assignment to read the supply voltage directly. In this case, the firmware will read the voltage value reported by the TuyaMCU in UART frames — if the MCU provides one. If no voltage data is present in the frames, the firmware will report whatever the ADC pin reads directly.
- **Reset reminder system:** Since the sensor sleeps and does not actively report battery level, a dedicated entity pushes the last session timestamp to Home Assistant. An automation records the date of each communication session and sends a reminder notification (configurable: 6 months, 1 year) to physically reset the leak sensor. This ensures the sensor remains responsive and the battery measurement stays accurate over long periods.
- **Button sequence:** Same as PIR — first press → OTA mode (blinking). Second press → full device reset.

### PIR Motion Sensor (BK7231N / CBU)

- **New device — accidentally acquired.** The general code was adapted for this platform and works, though the UART protocol behaves slightly differently (larger frame structure, but no additional issues observed).
- **MQTT by IP only.** Unlike the ESP8266 variant where a hostname (`Home System Local`) works faster, the BK7231N module requires a **direct IP address** for the MQTT broker. Using a hostname causes significant slowdowns — likely due to DNS resolution overhead in the LibreTiny stack during the short 4–5 second wakeup window.
- **Initial flashing:** Requires `ltchiptool` or the [LibreTiny flasher](https://github.com/libretiny-eu/libretiny).
- **Bootloader serial noise filtering** is especially important on this platform — see [How It Works](#how-it-works).
- **OTA and reset logic:** Same as ESP8266 — press reset button → blinking → flash OTA within ~1 minute.

## 📁 Repository Structure

This component follows the **standard ESPHome external component** layout. All three device configurations are included — simply choose the one matching your hardware, fill in your variables (device name, static IP, MQTT broker, Wi‑Fi credentials), and flash.

Each device has its own config with pre‑filled defaults. The `.cpp` and `.h` component files are shared across all configurations — the code is universal.

> **Note:** Replace DP‑IDs, pin assignments, and network settings with values matching your specific device. The example above is a starting point — the actual `.cpp` / `.h` component files contain the protocol parser and frame reconstruction logic.

## How It Works

### Universal Protocol Initialization

The component uses a **heartbeat‑first** initialization sequence:

1. **Heartbeat** — on each wakeup, the component sends a heartbeat frame to the TuyaMCU. If the MCU responds, communication is established immediately.
2. **Product query fallback** — if the heartbeat receives no response within the timeout, the component automatically falls back to a product query (`0x04` command). This requests the device’s product key and DP configuration from the MCU.
3. Both paths are fully supported. The heartbeat path is faster and is used on ESP8266 devices. The product query fallback was added during BK7231N development, where the heartbeat was not initially accepted — the MCU required a full product query handshake before responding to any other commands.

### Port Noise Suppression (Unified Setup)

The breakthrough for BK7231N came from a custom `setup()` block that performs **port noise suppression** — cleaning garbage bytes and suppressing noisy GPIO lines before UART communication begins. This solved the BK7231N ↔ TuyaMCU communication failure entirely.

The same algorithm was then applied to the ESP8266 variant — not strictly necessary, but it unified the codebase into a single, reliable initialization sequence. The result is one universal scanner/component that works identically across all three supported devices.

### Problem 1: Bootloader Serial Noise (BK7231N)

On cold boot, the Beken bootloader injects garbage bytes into the UART RX buffer before the TuyaMCU starts sending valid `55 AA` frames. The component filters these by:

1. Scanning the incoming buffer for the `0x55 0xAA` sync header.
2. Discarding all bytes before the first valid sync sequence.
3. Resetting the parser state machine on each wakeup cycle.

### Problem 2: Grouped Frame Buffer Truncation

Battery devices wake for only 4–5 seconds. During this window, the TuyaMCU may send multiple frames back‑to‑back. The standard ESPHome Tuya component often reads only the first frame and discards the rest. This component:

1. Maintains a **private circular buffer** that accumulates bytes across multiple `loop()` iterations.
2. Extracts complete frames by checking the `length` field in the Tuya protocol header.
3. Processes all valid frames found in the buffer, not just the first one.
4. Clears the buffer on each new wakeup cycle to prevent stale data.

## Troubleshooting

| Symptom | Cause | Solution |
|---------|-------|----------|
| No data in Home Assistant | TX/RX lines not restored after flashing | Double‑check soldering on UART lines |
| Device behaves erratically after flashing | Residual charge in capacitors | Remove batteries, discharge capacitors (short power pads if needed), reinsert batteries |
| OTA fails and device does not recover | OTA window expired without clean reboot | Power-cycle the device (remove batteries, wait for discharge, reinsert) |
| Water leak sensor always reports 100% battery | TuyaMCU does not provide real battery data | Install a 100 kΩ / 47 kΩ voltage divider on the ADC pin, or reconfigure ADC to read supply voltage directly |
| Garbage in logs on BK7231N | Bootloader noise not filtered | Ensure you're using the latest component build |
| OTA fails after 60s | Window expired before upload completed | Reduce binary size or use faster OTA speed |
| Device not detected after battery replacement | TuyaMCU needs full power cycle | Remove batteries for 10s, then reinsert |
| Frames appear truncated in logs | Buffer overflow or old component version | Check buffer log lines — buffer should clear on each wake |
| BK7231N extremely slow over MQTT | Hostname used instead of IP | Switch MQTT broker config to direct IP address |
| ESP8266 slow over MQTT | IP address used instead of hostname | Try using hostname (e.g., `Home System Local`) instead |

## How to Use as an External Component

```yaml
external_components:
  - source:
      type: git
      url: https://github.com
    components: [ tuya_uart ]

# Example configuration of the component
tuya_uart:
  id: uart_bus
```

---

### Как это сделать на сайте GitHub:
1. Откройте ваш репозиторий, нажмите на файл `README.md`, а затем на иконку **карандаша** (Edit).
2. Прокрутите в самый низ файла, сделайте отступ в одну строку и вставьте скопированный текст выше.
3. Нажмите кнопку **Commit changes** в правом верхнем углу для сохранения.


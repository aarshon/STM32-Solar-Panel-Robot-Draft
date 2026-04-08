# STM32 Solar Panel Robot — Integrated Firmware

A differential-drive solar panel inspection robot.  A Raspberry Pi reads a physical joystick and streams control data over MQTT → an ESP8266 bridges it to UART → an STM32 Nucleo-F767ZI drives two VESC ESCs.  A 128×64 OLED displays live motor telemetry and Pi connection status.  A 3×4 keypad provides local fallback control. (Robotic Arm logic to be implemented later).

---

## Table of Contents

- [System Architecture](#system-architecture)
- [Hardware List](#hardware-list)
- [Communication Flow](#communication-flow)
- [UART Pin Map](#uart-pin-map)
- [Software Components](#software-components)
  - [Raspberry Pi — HMI Controller](#raspberry-pi--hmi-controller)
  - [ESP8266 — MQTT Bridge](#esp8266--mqtt-bridge)
  - [STM32 — Merged Firmware](#stm32--merged-firmware)
    - [Motor Driver (vesc.c / bldc_interface)](#motor-driver-vescc--bldc_interface)
    - [Joystick Parser & Ian's Mixing Math](#joystick-parser--ians-mixing-math)
    - [OLED UI (ui.c / ssd1306)](#oled-ui-uic--ssd1306)
    - [Keypad (keypad.c)](#keypad-keypadc)
    - [Safety Watchdog](#safety-watchdog)
- [Communication Protocol Reference](#communication-protocol-reference)
- [OLED Screen Reference](#oled-screen-reference)
- [Build & Flash Instructions](#build--flash-instructions)
- [Raspberry Pi Setup](#raspberry-pi-setup)
- [Operating Instructions](#operating-instructions)
- [Future Work — Arm Integration](#future-work--arm-integration)
- [Team & Credits](#team--credits)

---

## System Architecture

```
┌────────────────────────────────────────────────────────────────────┐
│  Raspberry Pi 4 (Python 3)                                          │
│                                                                      │
│  MCP3008 ADC ──► Joystick (CH0=X, CH1=Y)                           │
│  raw_to_vehicle_norm_x/y()  →  float [0.0, 1.0]  (centre = 0.5)   │
│                                                                      │
│  paho-mqtt client ──► localhost:1883  ──► topic "vehicle/base"      │
│  payload: {"x": 0.52, "y": 0.73}   published at 40 Hz             │
│                                                                      │
│  WiFi client connected to ESP8266 AP "ESP32_AP"                     │
│  Pi IP on that network: 192.168.4.2                                 │
│  Mosquitto broker listening on all interfaces (:1883)               │
└────────────────────────────┬───────────────────────────────────────┘
                             │ WiFi 802.11  (SSID: ESP32_AP)
                             ▼
┌────────────────────────────────────────────────────────────────────┐
│  ESP8266 (click shield on STM32 Nucleo)                             │
│  Firmware: esp8266_mqtt.ino                                         │
│                                                                      │
│  ● Creates WiFi AP "ESP32_AP" (password: esp32password)             │
│  ● MQTT client → Pi broker @ 192.168.4.2:1883                      │
│  ● Subscribes to topic "vehicle/base"                               │
│  ● On each message:                                                 │
│      JSON {"x": f, "y": f}  →  4-byte binary frame                 │
│      [0xAA] [x_u8] [y_u8] [0x55]                                   │
│      float [0,1] → uint8 [0,255]  (centre = 128)                   │
│  ● Serial.write(frame, 4) at 115200 baud → STM32 UART4             │
└────────────────────────────┬───────────────────────────────────────┘
                             │ UART  (115200 baud, 3.3 V logic)
                             │ ESP8266 TX → STM32 PA1 (UART4 RX)
                             ▼
┌────────────────────────────────────────────────────────────────────┐
│  STM32F767ZI Nucleo (merged firmware — Vehicle_Base/)               │
│                                                                      │
│  UART4 RX ISR:                                                      │
│    Parse [0xAA][x][y][0x55] → x_float, y_float ∈ [-1.0, +1.0]     │
│                                                                      │
│  Ian's tank-drive mixing:                                           │
│    left_duty  = clamp(y + x) × 0.50                                │
│    right_duty = clamp(y - x) × 0.50                                │
│                                                                      │
│  bldc_interface library:                                            │
│    UART5 TX ──────────────────────► Left  VESC (FESC 6.7)          │
│    UART7 TX ──────────────────────► Right VESC (FESC 6.7)          │
│                                                                      │
│  UART5 RX ◄── Left VESC telemetry (RPM, V, A, temp, fault)         │
│                                                                      │
│  I2C1 ↔ SSD1306 OLED (128×64)                                      │
│    STATUS_MONITOR: RPM · Voltage · Current · Fault · Pi status      │
│                                                                      │
│  Keypad matrix (3×4): local fallback motor control                  │
│                                                                      │
│  Watchdog: stop motors if no frame in 500 ms                        │
└──────────────────────────────────────────────────────────────────  ┘
```

---

## Hardware List

| Component | Part / Model | Role |
|---|---|---|
| MCU | STM32F767ZI (Nucleo-F767ZI) | Main controller |
| Left ESC | FlipSky FESC 6.7 (VESC 6.x compatible) | Left drive motor |
| Right ESC | FlipSky FESC 6.7 (VESC 6.x compatible) | Right drive motor |
| Single-board computer | Raspberry Pi 4 | HMI + MQTT publisher |
| WiFi bridge | ESP8266 click shield | MQTT → UART bridge |
| Joystick ADC | MCP3008 (8-ch SPI ADC on Pi) | Joystick position reading |
| Display | SSD1306 128×64 OLED (I2C) | Status display on robot |
| Keypad | 3×4 membrane matrix keypad | Local manual control |
| Battery / UPS | UPS module (UART at /dev/serial0) | Power monitoring on Pi |

---

## Communication Flow

```
Joystick (physical)
  │  SPI @ 1 MHz
  ▼
MCP3008 (CH0=X, CH1=Y)
  │  raw ADC [0, 1023]
  ▼
Pi Python: raw_to_vehicle_norm_x/y()
  │  normalised float [0.0, 1.0]
  ▼
paho-mqtt publish
  │  topic: "vehicle/base"
  │  payload: {"x": 0.52, "y": 0.73}
  │  rate: 40 Hz
  ▼
Mosquitto broker (Pi localhost:1883, visible on 192.168.4.2)
  │
  ▼
ESP8266 MQTT subscribe
  │  JSON decode + float→uint8 scale
  ▼
4-byte binary frame [0xAA][x_byte][y_byte][0x55]
  │  UART 115200 baud
  ▼
STM32 UART4 RX ISR
  │  frame parser + latch to esp_cmd_x / esp_cmd_y
  ▼
Main loop: convert [0,255] → [-1,+1] float
  │  Ian's mixing: left = y+x, right = y-x
  ▼
VESC_JoystickDrive(x, y)  →  vesc.c
  │  bldc_interface_uart_init + bldc_interface_set_duty_cycle
  ├─ UART5 TX ──► Left  VESC  (COMM_SET_DUTY 0x05)
  └─ UART7 TX ──► Right VESC  (COMM_SET_DUTY 0x05)

Telemetry path (200 ms polling):
  VESC_RequestValues()  →  COMM_GET_VALUES (0x04) on UART5 TX
  UART5 RX byte-by-byte ──► VESC_ProcessRxByte()
  bldc_interface decoded ──► on_vesc_values(mc_values*)
  ──► UI_TelemetryUpdate() ──► OLED STATUS_MONITOR screen
```

---

## UART Pin Map

| Peripheral | STM32 Pins | Baud | Connected To | Direction |
|---|---|---|---|---|
| UART4 | PA1 (RX) | 115200 | ESP8266 TX | RX only (joystick data in) |
| UART5 | PC12 (TX), PC11 (RX) | 115200 | Left VESC | TX commands + RX telemetry |
| UART7 | PE8 (TX) | 115200 | Right VESC | TX commands only |
| USART3 | PD8 (TX), PD9 (RX) | 115200 | ST-Link (debug) | TX debug output |
| I2C1 | PB8 (SCL), PB9 (SDA) | 400 kHz | SSD1306 OLED | Bidirectional |

**Keypad matrix:**

| Signal | GPIO |
|---|---|
| ROW1 | PE7 |
| ROW2 | PE8 |
| ROW3 | PE10 |
| ROW4 | PE12 |
| COL1 | PE14 |
| COL2 | PG9 |
| COL3 | PG14 |

---

## Software Components

### Raspberry Pi — HMI Controller

**File:** `Rasp Code/NotFinalRaspPiCode1.py`

**Language:** Python 3  
**Key dependencies:** `paho-mqtt`, `spidev`, `pygame`, `RPi.GPIO`

**What it does:**
- Full-screen pygame UI (640×480) with multiple pages: Main, Controls, Info, Settings, WiFi
- Reads joystick position via MCP3008 ADC (SPI bus 0, CS0) on channels 0 and 1
- Normalises joystick to `[0.0, 1.0]` (centre = 0.5) using `raw_to_vehicle_norm_x/y()`
- Publishes to MQTT topic `"vehicle/base"` as `{"x": float, "y": float}` at 40 Hz
- Manages WiFi connection to `"ESP32_AP"` profile using `nmcli`
- Monitors UPS battery over `/dev/serial0` at 9600 baud
- Publishes arm servo angles on `"servos/angles"` (future use)

**Dead-zone handling (in Python):**
- Centre X = 680 ADC counts, Centre Y = 528 ADC counts
- Dead zone: ±20 counts — below this threshold the joystick returns 0.5 (centre)

**MQTT Configuration:**
```
Broker:  127.0.0.1:1883  (localhost — Mosquitto on the Pi)
Topic:   vehicle/base
Payload: {"x": 0.52, "y": 0.73}   (floats, [0.0, 1.0])
Rate:    40 Hz publish, 80 Hz event loop
```

---

### ESP8266 — MQTT Bridge

**File:** `Rasp Code/esp8266_mqtt.ino`

**Platform:** ESP8266 (Arduino IDE, libraries: ESP8266WiFi, PubSubClient, ArduinoJson)

**What it does:**
1. Creates a WiFi access point: SSID `"ESP32_AP"`, password `"esp32password"`
2. Connects to Pi's MQTT broker at `192.168.4.2:1883` (Pi's IP on the AP network)
3. Subscribes to `"vehicle/base"`
4. On each message: decodes JSON `{"x": f, "y": f}`, converts floats to uint8 bytes, assembles a 4-byte binary frame, and sends it over hardware UART to the STM32

**Frame format:**

```
Byte 0: 0xAA  — start marker (constant)
Byte 1: x_u8  — X (steering)  as uint8; float [0,1] × 255, centre = 128
Byte 2: y_u8  — Y (throttle)  as uint8; float [0,1] × 255, centre = 128
Byte 3: 0x55  — end marker (constant)
```

**UART settings:** 115200 baud, 8N1 — matches STM32 UART4

**Reconnect logic:** If MQTT connection drops, `loop()` calls `connectMQTT()` which retries with a 500 ms delay.

---

### STM32 — Merged Firmware

**Project folder:** `Vehicle_Base/`  
**Toolchain:** STM32CubeIDE (GCC ARM)  
**MCU:** STM32F767ZI @ 96 MHz (HSE 8 MHz bypass via ST-Link MCO + PLL)

The merged firmware combines:
- Structure and peripherals from **Vehicle_Base** (OLED, keypad, UI state machine)
- Motor library from **Base-Ctrl** (bldc_interface / VESC binary protocol)
- OLED test display driver from **OLED Test Display** (already integrated in Vehicle_Base)

#### Motor Driver (vesc.c / bldc_interface)

**Files:** `Core/Src/vesc.c`, `Core/Inc/vesc.h`  
**Library files copied from Base-Ctrl:**
- `Core/Src/bldc_interface.c` — VESC command serialisation (Benjamin Vedder, GPLv3)
- `Core/Src/bldc_interface_uart.c` — UART transport layer
- `Core/Src/packet.c` — framing (start/stop bytes, CRC16)
- `Core/Inc/bldc_interface.h`, `bldc_interface_uart.h`, `packet.h`
- `Core/Inc/buffer.h` — big-endian serialisation helpers
- `Core/Inc/crc.h`, `Core/Inc/datatypes.h` — CRC16 + VESC data types

**VESC binary frame format (generated by bldc_interface):**
```
[0x02] [payload_len] [payload_bytes...] [CRC16_hi] [CRC16_lo] [0x03]

COMM_SET_DUTY (0x05):
  payload = [0x05] [duty_b3] [duty_b2] [duty_b1] [duty_b0]
  duty is signed int32, big-endian: range -100000 to +100000 = -100% to +100%
  bldc_interface_set_duty_cycle(float) scales automatically

COMM_GET_VALUES (0x04):
  Request:  payload = [0x04]  (1 byte)
  Response: payload = [0x04] [18 fields, mixed types, big-endian] (73 bytes)
```

**Dual-motor architecture:**  
`bldc_interface` holds one internal send-function pointer.  `vesc.c` swaps it before each command using `bldc_interface_uart_init()`:

```c
VESC_JoystickDrive(x, y)
  ├── bldc_interface_uart_init(send_packet_left)
  │   bldc_interface_set_duty_cycle(left_duty)    → UART5 TX
  └── bldc_interface_uart_init(send_packet_right)
      bldc_interface_set_duty_cycle(right_duty)   → UART7 TX
      bldc_interface_uart_init(send_packet_left)  ← restore default
```

**Public API:**
```c
void VESC_Init(void);                             // call once after UART init
void VESC_TankDrive(uint8_t direction, uint8_t speed); // keypad control
void VESC_JoystickDrive(float x, float y);        // joystick control (Ian's math)
void VESC_Stop(void);                             // zero both motors
void VESC_SetValuesCallback(vesc_values_cb_t cb); // register telemetry callback
void VESC_RequestValues(void);                    // send COMM_GET_VALUES request
void VESC_ProcessRxByte(uint8_t byte);            // feed UART5 RX byte to decoder
```

#### Joystick Parser & Ian's Mixing Math

**File:** `Core/Src/main.c`

The ESP8266 sends `[0xAA][x_byte][y_byte][0x55]` over UART4.  The STM32 ISR synchronises on the `0xAA` start byte and validates the `0x55` end byte.

Once a valid frame is latched, the main loop applies:

```c
// Step 1: convert uint8 [0, 255] to float [-1.0, +1.0]
//   centre byte (128) → ≈0.0   full-forward byte (255) → +1.0   full-back (0) → -1.0
float x = ((float)esp_cmd_x - 127.5f) / 127.5f;   // steering
float y = ((float)esp_cmd_y - 127.5f) / 127.5f;   // throttle

// Step 2: Ian's tank-drive mixing
//   left motor  = throttle + steering
//   right motor = throttle - steering
// (result is clamped to [-1,+1] and scaled by VESC_MAX_DUTY_FLOAT = 0.50)
VESC_JoystickDrive(x, y);
```

Examples:
| Joystick | x | y | Left duty | Right duty |
|---|---|---|---|---|
| Full forward | 0.0 | +1.0 | +0.50 | +0.50 |
| Full backward | 0.0 | -1.0 | -0.50 | -0.50 |
| Spin left | -1.0 | 0.0 | -0.50 | +0.50 |
| Spin right | +1.0 | 0.0 | +0.50 | -0.50 |
| Forward-right | +0.5 | +0.7 | +0.50 (clamped) | +0.10 |

#### OLED UI (ui.c / ssd1306)

**Files:** `Core/Src/ui.c`, `Core/Inc/ui.h`, `Core/Src/ssd1306.c`, `Core/Inc/ssd1306.h`  
**Display:** SSD1306 128×64 OLED monochrome, I2C address 0x3C

**Screens (navigate with keypad):**

| Screen | Key to enter | Description |
|---|---|---|
| SPLASH | — | Animated boot screen (1.5 s auto-advance) |
| MAIN_MENU | Auto after splash | 4-item cursor menu |
| STATUS_MONITOR | `[1]` or `[#]` | Live telemetry + Pi connection status |
| MOTOR_CONTROL | `[2]` | Drive from keypad ([2]FWD [8]BCK [4]LEFT [6]RIGHT) |
| ROBOT_ARM | `[3]` | Placeholder (arm team) |
| INFO | `[4]` | Uptime, firmware version, fault log |

**STATUS_MONITOR layout:**

```
┌──────────────────────────────┐  128×64
│ STATUS         [OK] or [ERR] │  y=0   (title + fault badge)
│──────────────────────────────│  y=11  (divider)
│ RPM:+1234   V:13.2           │  y=13  (RPM + input voltage)
│ I: 4.5A  Dty:  45%           │  y=25  (motor current + duty)
│ Tmp:32C  Flt: OK             │  y=37  (FET temp + fault code)
│══════════════════════════════│  y=48  (divider)
│ Pi:LIVE  234ms               │  y=50  (connection status, Font_6x8)
│ CTRL:REMOTE                  │  y=57  (control source, Font_6x8)
└──────────────────────────────┘
```

When the watchdog fires (no Pi command in 500 ms):
- Row 4 becomes **inverted**: white background, `Pi:TIMEOUT  STOPPED`
- Row 5 shows `CTRL:KEYPAD/IDLE`

#### Keypad (keypad.c)

**File:** `Core/Src/keypad.c`, `Core/Inc/keypad.h`

3×4 matrix scan, 30 ms debounce, 600 ms hold detection, 150 ms auto-repeat.

| Key | Motor Control screen | Main Menu |
|---|---|---|
| [2] | Forward | Cursor up |
| [8] | Backward | Cursor down |
| [4] | Turn left | — |
| [6] | Turn right | — |
| [5] or [0] | Stop | Select |
| [1] / [3] | Speed −10 / +10 | — |
| [7] / [9] | Speed preset 60 / 220 | — |
| [*] | Stop + back to menu | — |
| [#] | — | Select |

#### Safety Watchdog

**Defined in:** `main.c` (`CMD_TIMEOUT_MS = 500`)

```
Main loop checks every iteration:
  if motorRunning AND (HAL_GetTick() - lastCmdTime > 500 ms):
      VESC_Stop()          → zero both motors immediately
      motorRunning = 0
      UI_ForceRedraw()     → OLED shows "Pi:TIMEOUT STOPPED"
      debug print → USART3
```

The VESC firmware has a **second independent watchdog** at ~1 second.  If the STM32 crashes or hangs, the VESC will still self-stop.

---

## Communication Protocol Reference

### ESP8266 → STM32 (UART4, 115200 baud)

```
[0xAA] [x_byte] [y_byte] [0x55]
  │       │        │       │
  │       │        │       └─ End marker (constant)
  │       │        └───────── Throttle (Y axis): uint8 [0,255], 128=centre
  │       └────────────────── Steering (X axis): uint8 [0,255], 128=centre
  └────────────────────────── Start marker (constant)
```

Conversion: `float = (byte - 127.5) / 127.5`  →  range [-1.0, +1.0]

### STM32 → VESC (UART5 / UART7, 115200 baud)

VESC binary protocol (bldc_interface):
```
[0x02] [len] [payload...] [CRC16_hi] [CRC16_lo] [0x03]
  └ for payloads ≤ 256 bytes; len is 1 byte

COMM_SET_DUTY (0x05) payload (5 bytes):
  [0x05] [duty_b3] [duty_b2] [duty_b1] [duty_b0]
  duty is signed int32 big-endian: bldc_interface converts float→int internally
  range: -100000 (+100000) = -100% (+100%) full throttle
  demo cap: ±50000 = ±50% (VESC_MAX_DUTY_FLOAT = 0.50)

COMM_GET_VALUES (0x04) request (1 byte payload):
  [0x04]
```

### Pi → MQTT Broker (localhost:1883)

```
Topic:   vehicle/base
Payload: {"x": 0.52, "y": 0.73}
  x: steering,  float [0.0, 1.0], centre = 0.5
  y: throttle,  float [0.0, 1.0], centre = 0.5
Rate: 40 Hz (every 25 ms)
```

---

## OLED Screen Reference

### Fault Codes (STATUS_MONITOR)

| Code | Display | Meaning |
|---|---|---|
| FAULT_CODE_NONE | `OK ` | No fault — normal operation |
| FAULT_CODE_OVER_VOLTAGE | `OV ` | Input voltage too high |
| FAULT_CODE_UNDER_VOLTAGE | `UV ` | Input voltage too low (battery low) |
| FAULT_CODE_DRV | `DRV` | DRV8302 gate driver fault |
| FAULT_CODE_ABS_OVER_CURRENT | `OC ` | Absolute over-current |
| FAULT_CODE_OVER_TEMP_FET | `OTF` | FET temperature too high |
| FAULT_CODE_OVER_TEMP_MOTOR | `OTM` | Motor temperature too high |
| (other) | `ERR` | Unknown fault — inspect VESC over VESC Tool |

---

## Build & Flash Instructions

### STM32 Firmware (STM32CubeIDE)

1. Open STM32CubeIDE.
2. **File → Import → Existing Projects into Workspace** → select `Vehicle_Base/`.
3. Verify the following source files are in the project build:
   - `Core/Src/main.c`, `vesc.c`, `ui.c`, `keypad.c`, `oled.c`, `ssd1306.c`, `ssd1306_fonts.c`
   - `Core/Src/bldc_interface.c`, `bldc_interface_uart.c`, `packet.c`
   - All existing HAL/CMSIS drivers
4. If any of the bldc_interface files are not recognised, right-click the file in Project Explorer → **Properties → C/C++ Build → Exclude from build** (ensure it is **not** excluded).
5. Build: **Project → Build All** (or `Ctrl+B`).  There should be 0 errors.
6. Connect the Nucleo board via ST-Link USB.
7. Flash: **Run → Debug** (or press F11).  The firmware will halt at main(); press Resume (F8) to run.

### ESP8266 Firmware (Arduino IDE)

1. Install board support: **File → Preferences → Additional Board URLs** → add the ESP8266 board manager URL.
2. Install libraries via **Sketch → Include Library → Manage Libraries**:
   - `PubSubClient` (MQTT client)
   - `ArduinoJson` (JSON parsing)
3. Open `Rasp Code/esp8266_mqtt.ino`.
4. Select board: **Tools → Board → ESP8266 Boards → Generic ESP8266 Module**.
5. Select the COM port for your click shield.
6. Upload.

---

## Raspberry Pi Setup

### Dependencies

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients python3-pip -y
pip3 install paho-mqtt spidev pygame RPi.GPIO
```

### Enable Mosquitto auto-start

```bash
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
# Verify: should print "active (running)"
sudo systemctl status mosquitto
```

### Enable SPI (for MCP3008)

```bash
sudo raspi-config
# Interface Options → SPI → Enable
sudo reboot
```

### Connect to ESP8266 WiFi

```bash
# Check if the profile exists
nmcli connection show

# Connect to ESP32_AP (created by the ESP8266 click shield)
nmcli connection up ESP32_AP
# or first-time add:
nmcli device wifi connect "ESP32_AP" password "esp32password"
```

### Run the HMI script

```bash
cd ~/STM32-Solar-Panel-Robot-Draft/Rasp\ Code
python3 NotFinalRaspPiCode1.py
```

### Test MQTT manually (without running the full Pi script)

```bash
# Simulate joystick full-forward command:
mosquitto_pub -h 127.0.0.1 -t vehicle/base -m '{"x":0.5,"y":1.0}'

# Simulate stop:
mosquitto_pub -h 127.0.0.1 -t vehicle/base -m '{"x":0.5,"y":0.5}'
```

---

## Operating Instructions

### Power-On Sequence

1. Power the robot (battery → VESCs → STM32).
2. Power the Raspberry Pi.
3. Pi will auto-connect to ESP8266 WiFi AP `"ESP32_AP"` (if profile is saved).
4. STM32 OLED shows splash screen for 1.5 s, then the main menu.
5. Navigate to **Status Monitor** screen (press `[1]` on keypad or `[#]` to select from menu).
6. Run the Pi HMI script.  Within ~2 s the OLED should show `Pi:LIVE` and receive telemetry.

### Normal Operation

- **Joystick:** move stick forward → robot drives forward; tilt sideways → differential steering.
- **OLED STATUS_MONITOR:** shows live RPM, voltage, current, duty%, FET temperature, fault code, and Pi connection status.
- **Keypad fallback:** press `[3]` in menu → Motor Control screen → use keypad `[2][8][4][6]` for manual control.  Speed adjustable with `[1]`/`[3]` keys.

### Watchdog Behaviour

If the Pi disconnects or the script stops:
- OLED shows `Pi:TIMEOUT  STOPPED` in an inverted bar within 500 ms.
- Both motors stop immediately.
- To resume: restart the Pi script; OLED will return to `Pi:LIVE` once frames resume.

### Debug Output (ST-Link)

Connect a serial terminal (e.g. PuTTY, minicom) to the Nucleo's ST-Link virtual COM port at **115200 baud**.  Messages include:
- `[BASE] Solar Robot firmware started` — on boot
- `[BASE] Watchdog: motor stop` — each time watchdog fires

---

## Future Work — Arm Integration

The arm team will publish servo angles to MQTT topic `"servos/angles"`:
```json
{"s1": 90.0, "s2": 45.0}
```

The Pi HMI (`NotFinalRaspPiCode1.py`) already publishes this from the second joystick axis when in the Controls screen.

**Integration points for arm firmware:**
- Subscribe to `"servos/angles"` on the Raspberry Pi's MQTT broker.
- Implement a 500 ms timeout (same as vehicle base) to stop servos if MQTT connection drops — as Ian suggested: *"we could probably have the arm team do something similar on their end"*.
- The STM32 `SCREEN_ROBOT_ARM` screen in `ui.c` is a placeholder ready for arm status display.

---

## Team & Credits

| Name | Role |
|---|---|
| Aarsh | STM32 firmware integration, OLED UI, system architecture |
| Ian | VESC motor control, tank-drive mixing algorithm |
| Raspberry Pi / HMI Team | Pi Python HMI, joystick normalisation, MQTT |
| Arm Team | Servo control (future integration) |

**Third-party libraries:**
- [afiskon/stm32-ssd1306](https://github.com/afiskon/stm32-ssd1306) — SSD1306 OLED driver (MIT)
- [vedderb/bldc](https://github.com/vedderb/bldc) — bldc_interface library (GPLv3, Benjamin Vedder)
- [knolleary/pubsubclient](https://github.com/knolleary/pubsubclient) — Arduino MQTT client (MIT)
- [bblanchon/ArduinoJson](https://github.com/bblanchon/ArduinoJson) — JSON for Arduino (MIT)
- [eclipse/paho.mqtt.python](https://github.com/eclipse/paho.mqtt.python) — Python MQTT client (EPL/EDL)

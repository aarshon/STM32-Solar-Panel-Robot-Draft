# STM32 Solar Panel Robot — Vehicle Base

A differential-drive solar panel robot controlled by an STM32 Nucleo-F767ZI. The system accepts drive commands from both a local 3×4 membrane keypad and a remote Raspberry Pi over UART. A 128×64 OLED displays a multi-screen UI with live VESC telemetry. 1 VESC ESCs (FlipSky FESC 6.7) drive the left and right motors via a tank-drive scheme.

---

## Table of Contents

- [Hardware Overview](#hardware-overview)
- [System Architecture](#system-architecture)
- [Vehicle Base (STM32 Firmware)](#vehicle-base-stm32-firmware)
  - [BASE — Main Loop & Command Processing](#base--main-loop--command-processing)
  - [VESC Motor Driver](#vesc-motor-driver)
  - [HMI — Keypad Input](#hmi--keypad-input)
  - [UI — OLED Multi-Screen Interface](#ui--oled-multi-screen-interface)
  - [OLED Driver](#oled-driver)
- [Raspberry Pi (High-Level Controller)](#raspberry-pi-high-level-controller)
- [UART Communication Map](#uart-communication-map)
- [Command Protocol (Pi → STM32)](#command-protocol-pi--stm32)
- [Pin Reference](#pin-reference)
- [Building & Flashing](#building--flashing)

---

## Hardware Overview

| Component | Part |
|---|---|
| MCU | STM32F767ZI (Nucleo-F767ZI) |
| Left ESC | FlipSky FESC 6.7 (VESC-compatible) |
| Right ESC | FlipSky FESC 6.7 — addressed via CAN forwarding |
| Display | SSD1306 128×64 OLED (I2C) |
| HMI | 3×4 membrane keypad |
| High-level computer | Raspberry Pi (any model with UART) |
| Clock | 96 MHz (HSE 8 MHz + PLL ×96/4) |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────┐
│                   Raspberry Pi                          │
│  (path planning, solar tracking, high-level logic)      │
│                  USART1 @ 115200                        │
└────────────────────────┬────────────────────────────────┘
                         │ ASCII text commands
                         │ FWD,<spd>  BCK,<spd>
                         │ LFT,<spd>  RGT,<spd>
                         │ STP  ESTOP
                         ▼
┌─────────────────────────────────────────────────────────┐
│              STM32 Nucleo-F767ZI                        │
│                                                         │
│  ┌──────────┐   ┌──────────┐   ┌──────────────────┐     │
│  │  Keypad  │   │   BASE   │   │   VESC Driver    │     │
│  │  (HMI)  │──▶│ main.c   │──▶│   vesc.c         │      │
│  │ keypad.c │   │          │   │  binary protocol │     │
│  └──────────┘   └────┬─────┘   └────────┬─────────┘     │
│                      │                 │                │
│                      ▼                 │                │
│               ┌──────────┐             │ USART2         │
│               │    UI    │             │ @ 115200       │
│               │  ui.c    │             ▼                │
│               │ OLED     │   ┌──────────────────┐       │
│               │ screens  │   │  Left VESC ESC   │       │
│               └──────────┘   │  (direct UART)   │       │
│                    │         └────────┬─────────┘       │
│                    ▼                  │ CAN forward     │
│             SSD1306 OLED             ▼                  │
│             128×64 I2C       ┌──────────────────┐       │
│                              │  Right VESC ESC  │       │
│                              │  (CAN ID 1)      │       │
│                              └──────────────────┘       │ 
└─────────────────────────────────────────────────────────┘
```

---

## Vehicle Base (STM32 Firmware)

The firmware is a single STM32CubeIDE project under `Vehicle_Base/`. All application logic lives in `Core/Src/`.

### BASE — Main Loop & Command Processing

**File:** `Vehicle_Base/Core/Src/main.c`

The main loop runs four tasks every iteration, in order:

1. **Keypad scan** — calls `KEYPAD_Scan()` and stores the result.
2. **UI update** — calls `UI_Update(key)` to handle input and redraw the OLED if dirty.
3. **UART command dispatch** — if a complete newline-terminated command has arrived on USART1 (from the Raspberry Pi), `processCommand()` parses and executes it.
4. **Motor timeout watchdog** — if `motorRunning` is set but no command has arrived within 500 ms, `VESC_Stop()` is called automatically to prevent runaway.

A background telemetry poll fires every 200 ms: `VESC_RequestValues()` sends a `COMM_GET_VALUES` request; the decoded response is delivered via callback to `UI_TelemetryUpdate()`.

**UART RX interrupt (`HAL_UART_RxCpltCallback`):**
- **USART1** — accumulates incoming bytes into `cmdBuf[]` one character at a time. When `\n` or `\r` is received, the buffer is copied to `pendingCmd[]` and `cmdReady` is set.
- **USART2** — feeds each byte to `VESC_ProcessRxByte()` for telemetry frame assembly.

**Command handler (`processCommand`)** parses the command string, updates `lastCmdTime`, and calls `VESC_TankDrive()` with the decoded direction and speed. It also echoes a short status line on USART3 (ST-Link virtual COM / debug).

---

### VESC Motor Driver

**Files:** `Vehicle_Base/Core/Src/vesc.c`, `Vehicle_Base/Core/Inc/vesc.h`

Controls two VESC ESCs using the VESC binary UART protocol over **USART2** (`huart2`, PD5 TX / PD6 RX equivalent, 115200 baud).

#### Protocol Frame Format

```
[0x02] [payload_len] [payload bytes...] [CRC16_hi] [CRC16_lo] [0x03]
```

CRC is CRC16/XMODEM (poly `0x1021`, init `0x0000`) computed over the payload bytes only.

#### Motor Control

| Function | Description |
|---|---|
| `VESC_TankDrive(direction, speed)` | High-level tank drive. Converts 0–255 speed to duty ±50 000 and sets both motors. |
| `VESC_SetDuty(duty)` | Sends `COMM_SET_DUTY (0x05)` directly on USART2 — commands the **left** VESC. |
| `VESC_SetDutyRight(duty)` | Sends `COMM_FORWARD_CAN (0x21)` with CAN ID 1 — commands the **right** VESC through the left VESC's CAN port. |
| `VESC_Stop()` | Sets both motors to duty 0. |

**Tank-drive direction logic:**

| Direction | Left duty | Right duty |
|---|---|---|
| `DIR_FORWARD` | +duty | +duty |
| `DIR_BACKWARD` | −duty | −duty |
| `DIR_LEFT` | −duty | +duty |
| `DIR_RIGHT` | +duty | −duty |

#### Telemetry (RX)

`VESC_RequestValues()` sends a one-byte `COMM_GET_VALUES (0x04)` request. The VESC replies with a 74-byte payload containing RPM, input voltage, motor current, duty cycle, FET temperature, fault code, and more. `VESC_ProcessRxByte()` is called for each incoming byte from the UART ISR; once a complete, CRC-verified frame is assembled, `decode_get_values()` parses the big-endian fields into an `mc_values` struct and fires the registered callback.

Register your callback with:
```c
VESC_SetValuesCallback(my_callback);  // called with mc_values* on each telemetry packet
```

---

### HMI — Keypad Input

**Files:** `Vehicle_Base/Core/Src/keypad.c`, `Vehicle_Base/Core/Inc/keypad.h`

A standard 3×4 membrane keypad scanned by driving each row LOW in turn and reading the three column lines (input with pull-up). No EXTI interrupts are used to avoid conflicts with the RMII Ethernet pins.

#### Wiring

```
         COL1(PE14)  COL2(PG9)  COL3(PG14)
ROW1(PE7)   [1]        [2]        [3]
ROW2(PE8)   [4]        [5]        [6]
ROW3(PE10)  [7]        [8]        [9]
ROW4(PE12)  [*]        [0]        [#]
```

#### Debounce & Hold

| Parameter | Value |
|---|---|
| Debounce window | 30 ms |
| Hold threshold | 600 ms |
| Auto-repeat interval | 150 ms |

`KEYPAD_Scan()` returns:
- The ASCII character of the key (`'0'`–`'9'`, `'*'`, `'#'`) on a fresh press.
- The character ORed with `0x80` on hold/auto-repeat.
- `KEY_NONE (0xFF)` when nothing is pressed.

---

### UI — OLED Multi-Screen Interface

**Files:** `Vehicle_Base/Core/Src/ui.c`, `Vehicle_Base/Core/Inc/ui.h`

A dirty-flag driven multi-screen UI that renders to the SSD1306 OLED via the `ssd1306` library. Only re-renders when `needsRedraw` is set, except for the splash (animated) and status/info screens (polled every 200 ms).

#### Screens

| Screen | Description |
|---|---|
| **Splash** | Animated progress bar, auto-advances after 1.5 s. Any key skips it. |
| **Main Menu** | 4-item vertical menu. `[2]`/`[8]` navigate, `[5]` or `[#]` selects. |
| **Status Monitor** | Live VESC telemetry: RPM, input voltage, duty %, motor current, FET temperature, fault code. Displays `[ERR]` badge on active fault. `[*]` returns to menu. |
| **Motor Control** | Drive the robot directly from the keypad. Shows direction badge and speed bar. |
| **Robot Arm** | Placeholder screen — not yet implemented. |
| **Info** | Uptime counter, firmware label (`v1.0 F767ZI`), logged fault count. `[#]` clears the fault log. |

#### Motor Control Keypad Mapping

| Key | Action |
|---|---|
| `[2]` | Forward |
| `[8]` | Backward |
| `[4]` | Turn left |
| `[6]` | Turn right |
| `[5]` or `[0]` | Stop |
| `[1]` / `[3]` | Speed −10 / +10 |
| `[7]` / `[9]` | Speed preset: 60 / 220 |
| `[*]` | Stop + return to main menu |

#### Fault Flash

When a new VESC fault code is detected, the entire screen inverts in a 3-pulse flash (6 × 200 ms transitions) to alert the operator.

---

### OLED Driver

**Files:** `Vehicle_Base/Core/Src/oled.c`, `Vehicle_Base/Core/Inc/oled.h`

A lightweight SSD1306 driver using **page-addressing mode** over I2C1 (`hi2c1`, PB8 SCL / PB9 SDA, Fast mode 400 kHz). Contains a built-in 5×8 ASCII font covering characters `0x20`–`0x7E`.

The higher-level `ui.c` uses the `ssd1306` library (framebuffer-based, supports rectangles, lines, and multiple font sizes). `oled.c` provides a simpler page-direct API (`OLED_Init`, `OLED_Clear`, `OLED_Print`, `OLED_WriteStatus`) used during early bringup.

---

## Raspberry Pi (High-Level Controller)

The Raspberry Pi connects to the STM32 over a serial UART link (**USART1** on the STM32 side, 115200 8N1). Its responsibilities include:

- **Solar tracking** — computing optimal panel orientation and issuing drive commands to reposition the robot.
- **Path planning** — higher-level navigation and obstacle avoidance (implementation resides on the Pi).
- **Remote command generation** — sending ASCII drive commands (see protocol below) based on sensor data, camera feedback, or a remote control application.

The STM32 does not need to know anything about what the Pi is running; it simply parses command strings and drives the motors, with a 500 ms watchdog that stops the robot if commands stop arriving.

---

## UART Communication Map

| UART | STM32 Pins | Baud | Connected To | Direction |
|---|---|---|---|---|
| USART1 | PB6 TX / PB7 RX | 115 200 | Raspberry Pi | RX commands from Pi |
| USART2 | PA3 RX / PD5 TX (re-mapped) | 115 200 | Left VESC ESC | TX commands + RX telemetry |
| USART3 | PD8 TX / PD9 RX | 115 200 | ST-Link (debug) | TX debug echo |
| UART5 | PC12 TX / PB12 RX | 115 200 | Reserved / ARM controller | TBD |

---

## Command Protocol (Pi → STM32)

Commands are ASCII strings terminated with `\n` (or `\r`). Maximum length: 31 characters.

| Command | Description |
|---|---|
| `FWD,<speed>` | Drive forward at speed 0–255 |
| `BCK,<speed>` | Drive backward at speed 0–255 |
| `LFT,<speed>` | Turn left at speed 0–255 |
| `RGT,<speed>` | Turn right at speed 0–255 |
| `STP` | Stop both motors (graceful) |
| `ESTOP` | Emergency stop — stops motors and forces UI redraw |

If `<speed>` is omitted, a default of 150 is used. The watchdog stops the robot if no command arrives within **500 ms**.

**Example (from Pi terminal):**
```
echo -e "FWD,200\n" > /dev/ttyS0
```

---

## Pin Reference

| Signal | Pin | Notes |
|---|---|---|
| Keypad ROW1–ROW4 | PE7, PE8, PE10, PE12 | Output push-pull |
| Keypad COL1 | PE14 | Input pull-up |
| Keypad COL2–COL3 | PG9, PG14 | Input pull-up |
| OLED SCL | PB8 | I2C1 |
| OLED SDA | PB9 | I2C1 |
| VESC UART TX | (USART2 TX) | VESC binary protocol |
| VESC UART RX | (USART2 RX) | Telemetry receive |
| Pi UART TX→STM32 RX | PB7 (USART1 RX) | ASCII commands |
| Pi UART RX←STM32 TX | PB6 (USART1 TX) | (reserved / ACK) |
| Debug / ST-Link | PD8 TX / PD9 RX | USART3 |
| User button | PC13 | EXTI13 |
| LED Green | PB0 (LD1) | |
| LED Red | PB14 (LD3) | |

---

## Building & Flashing

1. Open the project in **STM32CubeIDE** (`Vehicle_Base/Vehicle_Base.ioc`).
2. Build with **Debug** or **Release** configuration (GCC toolchain, optimization `-O2` for Release).
3. Flash via the on-board ST-Link using **Run → Debug** or the ST-Link utility.
4. Monitor debug output on the ST-Link virtual COM port at 115 200 baud (USART3).

**Firmware package:** STM32Cube FW_F7 V1.17.4

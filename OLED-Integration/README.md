# OLED-Integration

Receive-only Solar Bot status display. Drop-in module for a CubeMX project on
the OLED-display board. This board is a passive listener: it reads frames off
a UART line, parses them, and renders the latest state of each system to an
SSD1306 OLED on I2C1. Nothing is ever transmitted on the UART side.

```
   (bridge UART tap)         I2C1
   ─────────────────►  STM32  ──────►  SSD1306 OLED
                       (RX only)       (write-only)
```

## What gets displayed

Frame routing is identified by `sysID` (byte 2 of every 12-byte packet):

| sysID | Label   | Direction       |
|------:|---------|-----------------|
| 0x01  | DRIVE   | CTRL → BASE     |
| 0x02  | ARM     | CTRL → ARM      |
| 0x03  | STATE   | ARM  → CTRL     |

The screen layout is fixed at 21 chars × 8 rows:

```
     SOLAR BOT
---------------------
DRIVE OK     45ms          ← link state + age since last DRIVE frame
 x:32768 y:32768           ← latest joystick from a DRIVE frame
ARM   OK    120ms          ← link state for ARM-CMD
 md:01 z:0 yaw:0           ← last ARM-CMD payload
STATE ----  ----ms         ← link state for ARM-STATE (none yet)
rx:00123 err:00005         ← total valid frames + bad-marker rejections
```

`OK` means a frame for that stream landed within the last 500 ms. `----` means
stale or never seen. `err` increments any time the parser drops a byte run
because the start/end markers didn't line up — useful for spotting baud or
wiring issues fast.

## Files

```
OLED-Integration/
├── README.md              ← this file
└── Core/
    ├── Inc/
    │   ├── oled.h         ← SSD1306 driver public API
    │   └── oled_status.h  ← receive-only status module public API
    └── Src/
        ├── oled.c         ← SSD1306 driver (page addressing, 5x8 font)
        └── oled_status.c  ← FSM parser + per-stream snapshots + refresh
```

## CubeMX setup (generate first, then drop these files in)

Pinout / Configuration:

- **I2C1** — Standard or Fast Mode, default pins:
  - PB8  → I2C1_SCL
  - PB9  → I2C1_SDA
  - The driver expects the handle to be named `hi2c1` (CubeMX default).

- **UART (any of UART2/4/8/etc.)** — RX-only is fine. Match the bridge baud
  rate. Today the ESP32 bridge in `Rasp Code/esp32s3_bridge/esp32s3_bridge.ino`
  uses **460 800 baud, 8N1**. Whichever UART you pick:
  - Enable global interrupt (or DMA RX) so bytes can be fed to the parser.
  - Common GND with whatever you're tapping.

- **System clock** — anything CubeMX gives you for the F767ZI is fine.
  HAL_Delay needs SysTick at 1 kHz (default).

After CubeMX generates `Core/Inc/main.h`, `Core/Src/main.c`, etc., copy:

- `oled.h` and `oled_status.h` into the generated `Core/Inc/`
- `oled.c` and `oled_status.c` into the generated `Core/Src/`

(or merge them into your existing tree).

## Glue code (add to the generated main.c)

Three insertion points, all inside `USER CODE BEGIN/END` markers so CubeMX
won't clobber them on regeneration. Replace `huartX` with the UART you
configured.

### 1. Includes

```c
/* USER CODE BEGIN Includes */
#include "oled.h"
#include "oled_status.h"
/* USER CODE END Includes */
```

### 2. After peripheral init, before `while (1)`

```c
/* USER CODE BEGIN 2 */
OLED_Init();
OLED_Status_Init();

static uint8_t rx_byte;
HAL_UART_Receive_IT(&huartX, &rx_byte, 1);   /* arm 1-byte RX */
/* USER CODE END 2 */
```

`rx_byte` must be `static` (or file-scope) — HAL keeps a pointer to it across
ISR firings.

### 3. Periodic refresh in the main loop

```c
/* USER CODE BEGIN WHILE */
while (1) {
    OLED_Status_Refresh();   /* self-gates to ~2 Hz; cheap to call hot */
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
```

### 4. RX completion callback (anywhere outside main, typically `USER CODE BEGIN 4`)

```c
/* USER CODE BEGIN 4 */
static uint8_t rx_byte;   /* same buffer used by HAL_UART_Receive_IT above */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == UARTX) {           /* match the peripheral you picked */
        OLED_Status_FeedByte(rx_byte);
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}
/* USER CODE END 4 */
```

If you'd rather use DMA + idle-line detection (more robust at high baud),
swap the IT calls for `HAL_UARTEx_ReceiveToIdle_DMA` and feed every byte of
the buffer to `OLED_Status_FeedByte` from `HAL_UARTEx_RxEventCallback`. The
status module doesn't care how bytes arrive.

## Notes on timing

The OLED uses blocking `HAL_I2C_Master_Transmit` and emits 6 bytes per
character. A full 8-row repaint is roughly 168 small I²C transactions:

- 100 kHz I²C: ~150 ms blocking
- 400 kHz I²C: ~30–40 ms blocking

Configure I²C in **Fast Mode (400 kHz)** in CubeMX if it isn't already. The
2 Hz refresh gate gives the main loop plenty of room either way.

## Out of scope (deliberately)

- No TX on UART. This board never sends a byte back.
- No CRC / no ack — same as the upstream comm protocol. Markers are the only
  integrity check; corruption shows up as a rising `err` counter on the
  display.
- No menu, no buttons, no e-stop. Pure status read-out.

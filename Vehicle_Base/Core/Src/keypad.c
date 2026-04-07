/*
 * keypad.c  —  3x4 Membrane Keypad Driver
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Polling-based scan with per-key confirmed-state debounce.
 * No EXTI interrupts used — avoids conflicts with RMII Ethernet pins.
 */

#include "keypad.h"
#include <string.h>

/* =========================================================================
 * Internal tables
 * ========================================================================= */

/* Key character map [row][col] */
static const char key_map[4][3] = {
    { '1', '2', '3' },
    { '4', '5', '6' },
    { '7', '8', '9' },
    { '*', '0', '#' }
};

/* Row port/pin arrays for iteration */
static GPIO_TypeDef * const row_ports[4] = {
    KP_ROW1_PORT, KP_ROW2_PORT, KP_ROW3_PORT, KP_ROW4_PORT
};
static const uint16_t row_pins[4] = {
    KP_ROW1_PIN, KP_ROW2_PIN, KP_ROW3_PIN, KP_ROW4_PIN
};

/* Column port/pin arrays for iteration */
static GPIO_TypeDef * const col_ports[3] = {
    KP_COL1_PORT, KP_COL2_PORT, KP_COL3_PORT
};
static const uint16_t col_pins[3] = {
    KP_COL1_PIN, KP_COL2_PIN, KP_COL3_PIN
};

/* =========================================================================
 * Per-key debounce state  (12 keys = 4 rows × 3 cols)
 * Index = row*3 + col
 * ========================================================================= */
typedef struct {
    uint8_t  lastRaw;           /* Raw reading last scan (0=up, 1=down)     */
    uint8_t  stableState;       /* Debounced state                          */
    uint32_t lastChangeMs;      /* Tick of last raw change                  */
    uint32_t pressMs;           /* Tick when key became stable-down         */
    uint8_t  holdFired;         /* 1 once hold threshold crossed            */
    uint32_t lastRepeatMs;      /* Tick of last auto-repeat event           */
} KeyState_t;

static KeyState_t ks[12];

/* =========================================================================
 * KEYPAD_Init
 * ========================================================================= */
void KEYPAD_Init(void)
{
    GPIO_InitTypeDef g = {0};

    /* Enable clocks (GPIOE already enabled by CubeMX; GPIOG too) */
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Rows: PE7, PE8, PE10, PE12 — output push-pull, start HIGH (inactive) */
    g.Pin   = KP_ROW1_PIN | KP_ROW2_PIN | KP_ROW3_PIN | KP_ROW4_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOE, &g);
    HAL_GPIO_WritePin(GPIOE,
        KP_ROW1_PIN | KP_ROW2_PIN | KP_ROW3_PIN | KP_ROW4_PIN,
        GPIO_PIN_SET);   /* All rows HIGH = inactive */

    /* Col 1: PE14 — input with pull-up */
    g.Pin  = KP_COL1_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOE, &g);

    /* Col 2, Col 3: PG9, PG14 — input with pull-up */
    g.Pin  = KP_COL2_PIN | KP_COL3_PIN;
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOG, &g);

    memset(ks, 0, sizeof(ks));
}

/* =========================================================================
 * KEYPAD_Scan
 *
 * Call every main-loop iteration.
 *
 * Returns:
 *   KEY_NONE         — no new event
 *   'c'              — key 'c' just pressed (stable transition 0→1)
 *   'c' | 0x80       — key 'c' hold/auto-repeat event
 * ========================================================================= */
uint8_t KEYPAD_Scan(void)
{
    uint32_t now = HAL_GetTick();

    for (uint8_t r = 0; r < 4; r++)
    {
        /* Drive this row LOW, all others remain HIGH */
        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_RESET);

        /* Small settling delay — 1 tick at 1 kHz SysTick is fine */
        uint32_t t = HAL_GetTick();
        while (HAL_GetTick() == t) { /* wait one tick */ }

        for (uint8_t c = 0; c < 3; c++)
        {
            uint8_t idx    = r * 3 + c;
            uint8_t rawNow = (HAL_GPIO_ReadPin(col_ports[c], col_pins[c])
                              == GPIO_PIN_RESET) ? 1u : 0u;

            /* Track raw change time for debounce */
            if (rawNow != ks[idx].lastRaw)
            {
                ks[idx].lastRaw      = rawNow;
                ks[idx].lastChangeMs = now;
            }

            /* Only accept state once stable for KEYPAD_DEBOUNCE_MS */
            if ((now - ks[idx].lastChangeMs) >= KEYPAD_DEBOUNCE_MS)
            {
                if (rawNow != ks[idx].stableState)
                {
                    ks[idx].stableState = rawNow;

                    if (rawNow == 1)
                    {
                        /* Fresh press event */
                        ks[idx].pressMs    = now;
                        ks[idx].holdFired  = 0;
                        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
                        return (uint8_t)key_map[r][c];
                    }
                    /* Key released — no event */
                }
                else if (rawNow == 1)
                {
                    /* Key held — check for hold/repeat */
                    if (!ks[idx].holdFired &&
                        (now - ks[idx].pressMs) >= KEYPAD_HOLD_MS)
                    {
                        ks[idx].holdFired    = 1;
                        ks[idx].lastRepeatMs = now;
                        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
                        return (uint8_t)key_map[r][c] | 0x80u;
                    }
                    if (ks[idx].holdFired &&
                        (now - ks[idx].lastRepeatMs) >= KEYPAD_REPEAT_MS)
                    {
                        ks[idx].lastRepeatMs = now;
                        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
                        return (uint8_t)key_map[r][c] | 0x80u;
                    }
                }
            }
        }

        /* Restore row HIGH before scanning next row */
        HAL_GPIO_WritePin(row_ports[r], row_pins[r], GPIO_PIN_SET);
    }

    return KEY_NONE;
}

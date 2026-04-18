/*
 * ui.c  —  Multi-screen OLED UI
 * Board : STM32 Nucleo-F767ZI  (Vehicle Base)
 *
 * Uses the afiskon stm32-ssd1306 library (ssd1306.h / ssd1306_fonts.h).
 * Display: 128 × 64 px, I2C.
 *
 * Font reference:
 *   Font_6x8  →  6 px wide,  8 px tall  (21 chars/row, 8 rows)
 *   Font_7x10 →  7 px wide, 10 px tall  (18 chars/row, 6 rows)
 *   Font_11x18→ 11 px wide, 18 px tall  (11 chars/row, 3 rows)
 *
 * Line Y positions used (Font_7x10, 12 px pitch):
 *   Title bar: Y = 0
 *   Divider:   Y = 11  (ssd1306_Line)
 *   Row 1:     Y = 13
 *   Row 2:     Y = 25
 *   Row 3:     Y = 37
 *   Row 4:     Y = 49  (Font_6x8 fits in remaining 15 px)
 */

/*
 * ui.c  —  Multi-screen OLED UI (updated for merged firmware)
 *
 * Changes from original Vehicle_Base ui.c:
 *   - fault_name() updated to match the 7 fault codes in datatypes.h
 *     (bldc_interface library), removing GATE_DRIVER_* and MCU_UNDER_VOLTAGE
 *     which are not present in that enum.
 *   - STATUS_MONITOR screen now shows Pi connection status (LIVE / TIMEOUT)
 *     based on the CMD_TIMEOUT_MS watchdog in main.c.
 *   - MOTOR_CONTROL screen now reflects joystick drive direction and shows
 *     CTRL: REMOTE when driven by Pi vs CTRL: KEYPAD when using keypad.
 */
#include "ui.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "vesc.h"
#include "screen_stepper.h"
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * Globals accessible from main.c motor-timeout logic
 * ========================================================================= */
extern uint8_t  motorRunning;
extern uint32_t lastCmdTime;

/* =========================================================================
 * Single global UI state
 * ========================================================================= */
static UI_State_t ui;

/* =========================================================================
 * Helper: set screen and request redraw
 * ========================================================================= */
static void ui_set_screen(UI_Screen_t s)
{
    ui.currentScreen = s;
    ui.needsRedraw   = 1;
}

/* =========================================================================
 * Helper: draw horizontal title bar + divider line
 * ========================================================================= */
static void ui_title(const char *title)
{
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString((char *)title, Font_7x10, White);
    ssd1306_Line(0, 11, 127, 11, White);
}

/* =========================================================================
 * Helper: draw speed bar at bottom of motor screen
 *   Outline: x=18..98, y=51..59
 *   Fill: proportional to speed (0-255)
 * ========================================================================= */
static void ui_speed_bar(uint8_t speed)
{
    char buf[10];
    /* Label */
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString("S:", Font_6x8, White);
    /* Outline rectangle */
    ssd1306_DrawRectangle(18, 50, 88, 59, White);
    /* Fill */
    uint8_t fill = (uint8_t)(18u + ((uint16_t)speed * 69u) / 255u);
    if (fill > 18) {
        ssd1306_FillRectangle(19, 51, fill, 58, White);
    }
    /* Numeric value */
    snprintf(buf, sizeof(buf), " %3d", speed);
    ssd1306_SetCursor(90, 50);
    ssd1306_WriteString(buf, Font_6x8, White);
}

/* =========================================================================
 * Helper: short fault name (3 chars + null)
 *
 * Only the 7 fault codes defined in datatypes.h (bldc_interface library)
 * are handled here.  The extended fault codes from the original vesc.h
 * (GATE_DRIVER_*, MCU_UNDER_VOLTAGE) do not exist in this enum.
 * ========================================================================= */
static const char *fault_name(mc_fault_code fc)
{
    switch (fc) {
        case FAULT_CODE_NONE:             return "OK ";   /* No fault               */
        case FAULT_CODE_OVER_VOLTAGE:     return "OV ";   /* Input over-voltage     */
        case FAULT_CODE_UNDER_VOLTAGE:    return "UV ";   /* Input under-voltage    */
        case FAULT_CODE_DRV:              return "DRV";   /* DRV8302 gate driver    */
        case FAULT_CODE_ABS_OVER_CURRENT: return "OC ";   /* Absolute over-current  */
        case FAULT_CODE_OVER_TEMP_FET:    return "OTF";   /* FET over-temperature   */
        case FAULT_CODE_OVER_TEMP_MOTOR:  return "OTM";   /* Motor over-temperature */
        default:                          return "ERR";   /* Unknown fault code     */
    }
}

/* =========================================================================
 * Helper: log a new fault (only if non-NONE and different from last)
 * ========================================================================= */
static void ui_log_fault(mc_fault_code fc)
{
    if (fc == FAULT_CODE_NONE) return;
    /* Check if same as last logged */
    if (ui.faultLogCount > 0) {
        uint8_t last = (ui.faultLogIdx + 3u) % 4u;
        if (ui.faultLog[last] == fc) return;
    }
    ui.faultLog[ui.faultLogIdx] = fc;
    ui.faultLogIdx = (ui.faultLogIdx + 1u) % 4u;
    if (ui.faultLogCount < 4) ui.faultLogCount++;
    /* Trigger 3-pulse screen flash */
    ui.faultFlashCount = 6;   /* 3 inverts × 2 (invert + restore) */
}

/* =========================================================================
 * Helper: direction label string
 * ========================================================================= */
static const char *dir_label(UI_MotorDir_t d)
{
    switch (d) {
        case MCTRL_FORWARD:  return "FWD";
        case MCTRL_BACKWARD: return "BCK";
        case MCTRL_LEFT:     return "LFT";
        case MCTRL_RIGHT:    return "RGT";
        default:             return "STP";
    }
}

/* =========================================================================
 * SCREEN RENDERERS
 * ========================================================================= */

/* ---- Splash ------------------------------------------------------------ */
static void ui_draw_splash(void)
{
    ssd1306_Fill(Black);

    /* Large title */
    ssd1306_SetCursor(12, 4);
    ssd1306_WriteString("SOLAR ROBOT", Font_11x18, White);

    /* Subtitle */
    ssd1306_SetCursor(18, 26);
    ssd1306_WriteString("STM32 F767ZI", Font_7x10, White);

    /* Loading label */
    ssd1306_SetCursor(28, 40);
    ssd1306_WriteString("Loading...", Font_6x8, White);

    /* Progress bar outline */
    ssd1306_DrawRectangle(0, 52, 127, 62, White);

    /* Fill based on elapsed time */
    uint32_t elapsed = HAL_GetTick() - ui.splashStartMs;
    if (elapsed > 1500) elapsed = 1500;
    uint8_t fillX = (uint8_t)(1u + (elapsed * 125u) / 1500u);
    if (fillX > 1) {
        ssd1306_FillRectangle(1, 53, fillX, 61, White);
    }

    ssd1306_UpdateScreen();
}

/* ---- Main Menu ---------------------------------------------------------
 *
 * 5-item menu.  Font_7x10 rows at 10 px pitch — 5 × 10 = 50 px fits in the
 * 52 px area below the title divider (y=13..62).  Row strings are padded
 * to 17 glyphs so the highlight bar looks uniform across all entries.
 */
static void ui_draw_main_menu(void)
{
    static const char *items[5] = {
        "1. Status Monitor",
        "2. Motor Control ",
        "3. Robot Arm     ",
        "4. Stepper Motor ",
        "5. Info          "
    };
    static const uint8_t row_y[5] = { 13, 23, 33, 43, 53 };

    ssd1306_Fill(Black);
    ui_title("== MAIN MENU ==");

    for (uint8_t i = 0; i < 5; i++)
    {
        if (i == ui.menuCursor)
        {
            /* Highlight selected row: white background, black text */
            ssd1306_FillRectangle(0, row_y[i] - 1, 127, row_y[i] + 9, White);
            ssd1306_SetCursor(0, row_y[i]);
            ssd1306_WriteString(">", Font_7x10, Black);
            ssd1306_SetCursor(7, row_y[i]);
            ssd1306_WriteString((char *)items[i], Font_7x10, Black);
        }
        else
        {
            ssd1306_SetCursor(7, row_y[i]);
            ssd1306_WriteString((char *)items[i], Font_7x10, White);
        }
    }

    ssd1306_UpdateScreen();
}

/* ---- Status Monitor ────────────────────────────────────────────────────
 *
 * Layout (128×64, Font_7x10 for rows 0-3, Font_6x8 for rows 4-5):
 *
 *   Row 0  (y= 0): "STATUS   [OK]"  or  "STATUS   [ERR]" (inverted badge)
 *        divider at y=11
 *   Row 1  (y=13): "RPM: +12345  V:13.2"
 *   Row 2  (y=25): "I: 4.5A  Duty:  45%"
 *   Row 3  (y=37): "Tmp:32C  Flt: OK "
 *        divider at y=48
 *   Row 4  (y=50): "Pi:LIVE  [234ms ago]"  or  "Pi:TIMEOUT"  (Font_6x8)
 *   Row 5  (y=57): "CTRL:REMOTE"  or  "CTRL:KEYPAD"           (Font_6x8)
 *
 * Pi connection status is derived from the motor watchdog variables in
 * main.c (lastCmdTime, motorRunning).  A "LIVE" label means a joystick
 * frame arrived within the last CMD_TIMEOUT_MS (500 ms).
 * ──────────────────────────────────────────────────────────────────────── */
static void ui_draw_status_monitor(void)
{
    char buf[22];

    ssd1306_Fill(Black);

    /* ── Title + fault badge ── */
    if (ui.tele.valid && ui.tele.fault_code != FAULT_CODE_NONE)
    {
        ssd1306_SetCursor(0, 0);
        ssd1306_WriteString("STATUS", Font_7x10, White);
        /* Inverted [ERR] badge on the right side */
        ssd1306_FillRectangle(90, 0, 127, 10, White);
        ssd1306_SetCursor(91, 0);
        ssd1306_WriteString("[ERR]", Font_7x10, Black);
    }
    else
    {
        ssd1306_SetCursor(0, 0);
        ssd1306_WriteString("STATUS   [OK]", Font_7x10, White);
    }
    ssd1306_Line(0, 11, 127, 11, White);

    /* ── Waiting state — no VESC telemetry yet ── */
    if (!ui.tele.valid)
    {
        ssd1306_SetCursor(4, 24);
        ssd1306_WriteString("Waiting for VESC", Font_7x10, White);
        ssd1306_SetCursor(10, 38);
        ssd1306_WriteString("(no telemetry)", Font_7x10, White);
        /* Still show Pi status even without VESC telemetry */
    }
    else
    {
        /* ── Row 1: RPM + voltage ── */
        snprintf(buf, sizeof(buf), "RPM:%+.0f V:%.1f",
                 ui.tele.rpm, ui.tele.v_in);
        ssd1306_SetCursor(0, 13);
        ssd1306_WriteString(buf, Font_7x10, White);

        /* ── Row 2: Motor current + duty cycle ── */
        snprintf(buf, sizeof(buf), "I:%.1fA  Dty:%.0f%%",
                 ui.tele.current_motor, ui.tele.duty_pct);
        ssd1306_SetCursor(0, 25);
        ssd1306_WriteString(buf, Font_7x10, White);

        /* ── Row 3: FET temperature + fault code ── */
        snprintf(buf, sizeof(buf), "Tmp:%.0fC Flt:%s",
                 ui.tele.temp_mos, fault_name(ui.tele.fault_code));
        ssd1306_SetCursor(0, 37);
        ssd1306_WriteString(buf, Font_7x10, White);
    }

    /* ── Divider before connection status ── */
    ssd1306_Line(0, 48, 127, 48, White);

    /* ── Row 4: Pi / MQTT connection status ──
     *
     * lastCmdTime is updated by main.c every time a valid ESP8266 frame
     * arrives.  If more than 500 ms have elapsed, the watchdog has fired
     * and motors are stopped — we show "TIMEOUT" in inverted colour.
     */
    uint32_t now     = HAL_GetTick();
    uint32_t elapsed = now - lastCmdTime;

    if (motorRunning || elapsed < 500u)
    {
        /* Pi is connected and sending joystick data */
        snprintf(buf, sizeof(buf), "Pi:LIVE %4lums", (unsigned long)elapsed);
        ssd1306_SetCursor(0, 50);
        ssd1306_WriteString(buf, Font_6x8, White);
    }
    else
    {
        /* No command received recently — watchdog has fired */
        ssd1306_FillRectangle(0, 49, 127, 57, White);   /* inverted background */
        ssd1306_SetCursor(0, 50);
        ssd1306_WriteString("Pi:TIMEOUT  STOPPED", Font_6x8, Black);
    }

    /* ── Row 5: Control source indicator ── */
    ssd1306_SetCursor(0, 57);
    if (motorRunning)
    {
        /* Robot is being driven by the Raspberry Pi joystick */
        ssd1306_WriteString("CTRL:REMOTE", Font_6x8, White);
    }
    else
    {
        /* Either idle or driven locally by keypad */
        ssd1306_WriteString("CTRL:KEYPAD/IDLE", Font_6x8, White);
    }

    ssd1306_UpdateScreen();
}

/* ---- Motor Control ----------------------------------------------------- */
static void ui_draw_motor_control(void)
{
    char buf[22];

    ssd1306_Fill(Black);

    /* Title + direction badge */
    ssd1306_SetCursor(0, 0);
    ssd1306_WriteString("MOTOR CTRL", Font_7x10, White);

    /* Direction badge — inverted when active */
    if (ui.motorDir != MCTRL_IDLE)
    {
        ssd1306_FillRectangle(88, 0, 127, 10, White);
        ssd1306_SetCursor(89, 0);
        snprintf(buf, sizeof(buf), "[%s]", dir_label(ui.motorDir));
        ssd1306_WriteString(buf, Font_7x10, Black);
    }
    else
    {
        ssd1306_SetCursor(88, 0);
        ssd1306_WriteString("[STP]", Font_7x10, White);
    }
    ssd1306_Line(0, 11, 127, 11, White);

    /* Direction hints */
    ssd1306_SetCursor(24, 13);
    ssd1306_WriteString("[2] FORWARD", Font_6x8, White);

    ssd1306_SetCursor(0, 24);
    ssd1306_WriteString("[4]LEFT", Font_6x8, White);
    ssd1306_SetCursor(74, 24);
    ssd1306_WriteString("[6]RIGHT", Font_6x8, White);

    ssd1306_SetCursor(22, 35);
    ssd1306_WriteString("[8] BACKWARD", Font_6x8, White);

    /* Speed bar */
    ui_speed_bar(ui.motorSpeed);

    /* Live RPM bar on right side (extra feature) */
    if (ui.tele.valid)
    {
        float absRpm = ui.tele.rpm < 0 ? -ui.tele.rpm : ui.tele.rpm;
        uint8_t rpmH = (uint8_t)(absRpm * 30.0f / 5000.0f);
        if (rpmH > 30) rpmH = 30;

        /* Commanded bar */
        uint8_t cmdH = (uint8_t)((uint16_t)ui.motorSpeed * 30u / 255u);
        ssd1306_DrawRectangle(120, 12, 127, 43, White);
        if (cmdH > 0)
            ssd1306_FillRectangle(121, 43 - cmdH, 126, 43, White);

        /* RPM overlay (inverted pixels inside bar = contrasting shade) */
        if (rpmH > 0)
            ssd1306_InvertRectangle(121, 43 - rpmH, 126, 43);
    }

    ssd1306_UpdateScreen();
}

/* ---- Robot Arm --------------------------------------------------------- */
static void ui_draw_robot_arm(void)
{
    ssd1306_Fill(Black);
    ui_title("--- ROBOT ARM ---");

    ssd1306_SetCursor(14, 22);
    ssd1306_WriteString("Not Implemented", Font_7x10, White);

    ssd1306_SetCursor(22, 36);
    ssd1306_WriteString("Coming Soon...", Font_7x10, White);

    ssd1306_SetCursor(28, 52);
    ssd1306_WriteString("[*] Back", Font_6x8, White);

    ssd1306_UpdateScreen();
}

/* ---- Info -------------------------------------------------------------- */
static void ui_draw_info(void)
{
    char buf[22];
    ssd1306_Fill(Black);
    ui_title("--- INFO ---");

    /* Uptime */
    uint32_t secs  = (HAL_GetTick() - ui.bootTick) / 1000u;
    uint32_t hh    = secs / 3600u;
    uint32_t mm    = (secs % 3600u) / 60u;
    uint32_t ss    = secs % 60u;
    snprintf(buf, sizeof(buf), "Up: %02lu:%02lu:%02lu", hh, mm, ss);
    ssd1306_SetCursor(0, 13);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* Firmware label */
    ssd1306_SetCursor(0, 25);
    ssd1306_WriteString("FW: v1.0 F767ZI", Font_7x10, White);

    /* Fault count */
    snprintf(buf, sizeof(buf), "Faults: %d", ui.faultLogCount);
    ssd1306_SetCursor(0, 37);
    ssd1306_WriteString(buf, Font_7x10, White);

    /* Controls */
    ssd1306_SetCursor(0, 50);
    ssd1306_WriteString("[*]Back [#]ClrFlt", Font_6x8, White);

    ssd1306_UpdateScreen();
}

/* =========================================================================
 * KEY HANDLERS  (one per screen)
 * ========================================================================= */

static void ui_handle_splash(uint8_t key)
{
    /* Any key skips the splash */
    if (key != KEY_NONE) {
        ui_set_screen(SCREEN_MAIN_MENU);
    }
}

static void ui_handle_main_menu(uint8_t key)
{
    uint8_t k = key & 0x7Fu;   /* Strip hold bit — menu ignores holds */

    switch (k)
    {
        case '2':
            if (ui.menuCursor > 0) { ui.menuCursor--; ui.needsRedraw = 1; }
            break;
        case '8':
            if (ui.menuCursor < 4) { ui.menuCursor++; ui.needsRedraw = 1; }
            break;
        case '5':
        case '#':
            /* Enter highlighted item.  Menu cursor 0..4 maps 1:1 to the
             * enum range SCREEN_STATUS_MONITOR..SCREEN_INFO. */
            ui_set_screen((UI_Screen_t)(SCREEN_STATUS_MONITOR + ui.menuCursor));
            break;
        case '1': ui_set_screen(SCREEN_STATUS_MONITOR); break;
        case '3': ui_set_screen(SCREEN_MOTOR_CONTROL);  break;
        case '9': ui_set_screen(SCREEN_STEPPER);        break;
        case '7': ui_set_screen(SCREEN_INFO);            break;
        default: break;
    }
}

static void ui_handle_status_monitor(uint8_t key)
{
    uint8_t k = key & 0x7Fu;
    if (k == '*') {
        ui_set_screen(SCREEN_MAIN_MENU);
    } else if (k == '#') {
        /* Force telemetry refresh display */
        ui.needsRedraw = 1;
    }
}

static void ui_handle_motor_control(uint8_t key)
{
    if (key == KEY_NONE) return;

    uint8_t k = key & 0x7Fu;

    switch (k)
    {
        case '2':
            VESC_TankDrive(DIR_FORWARD, ui.motorSpeed);
            ui.motorDir  = MCTRL_FORWARD;
            motorRunning = 1;
            lastCmdTime  = HAL_GetTick();
            ui.needsRedraw = 1;
            break;
        case '8':
            VESC_TankDrive(DIR_BACKWARD, ui.motorSpeed);
            ui.motorDir  = MCTRL_BACKWARD;
            motorRunning = 1;
            lastCmdTime  = HAL_GetTick();
            ui.needsRedraw = 1;
            break;
        case '4':
            VESC_TankDrive(DIR_LEFT, ui.motorSpeed);
            ui.motorDir  = MCTRL_LEFT;
            motorRunning = 1;
            lastCmdTime  = HAL_GetTick();
            ui.needsRedraw = 1;
            break;
        case '6':
            VESC_TankDrive(DIR_RIGHT, ui.motorSpeed);
            ui.motorDir  = MCTRL_RIGHT;
            motorRunning = 1;
            lastCmdTime  = HAL_GetTick();
            ui.needsRedraw = 1;
            break;
        case '5':
        case '0':
            VESC_Stop();
            ui.motorDir  = MCTRL_IDLE;
            motorRunning = 0;
            ui.needsRedraw = 1;
            break;
        case '1':
            if (ui.motorSpeed >= 10) ui.motorSpeed -= 10;
            ui.needsRedraw = 1;
            break;
        case '3':
            if (ui.motorSpeed <= 245) ui.motorSpeed += 10;
            ui.needsRedraw = 1;
            break;
        case '7':
            ui.motorSpeed  = 60;
            ui.needsRedraw = 1;
            break;
        case '9':
            ui.motorSpeed  = 220;
            ui.needsRedraw = 1;
            break;
        case '*':
            VESC_Stop();
            ui.motorDir  = MCTRL_IDLE;
            motorRunning = 0;
            ui_set_screen(SCREEN_MAIN_MENU);
            break;
        default:
            break;
    }
}

static void ui_handle_robot_arm(uint8_t key)
{
    if ((key & 0x7Fu) == '*') {
        ui_set_screen(SCREEN_MAIN_MENU);
    }
}

/* Stepper screen — delegate to screen_stepper module, then translate its
 * action enum into a ui_set_screen() call.  This keeps screen_stepper.c
 * independent of UI_Screen_t. */
static void ui_handle_stepper(uint8_t key)
{
    ScreenStepper_Action_t act = ScreenStepper_HandleKey(key);
    if (act == SCREEN_STEPPER_ACTION_BACK) {
        ui_set_screen(SCREEN_MAIN_MENU);
    }
}

static void ui_handle_info(uint8_t key)
{
    uint8_t k = key & 0x7Fu;
    if (k == '*') {
        ui_set_screen(SCREEN_MAIN_MENU);
    } else if (k == '#') {
        /* Clear fault log */
        memset(ui.faultLog, 0, sizeof(ui.faultLog));
        ui.faultLogIdx   = 0;
        ui.faultLogCount = 0;
        ui.needsRedraw   = 1;
    }
    /* Uptime always changes — redraw every second */
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void UI_Init(void)
{
    memset(&ui, 0, sizeof(ui));
    ui.motorSpeed    = 150;
    ui.bootTick      = HAL_GetTick();
    ui.splashStartMs = HAL_GetTick();
    ui.currentScreen = SCREEN_SPLASH;
    ui.needsRedraw   = 1;

    /* Render first frame immediately so display is never blank */
    ssd1306_Fill(Black);
    ui_draw_splash();
}

void UI_Update(uint8_t key)
{
    uint32_t now = HAL_GetTick();

    /* ---- Fault flash (highest priority — overrides everything) --------- */
    if (ui.faultFlashCount > 0 &&
        (now - ui.lastFlashMs) >= 200u)
    {
        ui.lastFlashMs = now;
        ui.faultFlashCount--;
        ssd1306_InvertRectangle(0, 0, 127, 63);
        ssd1306_UpdateScreen();
        return;
    }

    /* ---- Splash auto-advance ------------------------------------------ */
    if (ui.currentScreen == SCREEN_SPLASH)
    {
        /* Animate progress bar every ~50 ms while on splash */
        if ((now - ui.splashStartMs) < 1500u) {
            ui_draw_splash();   /* redraws with updated fill each call */
        } else {
            ui_set_screen(SCREEN_MAIN_MENU);
        }
        ui_handle_splash(key);
        return;
    }

    /* ---- Process key input --------------------------------------------- */
    if (key != KEY_NONE)
    {
        switch (ui.currentScreen)
        {
            case SCREEN_MAIN_MENU:      ui_handle_main_menu(key);      break;
            case SCREEN_STATUS_MONITOR: ui_handle_status_monitor(key); break;
            case SCREEN_MOTOR_CONTROL:  ui_handle_motor_control(key);  break;
            case SCREEN_ROBOT_ARM:      ui_handle_robot_arm(key);      break;
            case SCREEN_STEPPER:        ui_handle_stepper(key);        break;
            case SCREEN_INFO:           ui_handle_info(key);           break;
            default: break;
        }
    }

    /* ---- Auto-refresh for live screens --------------------------------- */
    if (ui.currentScreen == SCREEN_STATUS_MONITOR ||
        ui.currentScreen == SCREEN_INFO           ||
        ui.currentScreen == SCREEN_STEPPER)
    {
        if ((now - ui.lastStatusRedrawMs) >= 200u) {
            ui.lastStatusRedrawMs = now;
            ui.needsRedraw = 1;
        }
    }

    /* ---- Motor control: sync idle state when timeout fires ------------- */
    if (ui.currentScreen == SCREEN_MOTOR_CONTROL &&
        ui.motorDir != MCTRL_IDLE &&
        motorRunning == 0)
    {
        ui.motorDir    = MCTRL_IDLE;
        ui.needsRedraw = 1;
    }

    /* ---- Render if dirty ---------------------------------------------- */
    if (!ui.needsRedraw) return;
    ui.needsRedraw = 0;

    switch (ui.currentScreen)
    {
        case SCREEN_MAIN_MENU:      ui_draw_main_menu();      break;
        case SCREEN_STATUS_MONITOR: ui_draw_status_monitor(); break;
        case SCREEN_MOTOR_CONTROL:  ui_draw_motor_control();  break;
        case SCREEN_ROBOT_ARM:      ui_draw_robot_arm();      break;
        case SCREEN_STEPPER:        ScreenStepper_Draw();     break;
        case SCREEN_INFO:           ui_draw_info();           break;
        default: break;
    }
}

void UI_TelemetryUpdate(mc_values *val)
{
    /* Called from VESC RX callback — keep fast, no I2C calls here */
    ui.tele.rpm           = val->rpm;
    ui.tele.v_in          = val->v_in;
    ui.tele.current_motor = val->current_motor;
    ui.tele.duty_pct      = val->duty_now * 100.0f;
    ui.tele.temp_mos      = val->temp_mos;
    ui.tele.fault_code    = val->fault_code;
    ui.tele.valid         = 1;

    /* Log any new fault */
    ui_log_fault(val->fault_code);
}

void UI_ForceRedraw(void)
{
    ui.needsRedraw = 1;
}

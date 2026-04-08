import time
import json
import math
import shlex
import spidev
import pygame
import subprocess
import urllib.request
import urllib.parse
import paho.mqtt.client as mqtt
import RPi.GPIO as GPIO

try:
    import serial
except Exception:
    serial = None

# =========================================================
# CONFIG
# =========================================================

# NetworkManager saved connection profile names
ESP32_PROFILE = "ESP32_AP"
HOME_PROFILE = "Alex iPhone 11 USA"

# Labels shown in UI
ESP32_LABEL = "ESP32 AP"
HOME_LABEL = "Local WiFi"

# OpenWeather
OPENWEATHER_API_KEY = "709ed4b743b924654643818103c63551"   # replace if needed
OPENWEATHER_UNITS = "imperial"
WEATHER_REFRESH_S = 300

# UPS battery UART
BATTERY_UART_PORT = "/dev/serial0"
BATTERY_UART_BAUD = 9600
BATTERY_REFRESH_S = 0.25

# MQTT
BROKER_HOST = "127.0.0.1"
TOPIC = "servos/angles"
VEHICLE_TOPIC = "vehicle/base"

# Loop rates
LOOP_HZ = 80
PUB_HZ = 40

# Servo angle limits
ANGLE_MIN = 10.0
ANGLE_MAX = 170.0

# Tele-op joystick rate mapping
CENTER_X = 680
CENTER_Y = 528

MIN_X = 450                     # below this, X cuts weirdly; clamp here
MAX_X_OFFSET = CENTER_X - MIN_X
MAX_Y_OFFSET = max(CENTER_Y, 1023 - CENTER_Y)

DEAD_X_COUNTS = 20
DEAD_Y_COUNTS = 20

MAX_DPS_X = 60.0
MAX_DPS_Y = 60.0

# Navigation joystick thresholds
NAV_DEAD_X = 120
NAV_DEAD_Y = 120
NAV_REPEAT_S = 0.22

# MCP3008
SPI_BUS = 0
SPI_DEV = 0
SPI_SPEED = 1_000_000

# GPIO buttons (active low, pull-up)
CENTER_BUTTON_PIN = 24   # select
BACK_BUTTON_PIN = 23     # back

# =========================================================
# GPIO
# =========================================================

GPIO.setmode(GPIO.BCM)
GPIO.setup(CENTER_BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
GPIO.setup(BACK_BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)

# =========================================================
# SPI / MCP3008
# =========================================================

spi = spidev.SpiDev()
spi.open(SPI_BUS, SPI_DEV)
spi.max_speed_hz = SPI_SPEED
spi.mode = 0b00

def read_adc(ch: int) -> int:
    r = spi.xfer2([1, (8 + ch) << 4, 0])
    return ((r[1] & 3) << 8) | r[2]

# =========================================================
# TELEOP JOYSTICK RATE MAPPING
# =========================================================

def raw_to_vel_x(raw: int) -> float:
    # clamp left side because hardware cuts off below ~450
    if raw < MIN_X:
        raw = MIN_X

    offset = raw - CENTER_X
    if abs(offset) < DEAD_X_COUNTS:
        return 0.0

    if offset > 0:
        if offset > MAX_X_OFFSET:
            offset = MAX_X_OFFSET
        norm = offset / MAX_X_OFFSET
    else:
        if offset < -MAX_X_OFFSET:
            offset = -MAX_X_OFFSET
        norm = offset / MAX_X_OFFSET

    return norm * MAX_DPS_X

def raw_to_vel_y(raw: int) -> float:
    offset = raw - CENTER_Y
    if abs(offset) < DEAD_Y_COUNTS:
        return 0.0

    if offset > MAX_Y_OFFSET:
        offset = MAX_Y_OFFSET
    elif offset < -MAX_Y_OFFSET:
        offset = -MAX_Y_OFFSET

    norm = offset / MAX_Y_OFFSET
    return norm * MAX_DPS_Y

def clamp_angle(a: float) -> float:
    if a < ANGLE_MIN:
        return ANGLE_MIN
    if a > ANGLE_MAX:
        return ANGLE_MAX
    return a


def clamp01(v: float) -> float:
    if v < 0.0:
        return 0.0
    if v > 1.0:
        return 1.0
    return v

def raw_to_vehicle_norm_x(raw: int) -> float:
    # match the existing mqtttrans structure: publish x in [0, 1]
    if raw < MIN_X:
        raw = MIN_X
    if raw > 1023:
        raw = 1023
    span = 1023 - MIN_X
    return clamp01((raw - MIN_X) / span)

def raw_to_vehicle_norm_y(raw: int) -> float:
    # full ADC range to [0, 1] so the ESP code can stay unchanged
    if raw < 0:
        raw = 0
    if raw > 1023:
        raw = 1023
    return clamp01(raw / 1023.0)

# =========================================================
# NAVIGATION JOYSTICK
# =========================================================

def read_nav_direction():
    """
    Uses joystick to create discrete UI navigation.
    Returns: "up", "down", "left", "right", or None
    """
    x_raw = read_adc(0)
    y_raw = read_adc(1)

    dx = x_raw - CENTER_X
    dy = y_raw - CENTER_Y

    if abs(dx) < NAV_DEAD_X and abs(dy) < NAV_DEAD_Y:
        return None

    # choose dominant axis
    if abs(dx) > abs(dy):
        return "up" if dx > 0 else "down"
    else:
        # typical joystick wiring often has lower Y when pushed up;
        # if yours feels inverted, swap these two lines
        return "right" if dy > 0 else "left"

# =========================================================
# MQTT
# =========================================================

mqtt_client = mqtt.Client(client_id="pi-hmi", protocol=mqtt.MQTTv311)

def mqtt_start():
    mqtt_client.connect(BROKER_HOST, 1883, 60)
    mqtt_client.loop_start()

def mqtt_stop():
    try:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    except Exception:
        pass

# =========================================================
# WIFI
# =========================================================

def run_cmd(cmd: str, timeout=30):
    p = subprocess.run(
        shlex.split(cmd),
        capture_output=True,
        text=True,
        timeout=timeout
    )
    return p.returncode, p.stdout.strip(), p.stderr.strip()

def wifi_disconnect_all():
    run_cmd("nmcli radio wifi on", timeout=10)
    rc, out, _ = run_cmd("nmcli -t -f DEVICE,TYPE dev status", timeout=10)
    if rc == 0 and out:
        for line in out.splitlines():
            parts = line.split(":")
            if len(parts) >= 2:
                dev, typ = parts[0], parts[1]
                if typ == "wifi":
                    run_cmd(f"nmcli dev disconnect {dev}", timeout=10)

def wifi_current_ssid():
    rc, out, _ = run_cmd("nmcli -t -f ACTIVE,SSID dev wifi", timeout=10)
    if rc != 0 or not out:
        return ""
    for line in out.splitlines():
        parts = line.split(":", 1)
        if len(parts) == 2 and parts[0] == "yes":
            return parts[1]
    return ""

def wifi_connect_profile(profile_name: str):
    wifi_disconnect_all()
    run_cmd("nmcli radio wifi on", timeout=10)
    rc, out, err = run_cmd(f'nmcli connection up "{profile_name}"', timeout=35)
    if rc == 0:
        return True, out if out else f"Connected via profile: {profile_name}"
    return False, err if err else out if out else f"Failed to connect: {profile_name}"

# =========================================================
# WEATHER
# =========================================================

def get_ip_based_location():
    try:
        with urllib.request.urlopen("http://ip-api.com/json", timeout=5) as r:
            data = json.loads(r.read().decode("utf-8"))
            if data.get("status") != "success":
                return None, None
            return data.get("city"), data.get("zip")
    except Exception:
        return None, None

def fetch_openweather(city: str = None, zip_code: str = None):
    if not OPENWEATHER_API_KEY:
        raise RuntimeError("OpenWeather API key not set.")

    params = {"appid": OPENWEATHER_API_KEY, "units": OPENWEATHER_UNITS}
    if zip_code:
        params["zip"] = f"{zip_code},US"
    elif city:
        params["q"] = f"{city},US"
    else:
        params["q"] = "Phoenix,US"

    url = "https://api.openweathermap.org/data/2.5/weather?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=6) as r:
        data = json.loads(r.read().decode("utf-8"))

    main = data.get("main", {})
    wind = data.get("wind", {})
    weather_arr = data.get("weather", [{}])
    desc = weather_arr[0].get("description", "N/A")

    return {
        "location": data.get("name", "N/A"),
        "description": desc.title() if isinstance(desc, str) else "N/A",
        "temp": main.get("temp", "N/A"),
        "humidity": main.get("humidity", "N/A"),
        "wind": wind.get("speed", "N/A"),
    }

# =========================================================
# PYGAME UI
# =========================================================

pygame.init()
pygame.display.set_caption("Solarbot HMI")
screen = pygame.display.set_mode((0, 0), pygame.FULLSCREEN)
screen_rect = screen.get_rect()
clock = pygame.time.Clock()

# Colors
BG = (15, 15, 30)
TXT = (235, 235, 235)
PANEL = (30, 30, 50)
BTN_IDLE = (80, 80, 120)
BTN_GO = (50, 160, 80)
BTN_STOP = (165, 70, 70)
BTN_DISABLED = (70, 70, 70)
BORDER = (220, 220, 220)
SELECT_BORDER = (255, 220, 80)

# Fonts
font_title = pygame.font.SysFont("Arial", 52)
font_btn = pygame.font.SysFont("Arial", 28)
font_small = pygame.font.SysFont("Arial", 22)
font_mono = pygame.font.SysFont("Arial", 22)

# =========================================================
# BATTERY INDICATOR
# =========================================================

battery_percent = None

def init_battery_uart():
    if serial is None:
        return None
    try:
        return serial.Serial(BATTERY_UART_PORT, BATTERY_UART_BAUD, timeout=0)
    except Exception:
        return None

def parse_battery_percent(line: str):
    parts = line.replace(":", " ").split()
    for i, part in enumerate(parts):
        if part.upper() == "BATCAP" and i + 1 < len(parts):
            try:
                value = int(float(parts[i + 1]))
                return max(0, min(100, value))
            except Exception:
                return None
    return None

def poll_battery_percent(battery_uart, previous_percent=None):
    if battery_uart is None:
        return previous_percent

    latest = previous_percent
    try:
        waiting = getattr(battery_uart, "in_waiting", 0)
        if waiting <= 0:
            return latest

        raw = battery_uart.read(waiting).decode("utf-8", errors="ignore")
        for line in raw.splitlines():
            parsed = parse_battery_percent(line)
            if parsed is not None:
                latest = parsed
    except Exception:
        return previous_percent

    return latest

def draw_battery_indicator(percent):
    x = 40
    y = 6
    body_w = 26
    body_h = 14
    nub_w = 3
    nub_h = 6

    outline = pygame.Rect(x, y, body_w, body_h)
    nub = pygame.Rect(x + body_w, y + (body_h - nub_h) // 2, nub_w, nub_h)

    pygame.draw.rect(screen, TXT, outline, 2, border_radius=3)
    pygame.draw.rect(screen, TXT, nub, border_radius=1)

    if percent is None:
        level = 0
        label = "--%"
    else:
        level = max(0, min(100, int(percent)))
        label = f"{level}%"

    inner_margin = 2
    inner_w = body_w - inner_margin * 2
    fill_w = int(inner_w * (level / 100.0))
    if fill_w > 0:
        fill = pygame.Rect(x + inner_margin, y + inner_margin, fill_w, body_h - inner_margin * 2)
        pygame.draw.rect(screen, TXT, fill, border_radius=2)

    text_surf = font_small.render(label, True, TXT)
    screen.blit(text_surf, (x + body_w + nub_w + 8, y - 3))

# =========================================================
# PAGE STATE
# =========================================================

PAGE_MAIN = "main"
PAGE_SETTINGS = "settings"
PAGE_WIFI_SELECT = "wifi_select"

PAGE_CONTROLS_CONNECT = "controls_connect"
PAGE_CONTROLS_MAIN = "controls_main"
PAGE_TELEOP_INIT = "teleop_init"
PAGE_TELEOP_MENU = "teleop_menu"
PAGE_ARM_CONTROL = "arm_control"
PAGE_VEHICLE_CONTROL = "vehicle_control"

PAGE_INFO_CONNECT = "info_connect"
PAGE_INFO_MAIN = "info_main"
PAGE_SITE_INFO = "site_info"
PAGE_WEATHER = "weather"

page = PAGE_MAIN
page_stack = []

# =========================================================
# BUTTON LAYOUTS
# =========================================================

def centered_button(y, w=340, h=100):
    r = pygame.Rect(0, 0, w, h)
    r.center = (screen_rect.centerx, y)
    return r

def left_right_buttons(y, left_x_frac=0.33, right_x_frac=0.67, w=300, h=100):
    a = pygame.Rect(0, 0, w, h)
    b = pygame.Rect(0, 0, w, h)
    a.center = (int(screen_rect.width * left_x_frac), y)
    b.center = (int(screen_rect.width * right_x_frac), y)
    return a, b

main_btn_controls = centered_button(screen_rect.centery - 120, 280, 70)
main_btn_info = centered_button(screen_rect.centery, 280, 70)
main_btn_settings = centered_button(screen_rect.centery + 120, 280, 70)

settings_btn_wifi = centered_button(screen_rect.centery - 140, 360, 110)
settings_btn_dev = centered_button(screen_rect.centery + 140, 360, 110)

wifi_btn_local, wifi_btn_esp = left_right_buttons(screen_rect.centery, 0.33, 0.67, 320, 110)

controls_main_btn_teleop = centered_button(screen_rect.centery, 360, 110)

teleop_menu_btn_vehicle, teleop_menu_btn_arm = left_right_buttons(screen_rect.centery + 80, 0.33, 0.67, 320, 110)
teleop_menu_btn_exit = centered_button(screen_rect.bottom - 90, 260, 80)

arm_btn_enable = centered_button(screen_rect.centery - 90, 380, 100)
arm_btn_disable = centered_button(screen_rect.centery + 30, 380, 100)
arm_btn_exit = centered_button(screen_rect.bottom - 90, 260, 80)

vehicle_btn_back = centered_button(screen_rect.bottom - 90, 260, 80)

info_main_btn_site, info_main_btn_weather = left_right_buttons(screen_rect.centery + 80, 0.33, 0.67, 320, 110)

detail_btn_back = centered_button(screen_rect.bottom - 90, 260, 80)

# =========================================================
# UI HELPERS
# =========================================================

class UIButton:
    def __init__(self, rect, label, action, color=BTN_IDLE):
        self.rect = rect
        self.label = label
        self.action = action
        self.color = color

def draw_button(btn: UIButton, selected=False):
    pygame.draw.rect(screen, btn.color, btn.rect, border_radius=18)
    border_color = SELECT_BORDER if selected else BORDER
    border_width = 6 if selected else 3
    pygame.draw.rect(screen, border_color, btn.rect, border_width, border_radius=18)

    surf = font_btn.render(btn.label, True, TXT)
    screen.blit(surf, surf.get_rect(center=btn.rect.center))

def draw_text_lines(lines, x, y, line_h=30, mono=False):
    f = font_mono if mono else font_small
    for i, line in enumerate(lines):
        surf = f.render(line, True, TXT)
        screen.blit(surf, (x, y + i * line_h))

def draw_topbar(title):
    screen.fill(BG)
    draw_battery_indicator(battery_percent)
    t = font_title.render(title, True, TXT)
    screen.blit(t, (40, 25))

    ssid = wifi_current_ssid() or "Disconnected"
    s = font_small.render(f"Wi-Fi: {ssid}", True, TXT)
    screen.blit(s, (40, 90))

    now_str = time.strftime("%Y-%m-%d  %H:%M:%S")
    ts = font_small.render(now_str, True, TXT)
    screen.blit(ts, (screen_rect.right - 300, 35))

    hint = font_small.render("Center = Select | Back = Previous | ESC = Exit", True, TXT)
    screen.blit(hint, (40, screen_rect.bottom - 35))

def draw_loading(title, subtitle):
    screen.fill(BG)
    draw_battery_indicator(battery_percent)
    t = font_title.render(title, True, TXT)
    screen.blit(t, t.get_rect(center=(screen_rect.centerx, screen_rect.centery - 40)))
    s = font_small.render(subtitle, True, TXT)
    screen.blit(s, s.get_rect(center=(screen_rect.centerx, screen_rect.centery + 20)))
    now_str = time.strftime("%Y-%m-%d  %H:%M:%S")
    ts = font_small.render(now_str, True, TXT)
    screen.blit(ts, (screen_rect.right - 300, 35))
    pygame.display.flip()

def move_selection(buttons, selected_idx, direction):
    if not buttons:
        return selected_idx

    current = buttons[selected_idx].rect.center
    cx, cy = current

    candidates = []
    for i, b in enumerate(buttons):
        if i == selected_idx:
            continue
        x, y = b.rect.center
        dx = x - cx
        dy = y - cy

        valid = False
        primary = 0
        secondary = 0

        if direction == "up" and dy < 0:
            valid = True
            primary = abs(dy)
            secondary = abs(dx)
        elif direction == "down" and dy > 0:
            valid = True
            primary = abs(dy)
            secondary = abs(dx)
        elif direction == "left" and dx < 0:
            valid = True
            primary = abs(dx)
            secondary = abs(dy)
        elif direction == "right" and dx > 0:
            valid = True
            primary = abs(dx)
            secondary = abs(dy)

        if valid:
            dist = math.hypot(dx, dy)
            candidates.append((secondary, primary, dist, i))

    if candidates:
        candidates.sort()
        return candidates[0][3]

    return selected_idx

# =========================================================
# PAGE NAVIGATION
# =========================================================

selected_idx = 0

def set_page(new_page, push_current=True):
    global page, selected_idx, page_stack
    if push_current and page != new_page:
        page_stack.append(page)
    page = new_page
    selected_idx = 0

def go_back():
    global page, selected_idx, page_stack
    if page_stack:
        page = page_stack.pop()
        selected_idx = 0
    else:
        page = PAGE_MAIN
        selected_idx = 0

def current_buttons():
    if page == PAGE_MAIN:
        return [
            UIButton(main_btn_controls, "Controls", "goto_controls_connect", BTN_GO),
            UIButton(main_btn_info, "Information", "goto_info_connect", BTN_IDLE),
            UIButton(main_btn_settings, "Settings", "goto_settings", BTN_IDLE),
        ]

    if page == PAGE_SETTINGS:
        return [
            UIButton(settings_btn_wifi, "WiFi Select", "goto_wifi_select", BTN_IDLE),
            UIButton(settings_btn_dev, "Developer Mode", "exit_program", BTN_STOP),
        ]

    if page == PAGE_WIFI_SELECT:
        return [
            UIButton(wifi_btn_local, "Connect Local WiFi", "wifi_local", BTN_IDLE),
            UIButton(wifi_btn_esp, "Connect ESP32 AP", "wifi_esp32", BTN_IDLE),
        ]

    if page == PAGE_CONTROLS_MAIN:
        return [
            UIButton(controls_main_btn_teleop, "TeleOp", "goto_teleop_init", BTN_GO),
        ]

    if page == PAGE_TELEOP_MENU:
        return [
            UIButton(teleop_menu_btn_vehicle, "Vehicle Base", "goto_vehicle_control", BTN_IDLE),
            UIButton(teleop_menu_btn_arm, "Arm", "goto_arm_control", BTN_GO),
            UIButton(teleop_menu_btn_exit, "Exit TeleOp", "back_to_controls_main", BTN_STOP),
        ]

    if page == PAGE_ARM_CONTROL:
        return [
            UIButton(arm_btn_enable, "Enable Arm Control", "arm_enable", BTN_GO),
            UIButton(arm_btn_disable, "Disable Arm Control", "arm_disable", BTN_STOP),
            UIButton(arm_btn_exit, "Back", "back_to_teleop_menu", BTN_IDLE),
        ]

    if page == PAGE_VEHICLE_CONTROL:
        return [
            UIButton(vehicle_btn_back, "Back", "back_to_teleop_menu", BTN_IDLE),
        ]

    if page == PAGE_INFO_MAIN:
        return [
            UIButton(info_main_btn_site, "Site Info", "goto_site_info", BTN_IDLE),
            UIButton(info_main_btn_weather, "Weather", "goto_weather", BTN_GO),
        ]

    if page in (PAGE_SITE_INFO, PAGE_WEATHER):
        return [
            UIButton(detail_btn_back, "Back", "back_to_info_main", BTN_IDLE),
        ]

    return []

# =========================================================
# CONNECTION FLOWS
# =========================================================

def connect_flow(profile_name: str, label: str, page_title: str):
    start = time.monotonic()
    attempt = 0

    while True:
        attempt += 1
        draw_loading(page_title, f"Connecting to {label} ... (attempt {attempt})")

        ok, msg = wifi_connect_profile(profile_name)
        if ok:
            return True, msg

        fail_start = time.monotonic()
        while time.monotonic() - fail_start < 1.0:
            pygame.event.pump()
            if GPIO.input(BACK_BUTTON_PIN) == GPIO.LOW:
                time.sleep(0.2)
                return False, "Cancelled"
            time.sleep(0.03)

        if time.monotonic() - start > 20:
            return False, "Timed out connecting"

# =========================================================
# MAIN
# =========================================================

def main():
    global page, selected_idx, battery_percent

    mqtt_start()

    battery_uart = init_battery_uart()
    last_battery_fetch = 0.0

    # Teleop state
    control_enabled = False
    a1 = 90.0
    a2 = 90.0
    last_sent = None
    vehicle_last_sent = None
    next_pub_time = 0.0

    # Info/weather state
    weather = {"location":"N/A","description":"N/A","temp":"N/A","humidity":"N/A","wind":"N/A"}
    weather_status = "Weather: N/A"
    last_weather_fetch = 0.0

    # Input debounce / repeat
    last_nav_time = 0.0
    prev_center = GPIO.input(CENTER_BUTTON_PIN)
    prev_back = GPIO.input(BACK_BUTTON_PIN)

    # Loop timing
    loop_dt_target = 1.0 / LOOP_HZ
    pub_dt = 1.0 / PUB_HZ
    last_time = time.monotonic()

    running = True
    while running:
        now = time.monotonic()
        dt = now - last_time
        if dt <= 0:
            dt = loop_dt_target
        last_time = now

        # ------------- Global pygame events -------------
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif event.type == pygame.KEYDOWN and event.key == pygame.K_ESCAPE:
                running = False

        # ------------- Hardware buttons -------------
        center_now = GPIO.input(CENTER_BUTTON_PIN)
        back_now = GPIO.input(BACK_BUTTON_PIN)

        center_pressed = (prev_center == GPIO.HIGH and center_now == GPIO.LOW)
        back_pressed = (prev_back == GPIO.HIGH and back_now == GPIO.LOW)

        prev_center = center_now
        prev_back = back_now

        # ------------- Joystick navigation -------------
        buttons = current_buttons()
        nav_dir = read_nav_direction()
        if nav_dir is not None and (now - last_nav_time) > NAV_REPEAT_S and buttons:
            selected_idx = move_selection(buttons, selected_idx, nav_dir)
            last_nav_time = now

        # ------------- Back action -------------
        if back_pressed:
            if page == PAGE_MAIN:
                pass
            elif page == PAGE_CONTROLS_MAIN:
                wifi_disconnect_all()
                go_back()
            elif page == PAGE_INFO_MAIN:
                wifi_disconnect_all()
                go_back()
            elif page == PAGE_ARM_CONTROL:
                control_enabled = False
                set_page(PAGE_TELEOP_MENU, push_current=False)
            elif page == PAGE_VEHICLE_CONTROL:
                vehicle_last_sent = None
                set_page(PAGE_TELEOP_MENU, push_current=False)
            elif page == PAGE_TELEOP_MENU:
                control_enabled = False
                set_page(PAGE_CONTROLS_MAIN, push_current=False)
            elif page in (PAGE_SITE_INFO, PAGE_WEATHER):
                set_page(PAGE_INFO_MAIN, push_current=False)
            else:
                go_back()

        # ------------- Select action -------------
        if center_pressed and buttons:
            action = buttons[selected_idx].action

            if action == "goto_settings":
                set_page(PAGE_SETTINGS)

            elif action == "goto_wifi_select":
                set_page(PAGE_WIFI_SELECT)

            elif action == "exit_program":
                running = False

            elif action == "wifi_local":
                draw_loading("WiFi Select", "Connecting to Local WiFi...")
                ok, msg = wifi_connect_profile(HOME_PROFILE)
                draw_loading("WiFi Select", msg if ok else f"Failed: {msg}")
                pygame.display.flip()
                time.sleep(1.0)

            elif action == "wifi_esp32":
                draw_loading("WiFi Select", "Connecting to ESP32 AP...")
                ok, msg = wifi_connect_profile(ESP32_PROFILE)
                draw_loading("WiFi Select", msg if ok else f"Failed: {msg}")
                pygame.display.flip()
                time.sleep(1.0)

            elif action == "goto_controls_connect":
                ok, msg = connect_flow(ESP32_PROFILE, ESP32_LABEL, "Controls")
                if ok:
                    set_page(PAGE_CONTROLS_MAIN)
                else:
                    draw_loading("Controls", f"Failed: {msg}")
                    time.sleep(1.0)

            elif action == "goto_info_connect":
                ok, msg = connect_flow(HOME_PROFILE, HOME_LABEL, "Information")
                if ok:
                    set_page(PAGE_INFO_MAIN)
                    last_weather_fetch = 0.0
                    weather_status = "Weather: loading..."
                else:
                    draw_loading("Information", f"Failed: {msg}")
                    time.sleep(1.0)

            elif action == "goto_teleop_init":
                draw_loading("TeleOp", "Initializing TeleOp...")
                pygame.display.flip()
                time.sleep(0.8)
                set_page(PAGE_TELEOP_MENU)

            elif action == "goto_vehicle_control":
                vehicle_last_sent = None
                set_page(PAGE_VEHICLE_CONTROL)

            elif action == "goto_arm_control":
                set_page(PAGE_ARM_CONTROL)

            elif action == "back_to_controls_main":
                control_enabled = False
                vehicle_last_sent = None
                set_page(PAGE_CONTROLS_MAIN, push_current=False)

            elif action == "arm_enable":
                control_enabled = True

            elif action == "arm_disable":
                control_enabled = False

            elif action == "back_to_teleop_menu":
                control_enabled = False
                vehicle_last_sent = None
                set_page(PAGE_TELEOP_MENU, push_current=False)

            elif action == "goto_site_info":
                set_page(PAGE_SITE_INFO)

            elif action == "goto_weather":
                set_page(PAGE_WEATHER)

            elif action == "back_to_info_main":
                set_page(PAGE_INFO_MAIN, push_current=False)

        if now - last_battery_fetch > BATTERY_REFRESH_S:
            battery_percent = poll_battery_percent(battery_uart, battery_percent)
            last_battery_fetch = now

        # ------------- Render / page logic -------------

        if page == PAGE_MAIN:
            draw_topbar("Main Menu")
            #subtitle = font_small.render("Use joystick to move selection. Press center button to select.", True, TXT)
            #screen.blit(subtitle, subtitle.get_rect(center=(screen_rect.centerx, 160)))
            
            lines = [
                "Use joystick to move selection."
                "Press center button to select."
                ]
            draw_text_lines(lines, screen_rect.centerx - 200, 120, line_h=30)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_SETTINGS:
            draw_topbar("Settings")
            lines = [
                "Developer Mode: exits the HMI and returns to Raspberry Pi OS.",
                "WiFi Select: manually connect to Local WiFi or ESP32 AP.",
            ]
            draw_text_lines(lines, 80, 160, line_h=34)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_WIFI_SELECT:
            draw_topbar("WiFi Select")
            info_lines = [
                "Choose which saved NetworkManager profile to connect to.",
                f"Local WiFi profile: {HOME_PROFILE}",
                f"ESP32 AP profile: {ESP32_PROFILE}",
            ]
            draw_text_lines(info_lines, 80, 160, line_h=34)
            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_CONTROLS_MAIN:
            draw_topbar("Controls Main Screen")
            lines = [
                "Connection to ESP32 AP successful.",
                "Zero-G is considered enabled by default here.",
                "Select TeleOp to continue.",
            ]
            draw_text_lines(lines, 80, 160, line_h=34)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_TELEOP_MENU:
            draw_topbar("TeleOp Controls")
            lines = [
                "Select a control mode.",
                "Arm Control uses the joystick to rate-control servo angles.",
                "Vehicle Base Control publishes MQTT joystick values for the ESP.",
            ]
            draw_text_lines(lines, 80, 160, line_h=34)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_ARM_CONTROL:
            draw_topbar("Arm Control")

            # arm control stays same communication structure
            x_raw = read_adc(0)
            y_raw = read_adc(1)

            v1 = raw_to_vel_x(x_raw)
            v2 = raw_to_vel_y(y_raw)

            if control_enabled:
                a1 = clamp_angle(a1 + v1 * dt)
                a2 = clamp_angle(a2 + v2 * dt)

                if now >= next_pub_time:
                    payload = {"s1": int(round(a1)), "s2": int(round(a2))}
                    if payload != last_sent:
                        mqtt_client.publish(TOPIC, json.dumps(payload), qos=0, retain=False)
                        last_sent = payload
                    next_pub_time = now + pub_dt

            status_lines = [
                f"Status: {'CONTROLLING' if control_enabled else 'IDLE'}",
                f"Angles: s1={int(round(a1))}°, s2={int(round(a2))}°",
                f"Joystick raw: X={x_raw}, Y={y_raw}",
                f"Joystick rate: v1={v1:.1f} dps, v2={v2:.1f} dps",
                "",
                "Enable Arm Control to send MQTT angle updates.",
            ]
            draw_text_lines(status_lines, 80, 160, line_h=32)

            buttons = current_buttons()
            # dynamic colors
            for i, b in enumerate(buttons):
                if b.action == "arm_enable":
                    b.label = "Arm Control Active" if control_enabled else "Enable Arm Control"
                    b.color = BTN_GO if not control_enabled else BTN_DISABLED
                elif b.action == "arm_disable":
                    b.color = BTN_STOP if control_enabled else BTN_DISABLED
                draw_button(b, i == selected_idx)

        elif page == PAGE_VEHICLE_CONTROL:
            draw_topbar("Vehicle Base Control")

            x_raw = read_adc(0)
            y_raw = read_adc(1)

            x_norm = raw_to_vehicle_norm_x(x_raw)
            y_norm = raw_to_vehicle_norm_y(y_raw)

            if now >= next_pub_time:
                payload = {"x": round(x_norm, 4), "y": round(y_norm, 4)}
                if payload != vehicle_last_sent:
                    mqtt_client.publish(VEHICLE_TOPIC, json.dumps(payload), qos=0, retain=False)
                    vehicle_last_sent = payload
                next_pub_time = now + pub_dt

            lines = [
                "Publishing joystick values to vehicle/base.",
                f"Joystick raw: X={x_raw}, Y={y_raw}",
                f"Published: x={x_norm:.3f}, y={y_norm:.3f}",
                "ESP code can stay unchanged.",
            ]
            draw_text_lines(lines, 80, 180, line_h=34)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_INFO_MAIN:
            draw_topbar("Information Main Menu")
            lines = [
                "Connection to local host successful.",
                "Choose Site Info or Weather.",
            ]
            draw_text_lines(lines, 80, 160, line_h=34)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_SITE_INFO:
            draw_topbar("Site Info Screen")
            panel = pygame.Rect(60, 150, screen_rect.width - 120, screen_rect.height - 280)
            pygame.draw.rect(screen, PANEL, panel, border_radius=18)
            pygame.draw.rect(screen, BORDER, panel, 2, border_radius=18)

            lines = [
                "Solar Site Information",
                "",
                "Tilt Angle: N/A",
                "Output Power: N/A",
                "Power Usage: N/A",
                "Status: N/A",
            ]
            draw_text_lines(lines, panel.x + 25, panel.y + 25, line_h=38)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        elif page == PAGE_WEATHER:
            draw_topbar("Site Weather Info")

            panel = pygame.Rect(60, 150, screen_rect.width - 120, screen_rect.height - 280)
            pygame.draw.rect(screen, PANEL, panel, border_radius=18)
            pygame.draw.rect(screen, BORDER, panel, 2, border_radius=18)

            if not OPENWEATHER_API_KEY:
                weather_lines = [
                    "OpenWeather API key not set.",
                    "Set OPENWEATHER_API_KEY in this file.",
                ]
            else:
                if now - last_weather_fetch > WEATHER_REFRESH_S:
                    try:
                        city, zc = get_ip_based_location()
                        weather = fetch_openweather(city=city, zip_code=zc)
                        weather_status = "Weather: OK (updated)"
                    except Exception as e:
                        weather_status = f"Weather: {type(e).__name__}"
                    last_weather_fetch = now

                unit = "°F" if OPENWEATHER_UNITS == "imperial" else "°C"
                wind_unit = "mph" if OPENWEATHER_UNITS == "imperial" else "m/s"

                weather_lines = [
                    weather_status,
                    "",
                    f"Location: {weather.get('location', 'N/A')}",
                    f"Conditions: {weather.get('description', 'N/A')}",
                    f"Temp: {weather.get('temp', 'N/A')} {unit}",
                    f"Humidity: {weather.get('humidity', 'N/A')} %",
                    f"Wind: {weather.get('wind', 'N/A')} {wind_unit}",
                ]

            draw_text_lines(weather_lines, panel.x + 25, panel.y + 25, line_h=38)

            buttons = current_buttons()
            for i, b in enumerate(buttons):
                draw_button(b, i == selected_idx)

        pygame.display.flip()
        clock.tick(LOOP_HZ)

    # exiting
    try:
        if battery_uart is not None:
            battery_uart.close()
    except Exception:
        pass

    wifi_disconnect_all()

try:
    main()
finally:
    mqtt_stop()
    try:
        spi.close()
    except Exception:
        pass
    GPIO.cleanup()
    pygame.quit()



/*
 * esp32s3_bridge.ino  —  Solar Robot CTRL ↔ Base bridge (ESP32-S3-WROOM)
 *
 * Translates between MQTT (and BLE) on the WiFi side and the fixed 12-byte
 * UART frame defined in Vehicle_Base/Core/Inc/comm_protocol.h:
 *
 *   0x41 0x5A | sysID mode xH xL yH yL zDir yaw | 0x59 0x42
 *
 * sysID values:
 *   0x01  CTRL → BASE  (joystick drive)
 *   0x02  CTRL → ARM   (mode + joystick + EE buttons)
 *   0x03  ARM  → CTRL  (state report from arm; published back to MQTT)
 *
 * MQTT topics:
 *   IN  vehicle/base    — JSON {x,y[,zDir,yaw]}    → emit sysID=0x01 frame
 *   IN  vehicle/arm     — JSON {mode,x,y,zDir,yaw} → emit sysID=0x02 frame
 *   OUT vehicle/arm/state — published whenever sysID=0x03 frame arrives
 *
 * Pin map: Serial1 on GPIO17 (TX) → STM32 PC11 (UART4_RX)
 *          Serial1 on GPIO18 (RX) ← STM32 PC10 (UART4_TX)
 *          Common GND. 460800 baud 8N1. No level shifter (3.3 V both sides).
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>

/* ───────────── User config ───────────── */
static const char *WIFI_SSID = "YOUR_SSID";
static const char *WIFI_PASS = "YOUR_PASS";
static const char *MQTT_HOST = "192.168.1.10";
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_CLIENT_ID = "solar_bridge";

static const int   UART_TX_PIN   = 17;
static const int   UART_RX_PIN   = 18;
static const long  UART_BAUD     = 460800;

static const char *AP_FALLBACK_SSID = "SolarBot_AP";
static const uint32_t WIFI_RETRY_MS = 10000;

/* ───────────── Frame format (mirror comm_protocol.h) ───────────── */
static const uint8_t COMM_FRAME_LEN   = 12;
static const uint8_t COMM_PAYLOAD_LEN = 8;
static const uint8_t COMM_START_1     = 0x41;
static const uint8_t COMM_START_2     = 0x5A;
static const uint8_t COMM_END_1       = 0x59;
static const uint8_t COMM_END_2       = 0x42;

static const uint8_t SYS_CTRL_TO_BASE = 0x01;
static const uint8_t SYS_CTRL_TO_ARM  = 0x02;
static const uint8_t SYS_ARM_TO_CTRL  = 0x03;

static const uint16_t JOY_CENTRE   = 32768;
static const uint8_t  BTN_IDLE     = 0x00;
static const uint8_t  BTN_POSITIVE = 0x01;
static const uint8_t  BTN_NEGATIVE = 0x02;

/* ───────────── Globals ───────────── */
static WiFiClient   net;
static PubSubClient mqtt(net);

static NimBLECharacteristic *bleFrameChar = nullptr;
static const char *BLE_SERVICE_UUID = "6a1c0001-9b2c-4f0a-9f00-000000000000";
static const char *BLE_FRAME_UUID   = "6a1c0002-9b2c-4f0a-9f00-000000000000";

/* ───────────── Frame builder ───────────── */
static void emit_frame(uint8_t sysID, uint8_t mode,
                       uint16_t x, uint16_t y,
                       uint8_t zDir, uint8_t yaw)
{
    uint8_t f[COMM_FRAME_LEN];
    f[0]  = COMM_START_1;
    f[1]  = COMM_START_2;
    f[2]  = sysID;
    f[3]  = mode;
    f[4]  = (uint8_t)(x >> 8);
    f[5]  = (uint8_t)(x & 0xFF);
    f[6]  = (uint8_t)(y >> 8);
    f[7]  = (uint8_t)(y & 0xFF);
    f[8]  = zDir;
    f[9]  = yaw;
    f[10] = COMM_END_1;
    f[11] = COMM_END_2;
    Serial1.write(f, COMM_FRAME_LEN);
}

/* ───────────── JSON helpers ───────────── */
static uint16_t json_u16(JsonVariant v, uint16_t fallback) {
    if (v.is<int>())   return (uint16_t)constrain(v.as<int>(),    0, 65535);
    if (v.is<float>()) return (uint16_t)constrain(v.as<float>(),  0.0f, 65535.0f);
    return fallback;
}
static uint8_t json_u8(JsonVariant v, uint8_t fallback) {
    if (v.is<int>()) return (uint8_t)constrain(v.as<int>(), 0, 255);
    return fallback;
}

/* ───────────── MQTT inbound ───────────── */
static void mqtt_callback(char *topic, byte *payload, unsigned int len)
{
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, payload, len)) return;

    if (!strcmp(topic, "vehicle/base")) {
        uint16_t x    = json_u16(doc["x"], JOY_CENTRE);
        uint16_t y    = json_u16(doc["y"], JOY_CENTRE);
        uint8_t  zDir = json_u8 (doc["zDir"], BTN_IDLE);
        uint8_t  yaw  = json_u8 (doc["yaw"],  BTN_IDLE);
        emit_frame(SYS_CTRL_TO_BASE, 0x00, x, y, zDir, yaw);
    }
    else if (!strcmp(topic, "vehicle/arm")) {
        uint8_t  mode = json_u8 (doc["mode"], 0x01);   /* default = CONTROLLER */
        uint16_t x    = json_u16(doc["x"], JOY_CENTRE);
        uint16_t y    = json_u16(doc["y"], JOY_CENTRE);
        uint8_t  zDir = json_u8 (doc["zDir"], BTN_IDLE);
        uint8_t  yaw  = json_u8 (doc["yaw"],  BTN_IDLE);
        emit_frame(SYS_CTRL_TO_ARM, mode, x, y, zDir, yaw);
    }
    /* Other topics ignored */
}

/* ───────────── UART RX parser (mirrors STM32 FSM) ───────────── */
enum ParserState { PS_W1, PS_W2, PS_COLLECT, PS_E1, PS_E2 };
static ParserState ps_state = PS_W1;
static uint8_t     ps_idx   = 0;
static uint8_t     ps_raw[COMM_FRAME_LEN];

static void on_frame(const uint8_t *raw)
{
    /* Decode and republish to MQTT. We only really expect sysID=0x03 here,
     * but echo any inbound frame to its corresponding topic for symmetry. */
    uint8_t  sysID = raw[2];
    uint8_t  mode  = raw[3];
    uint16_t x     = ((uint16_t)raw[4] << 8) | raw[5];
    uint16_t y     = ((uint16_t)raw[6] << 8) | raw[7];
    uint8_t  zDir  = raw[8];
    uint8_t  yaw   = raw[9];

    char buf[128];
    int n = snprintf(buf, sizeof(buf),
        "{\"sysID\":%u,\"mode\":%u,\"x\":%u,\"y\":%u,\"zDir\":%u,\"yaw\":%u}",
        sysID, mode, x, y, zDir, yaw);
    if (n <= 0) return;

    const char *topic = (sysID == SYS_ARM_TO_CTRL) ? "vehicle/arm/state"
                                                   : "vehicle/raw";
    if (mqtt.connected()) mqtt.publish(topic, buf, n);

    /* Mirror to BLE notify characteristic if anyone subscribed. */
    if (bleFrameChar) {
        bleFrameChar->setValue((uint8_t *)buf, n);
        bleFrameChar->notify();
    }
}

static void parser_feed(uint8_t b)
{
    switch (ps_state) {
        case PS_W1:
            if (b == COMM_START_1) { ps_raw[0] = b; ps_state = PS_W2; }
            break;
        case PS_W2:
            if (b == COMM_START_2) { ps_raw[1] = b; ps_idx = 0; ps_state = PS_COLLECT; }
            else if (b == COMM_START_1) { ps_raw[0] = b; }
            else { ps_state = PS_W1; }
            break;
        case PS_COLLECT:
            ps_raw[2 + ps_idx++] = b;
            if (ps_idx >= COMM_PAYLOAD_LEN) ps_state = PS_E1;
            break;
        case PS_E1:
            if (b == COMM_END_1) { ps_raw[10] = b; ps_state = PS_E2; }
            else                 { ps_state = PS_W1; }
            break;
        case PS_E2:
            if (b == COMM_END_2) { ps_raw[11] = b; on_frame(ps_raw); }
            ps_state = PS_W1;
            break;
    }
}

/* ───────────── BLE write callback ───────────── */
class FrameWriteCB : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        /* Forward raw 12-byte frame from BLE writer straight to UART.
         * Caller is responsible for the framing — easier than re-decoding. */
        if (v.size() == COMM_FRAME_LEN) {
            Serial1.write((const uint8_t *)v.data(), v.size());
        }
    }
};

static void ble_setup() {
    NimBLEDevice::init("SolarBridge");
    NimBLEServer *srv = NimBLEDevice::createServer();
    NimBLEService *svc = srv->createService(BLE_SERVICE_UUID);
    bleFrameChar = svc->createCharacteristic(
        BLE_FRAME_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);
    bleFrameChar->setCallbacks(new FrameWriteCB());
    svc->start();
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_SERVICE_UUID);
    adv->start();
}

/* ───────────── WiFi / MQTT plumbing ───────────── */
static uint32_t wifi_attempt_started = 0;

static void wifi_try_connect() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifi_attempt_started = millis();
}

static void wifi_fallback_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_FALLBACK_SSID);
}

static bool mqtt_try_connect() {
    if (mqtt.connect(MQTT_CLIENT_ID)) {
        mqtt.subscribe("vehicle/base");
        mqtt.subscribe("vehicle/arm");
        return true;
    }
    return false;
}

/* ───────────── Arduino lifecycle ───────────── */
void setup() {
    Serial.begin(115200);
    Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
    delay(100);
    Serial.println("[bridge] booting");

    wifi_try_connect();
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setCallback(mqtt_callback);

    ble_setup();
}

void loop() {
    /* WiFi state machine */
    if (WiFi.getMode() == WIFI_STA && WiFi.status() != WL_CONNECTED) {
        if (millis() - wifi_attempt_started > WIFI_RETRY_MS) {
            Serial.println("[bridge] STA join failed; switching to AP");
            wifi_fallback_ap();
        }
    }

    /* MQTT keepalive */
    if (WiFi.status() == WL_CONNECTED) {
        if (!mqtt.connected()) mqtt_try_connect();
        mqtt.loop();
    }

    /* Drain UART RX into the parser */
    while (Serial1.available()) {
        parser_feed((uint8_t)Serial1.read());
    }
}

/*
 * esp32s3_bridge.ino  —  Pi ↔ Base STM32 bridge (SRR-CP v1.0)
 *
 *  Target: ESP32-S3-WROOM-1  /  Arduino-ESP32 core ≥ 2.0.14
 *
 *  Replaces the legacy ESP8266 click shield (esp8266_mqtt.ino). Native USB-CDC
 *  on the S3 gives us flash+monitor without a USB-UART dongle; 320 KB SRAM
 *  lets us buffer frames across WiFi reconnects; BLE 5 provides a no-WiFi
 *  fallback for field debugging.
 *
 *  Wiring (ESP32-S3 ⇆ STM32 UART4)
 *  ───────────────────────────────
 *     GPIO17 (UART1 TX) ──► PA1  (UART4_RX on STM32)
 *     GPIO18 (UART1 RX) ◄── PC10 (UART4_TX on STM32)
 *     GND    ──────────── GND
 *
 *     Both sides run 3.3 V logic; no level shifter needed.
 *     Baud: 460800 8N1.
 *
 *  Primary transport: WiFi-STA + MQTT (same broker as before).
 *  Fallback:          BLE GATT write-characteristic = raw frame bytes.
 *  Safety fallback:   AP "SolarBot_AP" when STA join fails for 10 s.
 *
 *  MQTT topic → SRR-CP frame mapping (Pi → Base)
 *  ─────────────────────────────────────────────
 *     vehicle/base       DST=0x01  CMD=0x10 DRIVE      payload [x, y]          (2)
 *     vehicle/arm        DST=0x02  CMD=0x20 ARM_TARGET payload [jid, hi, lo]   (3)
 *     vehicle/effector   DST=0x02  CMD=0x30 EE_TORQUE  payload [state]         (1)
 *     vehicle/estop      DST=0xFF  CMD=0x40 ESTOP      payload [reason]        (1)
 *
 *  Return path (Base → Pi)
 *  ───────────────────────
 *     STATUS_STREAM (0x52)  → vehicle/telemetry/<field_id>
 *     FAULT_REPORT  (0x60)  → vehicle/fault/<subsystem>
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <NimBLEDevice.h>

/* ===== Configuration ===================================================== */
#define WIFI_SSID        "ESP32_AP"
#define WIFI_PASS        "pibridge2026"
#define MQTT_BROKER_IP   "192.168.4.2"
#define MQTT_BROKER_PORT 1883
#define WIFI_FALLBACK_MS 10000u

#define UART_BAUD        460800u
#define UART_TX_PIN      17
#define UART_RX_PIN      18

/* SRR-CP wire constants — must match comm_protocol.h on the STM32 */
#define SRRCP_STX        0xAA
#define SRRCP_ETX        0x55
#define SRRCP_PAYLOAD_MAX 16
#define SRRCP_FRAME_MAX  (6 + SRRCP_PAYLOAD_MAX + 2)   /* 24 bytes */

#define ADDR_PI       0x00
#define ADDR_BASE     0x01
#define ADDR_ARM_EE   0x02
#define ADDR_ESP32    0xFE
#define ADDR_BCAST    0xFF

#define CMD_DRIVE        0x10
#define CMD_ARM_TARGET   0x20
#define CMD_EE_TORQUE    0x30
#define CMD_ESTOP_ASSERT 0x40
#define CMD_STATUS_STREAM 0x52
#define CMD_FAULT_REPORT 0x60

/* BLE service + characteristic UUIDs (bridge-private) */
#define BLE_SERVICE_UUID "6a1c0001-7e8c-4b4e-8c44-000000000001"
#define BLE_FRAME_UUID   "6a1c0002-7e8c-4b4e-8c44-000000000002"

/* ===== Globals =========================================================== */
WiFiClient     wifi_client;
PubSubClient   mqtt(wifi_client);
HardwareSerial BaseLink(1);                 /* UART1 on the S3 */

static uint32_t sta_attempt_start_ms = 0;
static bool     ap_mode_active       = false;

/* ===== CRC8 Dallas/Maxim (poly 0x31, init 0x00) ========================== */
static const uint8_t crc8_table[256] = {
    0x00,0x31,0x62,0x53,0xC4,0xF5,0xA6,0x97,0xB9,0x88,0xDB,0xEA,0x7D,0x4C,0x1F,0x2E,
    0x43,0x72,0x21,0x10,0x87,0xB6,0xE5,0xD4,0xFA,0xCB,0x98,0xA9,0x3E,0x0F,0x5C,0x6D,
    0x86,0xB7,0xE4,0xD5,0x42,0x73,0x20,0x11,0x3F,0x0E,0x5D,0x6C,0xFB,0xCA,0x99,0xA8,
    0xC5,0xF4,0xA7,0x96,0x01,0x30,0x63,0x52,0x7C,0x4D,0x1E,0x2F,0xB8,0x89,0xDA,0xEB,
    0x3D,0x0C,0x5F,0x6E,0xF9,0xC8,0x9B,0xAA,0x84,0xB5,0xE6,0xD7,0x40,0x71,0x22,0x13,
    0x7E,0x4F,0x1C,0x2D,0xBA,0x8B,0xD8,0xE9,0xC7,0xF6,0xA5,0x94,0x03,0x32,0x61,0x50,
    0xBB,0x8A,0xD9,0xE8,0x7F,0x4E,0x1D,0x2C,0x02,0x33,0x60,0x51,0xC6,0xF7,0xA4,0x95,
    0xF8,0xC9,0x9A,0xAB,0x3C,0x0D,0x5E,0x6F,0x41,0x70,0x23,0x12,0x85,0xB4,0xE7,0xD6,
    0x7A,0x4B,0x18,0x29,0xBE,0x8F,0xDC,0xED,0xC3,0xF2,0xA1,0x90,0x07,0x36,0x65,0x54,
    0x39,0x08,0x5B,0x6A,0xFD,0xCC,0x9F,0xAE,0x80,0xB1,0xE2,0xD3,0x44,0x75,0x26,0x17,
    0xFC,0xCD,0x9E,0xAF,0x38,0x09,0x5A,0x6B,0x45,0x74,0x27,0x16,0x81,0xB0,0xE3,0xD2,
    0xBF,0x8E,0xDD,0xEC,0x7B,0x4A,0x19,0x28,0x06,0x37,0x64,0x55,0xC2,0xF3,0xA0,0x91,
    0x47,0x76,0x25,0x14,0x83,0xB2,0xE1,0xD0,0xFE,0xCF,0x9C,0xAD,0x3A,0x0B,0x58,0x69,
    0x04,0x35,0x66,0x57,0xC0,0xF1,0xA2,0x93,0xBD,0x8C,0xDF,0xEE,0x79,0x48,0x1B,0x2A,
    0xC1,0xF0,0xA3,0x92,0x05,0x34,0x67,0x56,0x78,0x49,0x1A,0x2B,0xBC,0x8D,0xDE,0xEF,
    0x82,0xB3,0xE0,0xD1,0x46,0x77,0x24,0x15,0x3B,0x0A,0x59,0x68,0xFF,0xCE,0x9D,0xAC
};

static uint8_t crc8(const uint8_t *buf, size_t n) {
    uint8_t c = 0x00;
    for (size_t i = 0; i < n; i++) c = crc8_table[c ^ buf[i]];
    return c;
}

/* ===== Frame emit ========================================================= */
static void emit_frame(uint8_t dst, uint8_t src, uint8_t cmd,
                       const uint8_t *payload, uint8_t len)
{
    if (len > SRRCP_PAYLOAD_MAX) return;

    uint8_t frame[SRRCP_FRAME_MAX];
    uint8_t i = 0;
    frame[i++] = SRRCP_STX;
    frame[i++] = dst;
    frame[i++] = src;
    frame[i++] = cmd;
    frame[i++] = len;
    for (uint8_t k = 0; k < len; k++) frame[i++] = payload[k];
    frame[i] = crc8(&frame[1], 4 + len);  i++;
    frame[i++] = SRRCP_ETX;

    BaseLink.write(frame, i);
}

/* ===== RX parser (Base → Pi) ============================================= */
/* Same 4-state FSM as the STM32 side. Re-publishes STATUS_STREAM/FAULT_REPORT
 * as MQTT topics. */

enum ParserState { PS_IDLE, PS_HDR, PS_PAYLOAD, PS_CRC, PS_ETX };
struct Parser {
    ParserState state = PS_IDLE;
    uint8_t     buf[SRRCP_FRAME_MAX];
    uint8_t     idx = 0;
    uint8_t     len = 0;
};
static Parser rx_parser;

static void on_frame(uint8_t dst, uint8_t src, uint8_t cmd,
                     const uint8_t *payload, uint8_t len)
{
    if (!mqtt.connected()) return;

    char topic[48];
    char body[48];

    if (cmd == CMD_STATUS_STREAM && len >= 3) {
        uint8_t  field = payload[0];
        uint16_t val   = ((uint16_t)payload[1] << 8) | payload[2];
        snprintf(topic, sizeof(topic), "vehicle/telemetry/%02x", field);
        snprintf(body,  sizeof(body),  "%u", val);
        mqtt.publish(topic, body);
    } else if (cmd == CMD_FAULT_REPORT && len >= 2) {
        uint8_t subsys = payload[0];
        uint8_t code   = payload[1];
        snprintf(topic, sizeof(topic), "vehicle/fault/%02x", subsys);
        snprintf(body,  sizeof(body),  "%u", code);
        mqtt.publish(topic, body);
    }
    (void)dst; (void)src;
}

static void feed_rx(uint8_t b)
{
    switch (rx_parser.state) {
        case PS_IDLE:
            if (b == SRRCP_STX) { rx_parser.idx = 0; rx_parser.state = PS_HDR; }
            break;
        case PS_HDR:
            rx_parser.buf[rx_parser.idx++] = b;
            if (rx_parser.idx == 4) {
                rx_parser.len = rx_parser.buf[3];
                if (rx_parser.len > SRRCP_PAYLOAD_MAX) { rx_parser.state = PS_IDLE; break; }
                rx_parser.state = (rx_parser.len == 0) ? PS_CRC : PS_PAYLOAD;
            }
            break;
        case PS_PAYLOAD:
            rx_parser.buf[rx_parser.idx++] = b;
            if (rx_parser.idx == (uint8_t)(4 + rx_parser.len)) rx_parser.state = PS_CRC;
            break;
        case PS_CRC: {
            uint8_t expect = crc8(rx_parser.buf, 4 + rx_parser.len);
            if (expect == b) { rx_parser.state = PS_ETX; }
            else             { rx_parser.state = PS_IDLE; }
            break;
        }
        case PS_ETX:
            if (b == SRRCP_ETX) {
                on_frame(rx_parser.buf[0], rx_parser.buf[1], rx_parser.buf[2],
                         &rx_parser.buf[4], rx_parser.len);
            }
            rx_parser.state = PS_IDLE;
            break;
    }
}

/* ===== MQTT → frame mapping ============================================== */
static void on_mqtt_message(char *topic, uint8_t *payload, unsigned int len)
{
    if (strcmp(topic, "vehicle/base") == 0 && len >= 2) {
        uint8_t p[2] = { payload[0], payload[1] };
        emit_frame(ADDR_BASE, ADDR_PI, CMD_DRIVE, p, 2);
    } else if (strcmp(topic, "vehicle/arm") == 0 && len >= 3) {
        uint8_t p[3] = { payload[0], payload[1], payload[2] };
        emit_frame(ADDR_ARM_EE, ADDR_PI, CMD_ARM_TARGET, p, 3);
    } else if (strcmp(topic, "vehicle/effector") == 0 && len >= 1) {
        uint8_t p[1] = { payload[0] };
        emit_frame(ADDR_ARM_EE, ADDR_PI, CMD_EE_TORQUE, p, 1);
    } else if (strcmp(topic, "vehicle/estop") == 0) {
        uint8_t reason = (len >= 1) ? payload[0] : 0x51; /* SOFTWARE */
        emit_frame(ADDR_BCAST, ADDR_PI, CMD_ESTOP_ASSERT, &reason, 1);
    }
}

/* ===== BLE GATT — same frames, different transport ======================= */
class FrameCb : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *c) override {
        std::string v = c->getValue();
        /* Write is expected to be a fully-formed SRR-CP frame. Re-emit on UART. */
        if (v.size() > 0 && v.size() <= SRRCP_FRAME_MAX) {
            BaseLink.write((const uint8_t *)v.data(), v.size());
        }
    }
};

static void ble_init()
{
    NimBLEDevice::init("SolarBot-Bridge");
    NimBLEServer         *server  = NimBLEDevice::createServer();
    NimBLEService        *service = server->createService(BLE_SERVICE_UUID);
    NimBLECharacteristic *ch      = service->createCharacteristic(
        BLE_FRAME_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    ch->setCallbacks(new FrameCb());
    service->start();
    server->getAdvertising()->addServiceUUID(BLE_SERVICE_UUID);
    server->getAdvertising()->start();
}

/* ===== WiFi / MQTT plumbing ============================================== */
static void ensure_mqtt()
{
    if (mqtt.connected()) return;
    if (!WiFi.isConnected()) return;
    if (mqtt.connect("bridge-s3")) {
        mqtt.subscribe("vehicle/base");
        mqtt.subscribe("vehicle/arm");
        mqtt.subscribe("vehicle/effector");
        mqtt.subscribe("vehicle/estop");
    }
}

static void ensure_wifi()
{
    if (WiFi.isConnected()) { ap_mode_active = false; return; }

    if (!ap_mode_active && sta_attempt_start_ms == 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        sta_attempt_start_ms = millis();
        return;
    }

    if (!ap_mode_active &&
        (millis() - sta_attempt_start_ms) > WIFI_FALLBACK_MS)
    {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP("SolarBot_AP", "pibridge2026");
        ap_mode_active = true;
    }
}

/* ===== Setup / loop ======================================================= */
void setup()
{
    Serial.begin(115200);
    BaseLink.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

    ensure_wifi();
    mqtt.setServer(MQTT_BROKER_IP, MQTT_BROKER_PORT);
    mqtt.setCallback(on_mqtt_message);

    ble_init();
}

void loop()
{
    ensure_wifi();
    ensure_mqtt();
    mqtt.loop();

    /* Drain STM32 → Pi return path. */
    while (BaseLink.available()) {
        feed_rx((uint8_t)BaseLink.read());
    }
}

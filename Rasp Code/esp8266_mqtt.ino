#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------- Wi-Fi AP ----------
const char* AP_SSID = "ESP32_AP";
const char* AP_PASS = "esp32password";

// ---------- MQTT ----------
const char* MQTT_HOST = "192.168.4.2";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "esp8266-uart-bridge";
const char* MQTT_TOPIC = "vehicle/base";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ---------- Framing bytes ----------
#define START_BYTE 0xAA
#define END_BYTE   0x55

static float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static uint8_t float01_to_u8(float v) {
  v = clamp01(v);
  return (uint8_t)(v * 255.0f + 0.5f);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<128> doc;
  DeserializationError err = deserializeJson(doc, payload, length);
  if (err) return;

  float x = clamp01(doc["x"] | 0.5f);
  float y = clamp01(doc["y"] | 0.5f);

  uint8_t x_byte = float01_to_u8(x);
  uint8_t y_byte = float01_to_u8(y);

  uint8_t msg[4];
  msg[0] = START_BYTE;
  msg[1] = x_byte;
  msg[2] = y_byte;
  msg[3] = END_BYTE;

  // Send framed packet over UART to STM
  Serial.write(msg, 4);
}

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  while (!mqtt.connected()) {
    if (mqtt.connect(MQTT_CLIENT_ID)) {
      mqtt.subscribe(MQTT_TOPIC);
    } else {
      delay(500);
    }
  }
}

void setup() {
  Serial.begin(115200);   // UART to STM
  delay(100);

  startAP();
  connectMQTT();
}

void loop() {
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();
  delay(5);
}

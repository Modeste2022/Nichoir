#include "M5TimerCAM.h"  
#include <WiFi.h>
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// ---------- WIFI ----------
const char* ssid     = "electroProjectWifi";
const char* password = "B1MesureEnv";

// ---------- MQTT ----------
const char* mqtt_server = "192.168.2.36";  // Votre Raspberry Pi
const int mqtt_port = 1883;
const char* mqtt_topic_image = "nichoir/image";
const char* mqtt_topic_battery = "nichoir/battery";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---------- WIFI CONNECT ----------
void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ---------- MQTT CONNECT ----------
void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    // Client ID unique
    String clientId = "TimerCAM-" + String(random(0xffff), HEX);
    
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("connected to MQTT!");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// -------------- Camera init optimisée --------------
void myCamInit(){
  if (!TimerCAM.Camera.begin()) {
    Serial.println("Camera Init Fail");
    return;
  }
  Serial.println("Camera Init Success");

  TimerCAM.Camera.sensor->set_pixformat(TimerCAM.Camera.sensor, PIXFORMAT_JPEG);
  TimerCAM.Camera.sensor->set_framesize(TimerCAM.Camera.sensor, FRAMESIZE_VGA); // 640x480
  TimerCAM.Camera.sensor->set_quality(TimerCAM.Camera.sensor, 15); // Qualité réduite pour MQTT
  TimerCAM.Camera.sensor->set_vflip(TimerCAM.Camera.sensor, 1);
  TimerCAM.Camera.sensor->set_hmirror(TimerCAM.Camera.sensor, 0);
}

// ------------ Send Image via MQTT ------------
void sendImageViaMQTT(){
  if (TimerCAM.Camera.get()) {
    size_t len = TimerCAM.Camera.fb->len;
    uint8_t* buf = TimerCAM.Camera.fb->buf;

    Serial.printf("Captured JPG: %d bytes\n", len);

    // Vérification taille (MQTT a des limitations)
    if(len > 100000) { // ~100KB max pour MQTT
      Serial.println("Image trop grande pour MQTT, réduction qualité");
      TimerCAM.Camera.free();
      return;
    }

    // Convertir en base64
    String imageBase64 = base64::encode(buf, len);
    
    // Créer le JSON pour MQTT
    DynamicJsonDocument doc(150000); // Taille adaptée pour les images
    doc["filename"] = "bird_" + String(millis()) + ".jpg";
    doc["image"] = imageBase64;
    doc["battery_level"] = TimerCAM.Power.getBatteryLevel();
    doc["voltage"] = TimerCAM.Power.getBatteryVoltage();
    doc["timestamp"] = millis();
    doc["resolution"] = "640x480";
    doc["esp32_id"] = "esp32_m5stack";

    String mqttPayload;
    serializeJson(doc, mqttPayload);

    Serial.printf("MQTT payload size: %d bytes\n", mqttPayload.length());

    // Envoyer via MQTT
    if (mqttClient.connected()) {
      bool result = mqttClient.publish(mqtt_topic_image, mqttPayload.c_str());
      if (result) {
        Serial.println("Image sent via MQTT successfully!");
      } else {
        Serial.println("Failed to send image via MQTT");
      }
    } else {
      Serial.println("MQTT not connected, cannot send image");
    }

    TimerCAM.Camera.free();
  } else {
    Serial.println("Failed to capture image");
  }
}

// ------------ Send Battery Data via MQTT ------------
void sendBatteryData() {
  DynamicJsonDocument doc(512);
  doc["battery_level"] = TimerCAM.Power.getBatteryLevel();
  doc["voltage"] = TimerCAM.Power.getBatteryVoltage();
  doc["timestamp"] = millis();
  doc["esp32_id"] = "esp32_m5stack";

  String payload;
  serializeJson(doc, payload);

  if (mqttClient.connected()) {
    mqttClient.publish(mqtt_topic_battery, payload.c_str());
    Serial.println("Battery data sent via MQTT");
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  TimerCAM.begin(true);
  myCamInit();
  setupWiFi();
  
  // Configuration MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(160000); // Buffer augmenté pour les images

  Serial.println("ESP32 TimerCAM Ready with MQTT!");
}

// ============================================================
//  LOOP
// ============================================================
unsigned long lastPhotoTime = 0;
unsigned long lastBatteryTime = 0;
const unsigned long PHOTO_INTERVAL = 30000; // 30 secondes
const unsigned long BATTERY_INTERVAL = 60000; // 1 minute

void loop() {
  // Maintenir la connexion MQTT
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  unsigned long now = millis();
  
  // Envoyer les données batterie régulièrement
  if (now - lastBatteryTime > BATTERY_INTERVAL) {
    lastBatteryTime = now;
    sendBatteryData();
  }
  
  // Prendre une photo périodiquement (pour test)
  if (now - lastPhotoTime > PHOTO_INTERVAL) {
    lastPhotoTime = now;
    
    Serial.println("Taking photo and sending via MQTT...");
    sendImageViaMQTT();
    
    Serial.println("Waiting for next photo...");
  }

  delay(1000);
}
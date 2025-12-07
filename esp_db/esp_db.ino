#include <WiFi.h>
#include <PubSubClient.h>
#include "esp_camera.h"
#include <base64.h>

// Configuration WiFi
const char* ssid     = "electroProjectWifi";
const char* password = "B1MesureEnv";

// Configuration MQTT
const char* mqtt_broker = "192.168.2.27";
const int mqtt_port = 1883;
const char* topic = "nichoir/data";

WiFiClient espClient;
PubSubClient client(espClient);

// Configuration M5Stack TimerCam (OV3660)
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM     27
#define SIOD_GPIO_NUM     25
#define SIOC_GPIO_NUM     23
#define Y9_GPIO_NUM       19
#define Y8_GPIO_NUM       36
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       39
#define Y5_GPIO_NUM        5
#define Y4_GPIO_NUM       34
#define Y3_GPIO_NUM       35
#define Y2_GPIO_NUM       32
#define VSYNC_GPIO_NUM    22
#define HREF_GPIO_NUM     26
#define PCLK_GPIO_NUM     21

// Broche PIR
#define PIR_PIN 33

// LED (optionnel)
#define LED_PIN 2

void setup_camera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  // Qualité selon la mémoire disponible
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA; // 1600x1200
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_SVGA; // 800x600
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Erreur caméra 0x%x\n", err);
    return;
  }
  
  // Configuration du capteur
  sensor_t * s = esp_camera_sensor_get();
  if (s != NULL) {
    s->set_vflip(s, 1); // Retourner verticalement si nécessaire
    s->set_hmirror(s, 0); // Miroir horizontal
  }
  
  Serial.println("Caméra initialisée avec succès");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  Serial.println("\n=== M5Stack TimerCam ===");
  
  // Connexion WiFi
  Serial.println("Connexion au WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connecté");
    Serial.print("Adresse IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nÉchec connexion WiFi");
  }
  
  // Configuration MQTT
  client.setServer(mqtt_broker, mqtt_port);
  client.setBufferSize(65000); // Augmenter pour les images
  
  // Initialisation caméra
  setup_camera();
}

void reconnect_mqtt() {
  int attempts = 0;
  while (!client.connected() && attempts < 3) {
    Serial.println("Connexion au broker MQTT...");
    String clientId = "TimerCam_" + String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("Connecté au broker MQTT");
    } else {
      Serial.print("Échec, rc=");
      Serial.print(client.state());
      Serial.println(" nouvelle tentative dans 5s");
      delay(5000);
      attempts++;
    }
  }
}

void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop();

  // Lecture du capteur PIR
  bool motion = digitalRead(PIR_PIN);
  
  if (motion) {
    Serial.println("Mouvement détecté !");
    digitalWrite(LED_PIN, HIGH);
    
    // Capture d'image
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Échec capture d'image");
      digitalWrite(LED_PIN, LOW);
      return;
    }
    
    Serial.printf("Image capturée: %d octets\n", fb->len);
    
    // Conversion en base64
    String imageBase64 = base64::encode(fb->buf, fb->len);
    esp_camera_fb_return(fb);
    
    // Lecture tension batterie (broche 38 sur TimerCam)
    float battery = analogRead(38) * (3.3 / 4095.0) * 2;
    
    // Préparation du message JSON
    String payload = "{";
    payload += "\"device_id\":\"TimerCam_01\",";
    payload += "\"motion\":true,";
    payload += "\"battery\":" + String(battery, 2) + ",";
    payload += "\"timestamp\":" + String(millis()) + ",";
    payload += "\"image\":\"" + imageBase64 + "\"";
    payload += "}";
    
    // Publication MQTT (peut nécessiter plusieurs paquets)
    if (client.publish(topic, payload.c_str())) {
      Serial.println("Données envoyées avec succès");
    } else {
      Serial.println("Échec envoi des données");
    }
    
    digitalWrite(LED_PIN, LOW);
    
    // Attente avant prochaine capture
    delay(10000); // 10 secondes
  }
  
  delay(100);
}


#include "M5TimerCAM.h"  
#include <WiFi.h>
#include <PubSubClient.h>
#include <base64.h>

// ---------- WIFI ----------
const char* ssid     = "electroProjectWifi";
const char* password = "B1MesureEnv";

// ---------- MQTT ----------
const char* mqtt_server = "192.168.2.36";
WiFiClient espClient;
PubSubClient client(espClient);

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

// ---------- MQTT RECONNECT ----------
void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("TimerCamTestClient")) {
      Serial.println("connected!");
    } else {
      Serial.print("failed rc=");
      Serial.print(client.state());
      Serial.println(" trying again in 2s");
      delay(2000);
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
  
  // COMMENCER PAR UNE RÉSOLUTION BASSE
  TimerCAM.Camera.sensor->set_framesize(TimerCAM.Camera.sensor, FRAMESIZE_VGA); // 640x480
  
  // QUALITÉ MOYENNE POUR RÉDUIRE LA TAILLE
  TimerCAM.Camera.sensor->set_quality(TimerCAM.Camera.sensor, 20);
  
  TimerCAM.Camera.sensor->set_vflip(TimerCAM.Camera.sensor, 1);
  TimerCAM.Camera.sensor->set_hmirror(TimerCAM.Camera.sensor, 0);
}

// ------------ Picture Function optimisée ------------
void TakePictureFct(){
  if (TimerCAM.Camera.get()) {
    size_t len = TimerCAM.Camera.fb->len;
    uint8_t* buf = TimerCAM.Camera.fb->buf;

    Serial.printf("Captured JPG: %d bytes\n", len);

    // Vérification taille
    if(len > 50000) {
      Serial.println("Image trop grande, annulation");
      TimerCAM.Camera.free();
      return;
    }

    // Encoder en Base64
    String encodedImage = base64::encode(buf, len);
    Serial.printf("Base64 size: %d bytes\n", encodedImage.length());

    // Publier
    bool ok = client.publish("picture", encodedImage.c_str());
    client.publish("picsize", String(len).c_str());

    if (ok) {
      Serial.println("MQTT: Picture sent (Base64)!");
    } else {
      Serial.println("MQTT: Failed to send picture!");
      // Debug
      Serial.printf("MQTT state: %d\n", client.state());
      Serial.printf("Buffer size: %d\n", client.getBufferSize());
    }

    TimerCAM.Camera.free();
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

  client.setServer(mqtt_server, 1883);
  client.setBufferSize(1024 * 100); // Buffer de 100KB
  reconnectMQTT();
}

// ============================================================
//  LOOP
// ============================================================
unsigned long lastMsg = 0;
int photoCount = 0;

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 10000) { // ⚠️ Passer à 10s entre les photos
    lastMsg = now;
    photoCount++;

    // Envoyer batterie
    String volt = String(TimerCAM.Power.getBatteryVoltage());
    String levl = String(TimerCAM.Power.getBatteryLevel());
    
    client.publish("Volt", volt.c_str());
    client.publish("Levl", levl.c_str());
    client.publish("photoCount", String(photoCount).c_str());
    
    Serial.println("Published: Voltage, Level and Count");
    
    // Prendre photo
    TakePictureFct();
  }
}

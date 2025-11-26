
#include "M5TimerCAM.h"  
#include <WiFi.h>
#include <HTTPClient.h>
#include <base64.h>
#include <ArduinoJson.h>

// ---------- WIFI ----------
const char* ssid     = "electroProjectWifi";
const char* password = "B1MesureEnv";

// ---------- SERVEUR RASPBERRY PI ----------
const char* serverURL = "http://192.168.2.36:5000/api/upload_image"; // Modifier l'IP si nécessaire

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

// ------------ HTTP Upload Function ------------
void uploadImageToServer(){
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

    // Créer un client HTTP
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    // Préparer les données
    String encodedImage = base64::encode(buf, len);
    String postData = "nom=esp32_photo_" + String(millis()) + ".jpg";
    postData += "&esp32_id=esp32_m5stack";
    postData += "&resolution=640x480";
    postData += "&image_data=" + encodedImage;

    Serial.printf("Uploading image to server... Size: %d bytes\n", postData.length());

    // Envoyer la requête POST
    int httpResponseCode = http.POST(postData);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("Server response: %d - %s\n", httpResponseCode, response.c_str());
      
      // Parser la réponse JSON
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, response);
      
      if (doc["status"] == "success") {
        Serial.printf("Image uploaded successfully! ID: %d\n", doc["image_id"].as<int>());
      } else {
        Serial.println("Upload failed on server side");
      }
    } else {
      Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
    TimerCAM.Camera.free();
  }
}

// ------------ Alternative avec multipart/form-data ------------
void uploadImageMultipart(){
  if (TimerCAM.Camera.get()) {
    size_t len = TimerCAM.Camera.fb->len;
    uint8_t* buf = TimerCAM.Camera.fb->buf;

    Serial.printf("Captured JPG: %d bytes\n", len);

    HTTPClient http;
    http.begin(serverURL);
    
    // Créer une boundary unique
    String boundary = "----WebKitFormBoundary" + String(millis());
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    // Construire le body multipart
    String body = "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"nom\"\r\n\r\n";
    body += "esp32_photo_" + String(millis()) + ".jpg\r\n";

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"esp32_id\"\r\n\r\n";
    body += "esp32_m5stack\r\n";

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"resolution\"\r\n\r\n";
    body += "640x480\r\n";

    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"image\"; filename=\"photo.jpg\"\r\n";
    body += "Content-Type: image/jpeg\r\n\r\n";

    // Convertir le body en bytes
    size_t bodyHeaderLength = body.length();
    size_t totalLength = bodyHeaderLength + len + ("\r\n--" + boundary + "--\r\n").length();

    // Commencer la requête
    http.beginRequest();
    http.sendHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.sendHeader("Content-Length", String(totalLength));
    http.beginBody();

    // Envoyer le body en plusieurs parties
    http.write((const uint8_t*)body.c_str(), bodyHeaderLength);
    http.write(buf, len);
    http.write((const uint8_t*)"\r\n--", 4);
    http.write((const uint8_t*)boundary.c_str(), boundary.length());
    http.write((const uint8_t*)"--\r\n", 4);

    int httpResponseCode = http.endRequest();

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("Server response: %d - %s\n", httpResponseCode, response.c_str());
    } else {
      Serial.printf("HTTP Error: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
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

  Serial.println("ESP32 Camera Ready!");
  Serial.printf("Server URL: %s\n", serverURL);
}

// ============================================================
//  LOOP
// ============================================================
unsigned long lastPhotoTime = 0;
const unsigned long PHOTO_INTERVAL = 30000; // 30 secondes entre les photos
int photoCount = 0;

void loop() {
  unsigned long now = millis();
  
  if (now - lastPhotoTime > PHOTO_INTERVAL) {
    lastPhotoTime = now;
    photoCount++;

    // Envoyer les infos batterie
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;
      http.begin("http://192.168.2.36:5000/api/status");
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
      String statusData = "esp32_id=esp32_m5stack";
      statusData += "&voltage=" + String(TimerCAM.Power.getBatteryVoltage());
      statusData += "&battery_level=" + String(TimerCAM.Power.getBatteryLevel());
      statusData += "&photo_count=" + String(photoCount);
      
      http.POST(statusData);
      http.end();
    }

    Serial.printf("Taking photo #%d\n", photoCount);
    
    // Prendre et uploader la photo
    uploadImageToServer(); // Utilisez uploadImageMultipart() si l'autre ne fonctionne pas
    
    Serial.println("Waiting for next photo...");
  }

  delay(1000);
}
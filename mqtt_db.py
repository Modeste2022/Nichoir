import paho.mqtt.client as mqtt
import base64
import json
import time
from datetime import datetime
import os

# Configuration MQTT
MQTT_BROKER = "192.168.2.36"
MQTT_PORT = 1883
MQTT_TOPIC_PICTURE = "picture"
MQTT_TOPIC_INFO = "picsize"
MQTT_TOPIC_BATTERY = "camera/battery/level"

# Dossier de sauvegarde
SAVE_FOLDER = "camera_images"
os.makedirs(SAVE_FOLDER, exist_ok=True)

# Variables pour stocker les données
current_image_data = None
current_image_size = None

def on_connect(client, userdata, flags, reason_code, properties):
    print(f"Connecté au broker MQTT avec le code: {reason_code}")
    # S'abonner aux topics
    client.subscribe(MQTT_TOPIC_PICTURE)
    client.subscribe(MQTT_TOPIC_INFO)
    client.subscribe(MQTT_TOPIC_BATTERY)
    client.subscribe("photoCount")
    client.subscribe("Volt")
    client.subscribe("Levl")

def on_message(client, userdata, msg):
    global current_image_data, current_image_size
    
    print(f"Message reçu [{msg.topic}]: {len(msg.payload)} bytes")
    
    try:
        if msg.topic == MQTT_TOPIC_PICTURE:
            # Recevoir l'image Base64
            current_image_data = msg.payload.decode('utf-8')
            print(f"Image Base64 reçue: {len(current_image_data)} caractères")
            
            # Sauvegarder l'image si on a toutes les données
            if current_image_data and current_image_size:
                save_image()
                
        elif msg.topic == MQTT_TOPIC_INFO:
            # Recevoir la taille de l'image
            current_image_size = msg.payload.decode('utf-8')
            print(f"Taille image: {current_image_size} bytes")
            
        elif msg.topic == "photoCount":
            print(f"Numéro de photo: {msg.payload.decode('utf-8')}")
            
        elif msg.topic == "Volt":
            print(f"Tension batterie: {msg.payload.decode('utf-8')} mV")
            
        elif msg.topic == "Levl":
            print(f"Niveau batterie: {msg.payload.decode('utf-8')}%")
            
    except Exception as e:
        print(f"Erreur traitement message: {e}")

def save_image():
    global current_image_data, current_image_size
    
    if not current_image_data or not current_image_size:
        return
        
    try:
        # Créer un nom de fichier avec timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"{SAVE_FOLDER}/photo_{timestamp}.jpg"
        
        # Décoder Base64
        image_bytes = base64.b64decode(current_image_data)
        
        # Sauvegarder l'image
        with open(filename, "wb") as f:
            f.write(image_bytes)
        
        print(f"✅ Image sauvegardée: {filename} ({len(image_bytes)} bytes)")
        
        # Réinitialiser pour la prochaine image
        current_image_data = None
        current_image_size = None
        
    except Exception as e:
        print(f"❌ Erreur sauvegarde image: {e}")

def main():
    # Créer le client MQTT
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    
    try:
        # Se connecter au broker
        print(f"Connexion au broker MQTT {MQTT_BROKER}:{MQTT_PORT}...")
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
        
        # Démarrer la boucle MQTT
        client.loop_forever()
        
    except KeyboardInterrupt:
        print("\nArrêt du programme...")
        client.disconnect()
    except Exception as e:
        print(f"Erreur connexion: {e}")

if __name__ == "__main__":
    main()
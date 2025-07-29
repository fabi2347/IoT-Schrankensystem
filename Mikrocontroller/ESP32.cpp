// Import aller benötigten Bibliotheken
#include <Arduino.h>
#include <HardwareSerial.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <HTTPClient.h>

// PIN-Definition für die bidirektionale Kommunikation über UART1
#define UART1_TX 17
#define UART1_RX 16

// Deklaration der Variablen für die WLAN-Zugangsdaten
const char* ssid = "SSID";
const char* password = "Passwort";

// Objekt des HTTP-Servers auf Standardport 80
WiFiServer server(80);

// Globale Variablendeklaration
String header;
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;
const String api_url = "http://192.168.43.151:5000/nfc";

// Funktion um Daten (als als String/JSON) an den STM32 zu senden
void sendSTM32(String msg) {
  Serial1.println(msg);
  delay(100); // Hardwarebedingter Delay von 100ms
}

// Funktion um String zu normalisieren und unnötige Leerzeichen zu entfernen
String strip(String input) {
  input.trim();
  return input;
}

// Funktion der allgemeinen Initialisierung des Mikrocontrollers
void setup() {
  Serial.begin(115200); // Baud-Rate 115200 für UART0 (Debug-Terminal)
  Serial1.begin(115200, SERIAL_8N1, UART1_RX, UART1_TX); // Baud-Rate 115200 für UART1 (bidirektionale Kommunikation des ESP32 und STM32)
  pinMode(2, OUTPUT); // Blaue USER-LED auf PIN 2 als Output konfigurieren

  Serial.println("Verbinde zum WLAN...");
  WiFi.begin(ssid, password); // Verbindung zum WLAN-Netzwerk herstellen

  // Solange der Mikrocontroller nicht zum WLAN verbunden ist, soll die USER-LED blinken
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    digitalWrite(2, !digitalRead(2));
  }

  // Wenn verbunden zum WLAN: USER-LED dauerhaft blau
  Serial.println("\nWLAN Verbindugn aufgebaut!");
  digitalWrite(2, HIGH);

  // IP-Adresse des ESP32 lesen und an den STM32 senden
  Serial.print("IP-Adresse: ");
  String ip_address = WiFi.localIP().toString();
  Serial.println(ip_address);

  DynamicJsonDocument doc(2048);
  doc["type"] = "connected";
  doc["ip"] = ip_address;

  sendSTM32(([] (DynamicJsonDocument& d) {
    String s;
    serializeJson(d, s);
    return s;
  })(doc));

  server.begin(); // Webserver starten
}

// Funktion um Servomotor Befehl als JSON zu serialisieren und an den STM32 zu senden
void servo_cmd(bool auf, bool fromNFC) {
  DynamicJsonDocument doc(2048);
  doc["type"] = "servo";
  if (auf) {
    doc["action"] = "auf";
    if (!fromNFC) {
      doc["source"] = "web";
    }
  } else {
    doc["action"] = "zu";
  }

  sendSTM32(([] (DynamicJsonDocument& d) {
    String s;
    serializeJson(d, s);
    return s;
  })(doc));
}

// Funktion um die NFC UID an den Webserver zu senden, damit diese validiert werden kann
void sendUID(String uid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.setTimeout(3000); // Timeout der Anfrage: 3 Sekunden

    http.begin(api_url); // API Anfrage an Webserver
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"uid\": \"" + uid + "\"}"; // JSON POST-Payload

    int httpResponseCode = http.POST(jsonPayload); // API POST Anfrage

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(response);
      if (httpResponseCode == 200) {
        // Antwort des Webservers deserialisieren
        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, response);

        if (error) {
          Serial.print("JSON parse failed: ");
          Serial.println(error.c_str());
          
          // Netzwerkfehlernachricht an STM32 senden
          DynamicJsonDocument errDoc(2048);
          errDoc["type"] = "nfc_error";
          errDoc["message"] = "Netzwerkfehler";
          sendSTM32(([] (DynamicJsonDocument& d) {
            String s;
            serializeJson(d, s);
            return s;
          })(errDoc));
          return;
        }

        bool access = doc["access"];
        servo_cmd(access, true);
        
        // Zugangsnachricht an STM32 senden
        DynamicJsonDocument resultDoc(2048);
        resultDoc["type"] = "nfc_result";
        resultDoc["access"] = access;
        sendSTM32(([] (DynamicJsonDocument& d) {
          String s;
          serializeJson(d, s);
          return s;
        })(resultDoc));
      }
    } else {
      Serial.print("Fehler in der HTTP Anfrage: ");
      Serial.println(http.errorToString(httpResponseCode));
      // Netzwerkfehlernachricht an STM32 senden
      DynamicJsonDocument errDoc(2048);
      errDoc["type"] = "nfc_error";
      errDoc["message"] = "Netzwerkfehler";
      sendSTM32(([] (DynamicJsonDocument& d) {
        String s;
        serializeJson(d, s);
        return s;
      })(errDoc));
    }
    http.end();
  } else {
    Serial.println("Nicht verbunden zum WLAN!");
    // Netzwerkfehlernachricht an STM32 senden
    DynamicJsonDocument errDoc(2048);
    errDoc["type"] = "nfc_error";
    errDoc["message"] = "Netzwerkfehler";
    sendSTM32(([] (DynamicJsonDocument& d) {
      String s;
      serializeJson(d, s);
      return s;
    })(errDoc));
  }
}

// Loop Funktion des Mikrocontrollers
void loop() {
  if (Serial1.available()) { // Überprüfen, ob eine Nachricht des STM32 vorliegt
    // Nachricht speichern und deserialieren
    String jsonString = Serial1.readStringUntil('\n');
    jsonString = strip(jsonString);

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.f_str());
      return;
    }

    if (doc.containsKey("type")) {
      String type = doc["type"].as<String>();
      
      if (type == "nfc") {
        String uid = doc["uid"].as<String>();
        Serial.println(uid);
        sendUID(uid);
      }
    }
  }

  // ESP32 API Server Anfragen annehmen
  WiFiClient client = server.available();

  // Wenn eine Verbindung zum Server vorliegt, soll der Endpunkt geprüft werden und die entsprechende Funktion ausgeführt werden
  if (client) {
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");
    String currentLine = "";
    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            if (header.indexOf("GET /42697474652031352050756E6B7465203A29/schranke/auf") >= 0) {
              servo_cmd(true, false);
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: text/html");
              client.println("Connection: close");
              client.println();
            } else if (header.indexOf("GET /42697474652031352050756E6B7465203A29/schranke/zu") >= 0) {
              servo_cmd(false, false);
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type: text/html");
              client.println("Connection: close");
              client.println();
            } else {
              client.println("HTTP/1.1 404 Not Found");
              client.println("Content-type: text/html");
              client.println("Connection: close");
              client.println();
            }

            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    
    header = "";
    client.stop(); // Client vom Server trennen
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
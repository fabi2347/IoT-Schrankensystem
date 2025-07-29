// Import aller benötigten Bibliotheken
#include <Arduino.h>
#include <HardwareSerial.h>
#include <LiquidCrystal_PCF8574.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <SPI.h>
#include <MFRC522.h>

// PIN-Definition für das RFID RC522 Modul
#define RST_PIN 9
#define SS_PIN 10

// Objekt des RFID RC522 Moduls mit PIN-Übergabe
MFRC522 mfrc522(SS_PIN, RST_PIN);
// Objekt des LCD Displays mit I2C-Adresse
LiquidCrystal_PCF8574 lcd(0x27);
// Objekt der seriellen Verbindung für bidirektionale Kommunikation über UART1
HardwareSerial Serial1C(PA10, PA9); // RX, TX

// Globale Variable
unsigned long previousMillis = 0;
const long delay_ms = 1000;
bool webActive = false;
bool nfcActive = false;
bool nfcBlocked = false;
unsigned long nfcOpenTime = 0;
const long nfcOpenDuration = 10000;
unsigned long nfcBlockTime = 0;
const long nfcBlockDuration = 3000;

// Funktion um Daten (als String/JSON) an den ESP32 zu senden
void sendESP32(String msg) {
  Serial1C.println(msg);
  delay(100); // Hardwarebedingter Delay von 100ms
}

// Funktion um Schranke über PWM zu schließen
void close() {
  analogWrite(PC6, 0.065 * 65535);
}

// Funktion um Schranke über PWM zu öffnen
void open() {
  analogWrite(PC6, 0.02 * 65535);
}

// Funktion um NFC Modul zu initialisieren
void NFCinit() {
  mfrc522.PCD_Init();
  mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max); // Max. Reichweite
  mfrc522.PCD_DumpVersionToSerial();
}

// Funktion der allgemeinen Initialisierung des Mikrocontrollers
void setup() {
  Serial.begin(115200); // Baud-Rate 115200 für UART0 (Debug-Terminal)
  Serial1C.begin(115200); // Baud-Rate 115200 für UART1 (bidirektionale Kommunikation des ESP32 und STM32)

  pinMode(PC6, OUTPUT); // Servomotor PWM PIN

  analogWriteFrequency(50); // 50 Hz PWM Frequenz
  analogWriteResolution(16); // 16-Bit PWM-Auflösung

  // Initialisierung des LCD Displays
  lcd.begin(16, 2);
  lcd.setBacklight(50);
  lcd.clear();

  // Verbindungsanzeige des Displays
  lcd.setCursor(0, 0);
  lcd.print("ESP32:");
  lcd.setCursor(0, 1);
  lcd.print("Verbinde...");

  // Warten, bis der serielle Port initialisiert wurde
  while (!Serial);

  // SPI-Bus starten
  SPI.begin();
  
  // NFC Modul initialisieren
  NFCinit();
}

// Funktion um String zu normalisieren und unnötige Leerzeichen zu entfernen
String strip(String input) {
  input.trim();
  return input;
}

// Funktion um Schranke über NFC zu öffnen
void handleNFCOpen() {
  nfcActive = true;
  webActive = false;
  nfcOpenTime = millis();
  
  lcd.setCursor(0, 0);
  lcd.print("Schranke offen!");
  open();
}

// Funktion um Schranke über die Website zu öffnen
void handleWebOpen() {
  webActive = true;
  nfcActive = false;
  
  lcd.setCursor(0, 0);
  lcd.print("Schranke dauerh. ");
  lcd.setCursor(0, 1);
  lcd.print("ge\xEF");
  lcd.print("ffnet!           ");
  
  open();
}

// Allgemeine Funktion um die Schranke zu schließen
void handleClose() {
  webActive = false;
  nfcActive = false;
  
  lcd.setCursor(0, 0);
  lcd.print("Schranke        ");
  lcd.setCursor(0, 1);
  lcd.print("geschlossen!    ");
  
  close();
}

// Funktion um einen Netzwerkfehler auf dem LCD Display anzuzeigen
void showNetworkError() {
  nfcBlocked = true;
  nfcBlockTime = millis();
  
  lcd.setCursor(0, 1);
  lcd.print("Netzwerkfehler! ");
  delay(3000);
}

// Funktion um eine Fehlermeldung bei fehlendem Zugriff auf dem LCD Display anzuzeigen
void showAccessDenied() {
  nfcBlocked = true;
  nfcBlockTime = millis();
  
  lcd.setCursor(0, 1);
  lcd.print("Kein Zugang!    ");
  delay(3000);
}

// Funktion für den Countdown der Schrankenschließung (Standardmäßig 10 Sekunden)
void updateNFCCountdown() {
  if (nfcActive) {
    unsigned long elapsed = millis() - nfcOpenTime;
    unsigned long remaining = (nfcOpenDuration - elapsed) / 1000;
    
    if (remaining > 0) {
      lcd.setCursor(0, 1);
      lcd.print("Zeit: ");
      lcd.print(remaining);
      lcd.print(" Sekunden ");
    } else {
      handleClose();
    }
  }
}

// Funktion um zu prüfen, ob das NFC Modul noch blockiert wird (nach Ablauf der Blockierzeit wird das Modul wieder freigegeben)
void checkNFCBlock() {
  if (nfcBlocked && (millis() - nfcBlockTime >= nfcBlockDuration)) {
    nfcBlocked = false;
    
    lcd.setCursor(0, 0);
    lcd.print("Schranke        ");
    lcd.setCursor(0, 1);
    lcd.print("geschlossen!    ");
  }
}

// Loop Funktion des Mikrocontrollers
void loop() {
  if (Serial1C.available()) { // Überprüfen, ob eine Nachricht des ESP32 vorliegt
    // Nachricht speichern und deserialieren
    String jsonString = Serial1C.readStringUntil('\n');
    jsonString = strip(jsonString);
    Serial.println(jsonString);

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonString);

    if (error) {
      Serial.print("JSON parse failed: ");
      Serial.println(error.f_str());
      return;
    }

    if (doc.containsKey("type")) {
      String type = doc["type"].as<String>();
      
      if (type == "connected") {
        lcd.setCursor(0, 1);
        lcd.print(doc["ip"].as<String>() + "           ");
      } else if (type == "servo") {
        String action = doc["action"].as<String>();
        if (action == "auf") {
          if (doc.containsKey("source") && doc["source"] == "web") {
            handleWebOpen();
          } else {
            handleNFCOpen();
          }
        } else if (action == "zu") {
          handleClose();
        }
      } else if (type == "nfc_error") {
        showNetworkError();
      } else if (type == "nfc_result") {
        bool access = doc["access"];
        if (!access) {
          showAccessDenied();
        }
      }
    }
  }

  // NFC Modul Blockierung prüfen/aufheben
  checkNFCBlock();

  // Wenn die Schranke geschlossen ist, NFC nicht blockiert und eine neue NFC Karte vorgelegt wird, wird die neue Karte gelesen und Daten verarbeitet
  if (!webActive && !nfcActive && !nfcBlocked && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    unsigned long currentMillis = millis();

    if (currentMillis - previousMillis >= delay_ms) {
      previousMillis = currentMillis;
      
      // NFC Karten UID dekodieren
      String cardUID = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
        cardUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        cardUID += String(mfrc522.uid.uidByte[i], HEX);
      }

      lcd.setCursor(0, 0);
      lcd.print("UID: ");
      lcd.print(String(cardUID) + "     ");
      lcd.setCursor(0, 1);
      lcd.print("Pr\xF5");
      lcd.print("fe Zugang...     ");

      // Während der zugangsprüfung wird das NFC Modul blockiert
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();

      // Befehl serialisieren und an ESP32 senden
      DynamicJsonDocument doc(2048);
      doc["type"] = "nfc";
      doc["uid"] = String(cardUID);

      sendESP32(([] (DynamicJsonDocument& d) {
        String s;
        serializeJson(d, s);
        return s;
      })(doc));
    }
  }

  // Countdown der Schließung der Schranke auf dem LCD Display anzeigen
  updateNFCCountdown();
}
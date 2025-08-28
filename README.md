# IoT Schrankensystem

## 🔎 Projektbeschreibung

Das IoT-Schrankensystem ist ein intelligentes Schrankensteuerungssystem, z. B. für Firmenparkplätze. Die Steuerung übernimmt ein **STM32**, der folgende Komponenten ansteuert:

- **RFID-RC522** für die NFC-Erkennung
- **SG90 Servomotor** zur Bewegung der Schranke
- **LCD-Display** zur Anzeige von Status- und Zugriffsinfos

Da der STM32 keine WLAN-Funktion besitzt, übernimmt ein **ESP32** die Kommunikation mit dem Internet. Dieser empfängt die Befehle des STM32 über eine selbst entwickelte **JSON-Befehlsschnittstelle** und leitet sie an einen zentralen **Webserver** weiter. Die Antwort wird zurückgegeben und vom STM32 verarbeitet.

Das Webinterface erlaubt die zentrale Verwaltung von NFC-UIDs, das manuelle Steuern der Schranke sowie z. B. das dauerhafte Öffnen bei Events. Alle Vorgänge werden protokolliert.

---

## 🧩 Systemkomponenten

### 🖥️ STM32 (Steuerungseinheit)

- **RFID-RC522 Modul**: Liest NFC-Tags und übergibt UID-Daten zur Prüfung
- **SG90 Servomotor**: Öffnet und schließt die Schranke physisch
- **LCD-Display**: Zeigt UID, Zugriffsstatus und Systeminformationen in Echtzeit an
- Kommunikation mit ESP32 via **UART1** (serielle Verbindung) und eigener **JSON-Schnittstelle**

### 🌐 ESP32 (Netzwerkbrücke)

- Empfang der UID-Daten vom STM32
- Weiterleitung der Anfrage an den Webserver per HTTP
- Rückgabe der Antwort an STM32 zur Ausführung (z. B. Schranke öffnen)

### 🌍 Webinterface (Zentrale Verwaltung)

- **Manuelle Schrankensteuerung** (Öffnen/Schließen)
- **Verwaltung der NFC-UIDs**: Anzeigen, Hinzufügen, Bearbeiten, Entfernen
- **Protokollanzeige**: Übersicht über alle Zugriffsversuche (erfolgreich, abgelehnt)
- **Statusanzeige**: Aktueller Zustand der Schranke, Anzahl der Vorgänge etc.

---

## ✅ Features

- Manuelles Öffnen der Schranke  
- Manuelles Schließen der Schranke  
- Anzeige von Gesamtstatistiken:
  - Anzahl Öffnungen
  - Anzahl Schließungen
  - Abgelehnte Versuche (ungültige UID / unzulässige Zeit)
- Anzeige aller zugelassenen NFC-UIDs  
- Hinzufügen / Entfernen von UID-Einträgen  
- Details zu jeder UID:
  - Name (z. B. Besitzer)
  - Zugangszeiten (von – bis)
- Bearbeiten und Löschen von UID-Daten
- Zugriff auf vollständiges Protokoll:
  - UID
  - Zeitpunkt
  - Status (Geöffnet, Geschlossen, Abgelehnt)
  - Typ (Manuell, NFC, Automatisch, Nicht registriert, Zeitbeschränkung)
  - Dauer der Öffnung
- Aktualisieren und Löschen des Protokolls

---

## 🖼️ Visualisierung

### 📷 Webinterface  
![Webinterface 1 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Webinterface_1.jpg?raw=true)
![Webinterface 2 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Webinterface_2.jpg?raw=true)
![Webinterface 3 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Webinterface_3.jpg?raw=true)
![Webinterface 4 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Webinterface_4.jpg?raw=true)
![Webinterface 5 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Webinterface_5.jpg?raw=true)

### 🔧 Hardware  
![Hardware 1 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Hardware_1.jpg?raw=true)
![Hardware 2 Image](https://github.com/fabi2347/IoT-Schrankensystem/blob/main/Project_Images/Hardware_2.jpg?raw=true)

---

## 🛠️ Voraussetzungen

- STM32 L152RE Mikrocontroller
- ESP32 NodeMCU D1 R32 Mikrocontroller mit WLAN-Funktion
- RFID-RC522 NFC-Modul
- SG90 Servomotor
- LCD-Display (z. B. über I2C)
- NFC-Tags/Karten

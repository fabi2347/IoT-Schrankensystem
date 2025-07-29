from flask import Flask, render_template, jsonify, request, send_from_directory
from werkzeug.exceptions import HTTPException
from datetime import datetime, timedelta
import os, json, requests
from threading import Timer

app = Flask(__name__)

### EINSTELLUNGEN ###

OPERATIONS_FILE = 'operations.json'
NFC_UIDS_FILE = 'nfc_uids.json'
DATE_FORMAT = "%d.%m.%Y %H:%M:%S"
ESP_ADDR = "http://192.168.43.196"

### EINSTELLUNGEN ###

nfc_timers = {}

# Funktion um die Protokollliste zu laden und dezuserialisieren
def load_operations():
    try:
        if os.path.exists(OPERATIONS_FILE):
            with open(OPERATIONS_FILE, 'r+') as file:
                return json.load(file)
    except Exception as e:
        print(f"Error: {e}")
    return []

# Funktion um die Protokollliste zu speichern und serialisieren
def save_operations(operations_list):
    try:
        with open(OPERATIONS_FILE, 'w+') as file:
            json.dump(operations_list, file, indent=2)
    except Exception as e:
        print(f"Error: {e}")

# Funktion um die NFC UID Liste zu laden und dezuserialisieren
def load_nfc_uids():
    try:
        if os.path.exists(NFC_UIDS_FILE):
            with open(NFC_UIDS_FILE, 'r+') as file:
                data = json.load(file)
                # Convert old format (list) to new format (dict with metadata)
                if isinstance(data.get('allowed_uids', None), list):
                    return {uid: {"name": "", "from": "", "to": ""} for uid in data['allowed_uids']}
                return data.get('allowed_uids', {})
    except Exception as e:
        print(f"Error: {e}")
    return {}

# Funktion um die NFC UID Liste zu speichern und serialisieren
def save_nfc_uids(uids):
    try:
        with open(NFC_UIDS_FILE, 'w+') as file:
            json.dump({'allowed_uids': uids}, file, indent=2)
    except Exception as e:
        print(f"Error: {e}")

ALLOWED_UIDS = load_nfc_uids()
operations = load_operations()

# Funktion um das Datum als String in einen richtigen Timestamp zu verarbeiten
def parse_timestamp(timestamp_string):
    return datetime.strptime(timestamp_string, DATE_FORMAT)

# Funktion um den aktuellen Zustand der Schranke abzufragen (geöffnet oder geschlossen)
def get_current_gate_status():
    if not operations:
        return None, None
    
    current_time = datetime.now()
    
    for operation in operations:
        operation_time = parse_timestamp(operation['timestamp'])
        if operation_time <= current_time and operation['status'] != "Nicht registriert":
            return operation['status'], operation
    
    return None, None

# Funktion zur Berechnung der Öffnungsdauer der Schranke
def calculate_duration(open_time_str, close_time_str):
    open_time = parse_timestamp(open_time_str)
    close_time = parse_timestamp(close_time_str)
    duration_seconds = int((close_time - open_time).total_seconds())
    return str(duration_seconds)

# Funktion um die Protokollliste nach ID zu sortieren
def sort_operations_by_id(operations_list, reverse=False):
    return sorted(operations_list, key=lambda op: int(op['id']), reverse=reverse)

# Funktion um die Protokollliste zu verarbeiten
def process_operations(operations_list):
    processed_operations = []
    open_operations = {}
    sorted_operations = sort_operations_by_id(operations_list)

    for operation in sorted_operations:
        operation_copy = operation.copy()
        
        if 'uid' not in operation_copy:
            operation_copy['uid'] = "—"
        
        if operation['status'] == "Geöffnet":
            open_operations[operation['id']] = operation_copy
            processed_operations.append(operation_copy)
        else:
            processed_operations.append(operation_copy)

    final_operations = []
    for operation in sort_operations_by_id(processed_operations, reverse=True):
        operation_with_duration = operation.copy()
        
        if operation['status'] == "Geöffnet":
            corr_close_id = str(int(operation['id']) + 1)
            close_operation = None

            for op in processed_operations:
                if op['id'] == corr_close_id and op['status'] == "Geschlossen":
                    close_operation = op
                    break
            
            if close_operation:
                duration = calculate_duration(operation['timestamp'], close_operation['timestamp'])
                operation_with_duration['duration'] = duration
            else:
                operation_with_duration['duration'] = "—"
        else:
            operation_with_duration['duration'] = "—"
        
        final_operations.append(operation_with_duration)

    return final_operations

# API-Endpunkt für statische Elemente (CSS-Dateien, JS-Dateien, Fonts)
@app.route('/static/<path:filename>')
def serve_static(filename):
    root_dir = os.path.dirname(os.getcwd())
    return send_from_directory(os.path.join(root_dir, 'static'), filename)

# API-Endpunkt für die Statistik auf dem Dashboard (serialisiert als JSON)
def get_statistics(operations_list):
    current_time = datetime.now()
    past_operations = []

    for op in operations_list:
        operation_time = parse_timestamp(op['timestamp'])
        if operation_time <= current_time:
            past_operations.append(op)

    return {
        "total_operations": len(past_operations),
        "open_count": len([op for op in past_operations if op["status"] == "Geöffnet"]),
        "closed_count": len([op for op in past_operations if op["status"] == "Geschlossen"]),
        "denied_count": len([op for op in past_operations if op["status"] == "Nicht registriert"]),
        "invalid_time_count": len([op for op in past_operations if op["status"] == "Unzulässige Zugangszeit"]),
        "last_operation": past_operations[0] if past_operations else None
    }

# API-Endpunkt zum Rendern des Dahsboards
@app.route('/')
def dashboard():
    return render_template('dashboard.html')

# API-Endpunkt für die Protokollliste als JSON
@app.route('/api/operations')
def get_operations_api():
    current_time = datetime.now()
    all_operations = load_operations()
    
    current_operations = []

    for op in all_operations:
        operation_time = parse_timestamp(op['timestamp'])
        if operation_time <= current_time:
            current_operations.append(op)

    processed_operations = process_operations(current_operations)

    return jsonify({
        "operations": processed_operations,
        "stats": get_statistics(processed_operations),
        "current_status": get_current_gate_status()[0]
    })

# API-Endpunkt für die Manuell öffnen/schließen Buttons mit Anfrage an den ESP32
@app.route('/api/control', methods=['POST'])
def control_gate():
    action = request.json.get('action')

    if action not in ['open', 'close']:
        return jsonify({"success": False, "message": "Ungültige Aktion"}), 400

    current_status, last_operation = get_current_gate_status()

    if action == 'open' and current_status == 'Geöffnet':
        return jsonify({"success": False, "message": "Schranke ist bereits geöffnet"}), 400

    if action == 'close' and current_status == 'Geschlossen':
        if (last_operation and last_operation.get('reason') == "NFC" and last_operation['id'] in nfc_timers):
            nfc_timers[last_operation['id']].cancel()
            del nfc_timers[last_operation['id']]
        return jsonify({"success": False, "message": "Schranke ist bereits geschlossen"}), 400

    endpoint = "/42697474652031352050756E6B7465203A29/schranke/auf" if action == "open" else "/42697474652031352050756E6B7465203A29/schranke/zu" # Endpunkt an den ESP32 zur Steuerung der Schranke -> URL Bruteforce Schutz mit Easter Egg :=)
    try:
        resp_code = requests.get(ESP_ADDR + endpoint, timeout=3).status_code
    except Exception:
        return jsonify({
            "success": False,
            "message": f"ESP32 ist nicht erreichbar"
        })

    if resp_code == 200:
        now = datetime.now().strftime(DATE_FORMAT)
        last_id = int(operations[0]['id']) if operations else 0
        new_id = str(last_id + 1)

        new_entry = {
            "id": new_id,
            "timestamp": now,
            "status": "Geöffnet" if action == "open" else "Geschlossen",
            "reason": "Manuell"
        }

        operations.insert(0, new_entry)
        save_operations(operations)

        if action == 'close' and current_status == 'Geöffnet':
            if (last_operation and last_operation.get('reason') == "NFC" and last_operation['id'] in nfc_timers):
                nfc_timers[last_operation['id']].cancel()
                del nfc_timers[last_operation['id']]

        return jsonify({
            "success": True,
            "message": f"Schranke erfolgreich {'geöffnet' if action == 'open' else 'geschlossen'}"
        })
    else:
        return jsonify({
            "success": False,
            "message": f"ESP32 API Fehler"
        })

# API-Endpunkt um die bisherige Protokollliste zu löschen
@app.route('/api/clear_log', methods=['POST'])
def clear_log():
    try:
        global operations
        operations = []
        save_operations(operations)
        
        for timer in nfc_timers.values():
            timer.cancel()
        nfc_timers.clear()
        
        return jsonify({
            "success": True,
            "message": "Protokoll erfolgreich gelöscht"
        })
    except Exception as e:
        return jsonify({
            "success": False,
            "message": f"Fehler beim Löschen des Protokolls: {str(e)}"
        }), 500

# API-Endpunkt für das Management der NFC UID's (auslesen, hinzufügen, löschen, bearbeiten)
@app.route('/api/nfc_uids', methods=['GET', 'POST', 'DELETE', 'PATCH'])
def manage_nfc_uids():
    if request.method == 'GET':
        global ALLOWED_UIDS
        ALLOWED_UIDS = load_nfc_uids()
        
        return jsonify({
            "success": True,
            "uids": ALLOWED_UIDS
        })
    
    elif request.method == 'POST':
        data = request.get_json()
        uid = data.get('uid')
        name = data.get('name', "")
        time_from = data.get('from', "")
        time_to = data.get('to', "")

        if not uid:
            return jsonify({"success": False, "message": "UID ist erforderlich"}), 400

        # Schutz vor XSS-Attacken (Cross-Site Scripting)
        if not uid.isalnum():
            return jsonify({
                "success": False, 
                "message": "UID darf nur Buchstaben und Zahlen enthalten (keine Sonderzeichen oder Leerzeichen)"
            }), 400
            
        if uid in ALLOWED_UIDS:
            return jsonify({"success": False, "message": "UID existiert bereits"}), 400

        ALLOWED_UIDS[uid] = {
            "name": name,
            "from": time_from,
            "to": time_to
        }
        save_nfc_uids(ALLOWED_UIDS)

        ALLOWED_UIDS = load_nfc_uids()
        return jsonify({"success": True, "message": "UID erfolgreich hinzugefügt"})
    
    elif request.method == 'PATCH':
        data = request.get_json()
        uid = data.get('uid')
        name = data.get('name', "")
        time_from = data.get('from', "")
        time_to = data.get('to', "")

        if not uid:
            return jsonify({"success": False, "message": "UID ist erforderlich"}), 400

        if uid not in ALLOWED_UIDS:
            return jsonify({"success": False, "message": "UID nicht gefunden"}), 404

        ALLOWED_UIDS[uid] = {
            "name": name,
            "from": time_from,
            "to": time_to
        }
        save_nfc_uids(ALLOWED_UIDS)

        ALLOWED_UIDS = load_nfc_uids()
        return jsonify({"success": True, "message": "UID erfolgreich aktualisiert"})
        
    elif request.method == 'DELETE':
        data = request.get_json()
        uid = data.get('uid')
        
        if not uid:
            return jsonify({"success": False, "message": "UID ist erforderlich"}), 400

        if uid not in ALLOWED_UIDS:
            return jsonify({"success": False, "message": "UID nicht gefunden"}), 404

        ALLOWED_UIDS.pop(uid)
        save_nfc_uids(ALLOWED_UIDS)
        
        ALLOWED_UIDS = load_nfc_uids()
        return jsonify({"success": True, "message": "UID erfolgreich entfernt"})

# API-Endpunkt für die NFC Datenverarbeitung des ESP32
@app.route('/nfc', methods=['POST'])
def nfc_access():
    try:
        if not request.is_json:
            return jsonify({"error": "Die Anfrage muss im JSON-Format sein"}), 400

        data = request.get_json()
        uid = data.get('uid')

        if not uid:
            return jsonify({"error": "Parameter 'uid' fehlt"}), 400

        current_status, _ = get_current_gate_status()
        access_granted = uid in ALLOWED_UIDS
        time_restriction_failed = False
        
        # Zeitbeschränkungen prüfen
        if access_granted:
            uid_data = ALLOWED_UIDS[uid]
            time_from = uid_data.get('from')
            time_to = uid_data.get('to')
            if time_from and time_to:
                try:
                    now = datetime.now().time()
                    from_time = datetime.strptime(time_from, "%H:%M").time()
                    to_time = datetime.strptime(time_to, "%H:%M").time()
                    if not (from_time <= now <= to_time):
                        access_granted = False
                        time_restriction_failed = True
                except ValueError:
                    pass

        # Prüfen, ob die Schranke schon geöffnet ist
        if access_granted and current_status == 'Geöffnet':
            return jsonify({"access": False, "message": "Schranke ist bereits geöffnet"}), 200

        now = datetime.now().strftime(DATE_FORMAT)
        last_id = int(operations[0]['id']) if operations else 0
        new_id = str(last_id + 1)

        # Status für das Protokoll bestimmen
        if time_restriction_failed:
            status = "Unzulässige Zugangszeit"
        elif not access_granted:
            status = "Nicht registriert"
        else:
            status = "Geöffnet"

        # Eintrag in das Protokoll einfügen
        new_entry = {
            "id": new_id,
            "uid": uid,
            "timestamp": now,
            "status": status,
            "reason": "NFC"
        }

        operations.insert(0, new_entry)
        save_operations(operations)

        if access_granted and not time_restriction_failed:
            # Nur schließen, wenn Zugang erlaubt war
            def close_gate():
                current_status, _ = get_current_gate_status()
                if current_status == 'Geschlossen':
                    return

                close_time = datetime.now().strftime(DATE_FORMAT)
                close_entry = {
                    "id": str(int(new_id) + 1),
                    "uid": uid,
                    "timestamp": close_time,
                    "status": "Geschlossen",
                    "reason": "Automatisch (NFC)"
                }

                operations.insert(0, close_entry)
                save_operations(operations)

                if new_id in nfc_timers:
                    del nfc_timers[new_id]

            timer = Timer(10, close_gate)
            nfc_timers[new_id] = timer
            timer.start()

        return jsonify({"access": access_granted}), 200

    except Exception as e:
        print(str(e))
        return jsonify({"error": "Server error", "message": str(e)}), 500

if __name__ == '__main__':
    # Erstelle NFC UIDs Datei, falls sie noch nicht existiert
    if not os.path.exists(NFC_UIDS_FILE):
        with open(NFC_UIDS_FILE, 'w') as f:
            json.dump({"allowed_uids": []}, f)

    app.run(debug=True, host="0.0.0.0", port=5000) # Webserver im Debug-Modus auf allen verfügbaren IP-Adressen auf dem Port 5000 starten (da keine root-Rechte benötigt werden für Ports über 1024)
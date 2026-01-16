from flask import Flask, render_template, request, jsonify, send_from_directory
import os
import Adafruit_BBIO.GPIO as GPIO
import Adafruit_BBIO.ADC as ADC
import paho.mqtt.client as mqtt
import threading
import random
import time

app = Flask(__name__)

# ==== CONFIGURACIÓN DE PINES ====
leds = {
    "baño_master": "P8_7",
    "baño_secundario": "P8_8",
    "pasillo": "P8_9",
    "sala": "P8_10", 
    "cocina": "P8_11",
    "dormitorio_master": "P8_12",
    "dormitorio_secundario": "P8_14"
}

sensores = {
    "puerta": "P9_39",  
    "luz": "P9_40"      
}

# ==== CONFIGURACIÓN MQTT ====
MQTT_BROKER = "localhost"  # Cambia a la dirección de tu broker MQTT
MQTT_PORT = 1883
MQTT_TOPIC_ALARMA = "alarma/estado"  # Tópico de suscripción

# ==== ESTADO DE LA ALARMA ====
alarm_state = "DESACTIVADA"
alarm_lock = threading.Lock()
alarm_blinking = False
alarm_stop_blinking = False

# ==== INICIALIZAR PINES ====
for pin in leds.values():
    GPIO.setup(pin, GPIO.OUT)
    GPIO.output(pin, GPIO.LOW)  # todos apagados inicialmente

# Inicializar ADC (pines analógicos)
ADC.setup()

# ==== FUNCIONES MQTT ====
def on_connect(client, userdata, flags, rc):
    """Callback cuando se conecta al broker MQTT"""
    if rc == 0:
        print(f"✓ Conectado al broker MQTT en {MQTT_BROKER}:{MQTT_PORT}")
        print(f"  Suscrito a: {MQTT_TOPIC_ALARMA}")
        client.subscribe(MQTT_TOPIC_ALARMA)
    else:
        print(f"✗ Error en conexión MQTT: código {rc}")

def on_message(client, userdata, msg):
    """Callback cuando se recibe un mensaje MQTT"""
    global alarm_state, alarm_blinking, alarm_stop_blinking
    
    try:
        payload = msg.payload.decode('utf-8').strip().upper()  # Convertir a mayúsculas
        print(f"Mensaje MQTT recibido en tema '{msg.topic}': {payload}")
        
        valid_states = ["DESACTIVADA", "ACTIVADA", "DISPARADA", "DURMIENDO"]
        
        if payload in valid_states:
            with alarm_lock:
                alarm_state = payload
                print(f"✓ Estado de alarma actualizado a: {alarm_state}")
                
                if payload == "DISPARADA" and not alarm_blinking:
                    # Iniciar el parpadeo de luces
                    alarm_blinking = True
                    alarm_stop_blinking = False
                    threading.Thread(target=blinking_lights, daemon=True).start()
                elif payload != "DISPARADA":
                    # Detener el parpadeo si la alarma se desactiva
                    alarm_stop_blinking = True
                    alarm_blinking = False
        else:
            print(f"✗ Estado de alarma inválido recibido: '{payload}'")
    except Exception as e:
        print(f"✗ Error procesando mensaje MQTT: {e}")

def init_mqtt():
    """Inicializa la conexión MQTT en un hilo separado"""
    global mqtt_client
    
    mqtt_client = mqtt.Client()
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    
    try:
        mqtt_client.connect(MQTT_BROKER, MQTT_PORT, keepalive=60)
        mqtt_client.loop_start()
        print(f"Intentando conectar a MQTT: {MQTT_BROKER}:{MQTT_PORT}")
    except Exception as e:
        print(f"Error conectando a MQTT: {e}")

def blinking_lights():
    """Hace parpadear todas las luces en un patrón aleatorio cuando la alarma se dispara"""
    global alarm_blinking, alarm_stop_blinking
    
    try:
        while alarm_blinking and not alarm_stop_blinking:
            # Generar patrón aleatorio de luces
            pattern = {room: random.choice([GPIO.HIGH, GPIO.LOW]) for room in leds}
            
            for room, state in pattern.items():
                GPIO.output(leds[room], state)
            
            time.sleep(0.3)  # Parpadeo cada 300ms
            
            if alarm_stop_blinking:
                break
        
        # Apagar todas las luces al terminar
        for pin in leds.values():
            GPIO.output(pin, GPIO.LOW)
        
        alarm_blinking = False
        
    except Exception as e:
        print(f"Error en blinking_lights: {e}")
        alarm_blinking = False
    
# ==== RUTAS FLASK ====
@app.route('/sources/<path:filename>')
def serve_static(filename):
    return send_from_directory(os.path.join(app.root_path, 'templates', 'sources'), filename)

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/set_led', methods=['POST'])
def set_led():
    """
    Endpoint para encender/apagar un LED.
    Espera JSON: { "habitacion": "sala", "estado": true }
    """
    data = request.get_json()
    habitacion = data.get('habitacion')
    estado = data.get('estado', False)

    if habitacion not in leds:
        return jsonify({"error": "Habitación no reconocida"}), 400

    GPIO.output(leds[habitacion], GPIO.HIGH if estado else GPIO.LOW)
    return jsonify({"ok": True, "habitacion": habitacion, "estado": estado})

@app.route('/set_intensity', methods=['POST'])
def set_intensity():
    """
    Endpoint para ajustar la intensidad de un LED (encendido/apagado).
    Espera JSON: { "habitacion": "sala", "intensidad": 75 }
    Intensidad > 0 enciende el LED, 0 lo apaga.
    """
    data = request.get_json()
    habitacion = data.get('habitacion')
    intensidad = int(data.get('intensidad', 0))

    if habitacion not in leds:
        return jsonify({"error": "Habitación no reconocida"}), 400

    pin = leds[habitacion]
    
    # Asegurar que el pin está configurado como OUTPUT
    GPIO.setup(pin, GPIO.OUT)
    
    # Si intensidad es 0, apagar; si no, encender
    estado = GPIO.HIGH if intensidad > 0 else GPIO.LOW
    GPIO.output(pin, estado)
    
    return jsonify({"ok": True, "habitacion": habitacion, "intensidad": intensidad, "estado": "encendido" if estado else "apagado"})

@app.route('/leer_sensores')
def leer_sensores():
    """
    Devuelve el estado de los sensores conectados (analógicos).
    P9_39 y P9_40 son pines ADC que devuelven valores entre 0 y 1 (0V a 1.8V)
    """
    # Leer valores analógicos (0.0 a 1.0)
    valor_puerta = ADC.read("AIN0")   # P9_39
    valor_luz = ADC.read("AIN1")      # P9_40
    
    # Convertir a rango 0-1024 para compatibilidad
    valor_puerta = int(valor_puerta * 1024)
    valor_luz = int(valor_luz * 1024)

    # Umbrales (ajusta según calibración real)
    UMBRAL_PUERTA = 650  # > umbral => puerta abierta. 1.15V = 654 (1.15/1.8*1024)
    UMBRAL_LUZ = 200     # > umbral => hay luz (día). 0.3V (oscuro) ≈ 171

    puerta_abierta = valor_puerta > UMBRAL_PUERTA
    luz_dia = valor_luz > UMBRAL_LUZ

    payload = {
        # Valores crudos
        "puerta": valor_puerta,            
        "luz": valor_luz,                 
        # Nuevos campos semánticos
        "puerta_valor": valor_puerta,
        "puerta_abierta": puerta_abierta,
        "luz_valor": valor_luz,
        "luz_dia": luz_dia,
        "umbrales": {"puerta": UMBRAL_PUERTA, "luz": UMBRAL_LUZ}
    }
    return jsonify(payload)

@app.route('/set_all', methods=['POST'])
def set_all():
    data = request.get_json()
    intensidad = int(data.get('intensidad', 0))
    for pin in leds.values():
        GPIO.output(pin, GPIO.HIGH if intensidad > 0 else GPIO.LOW)
    return jsonify({"ok": True, "intensidad": intensidad})

@app.route('/estado_alarma')
def estado_alarma():
    """
    Devuelve el estado actual de la alarma.
    Estados: DESACTIVADA, ACTIVADA, DISPARADA, DURMIENDO
    """
    with alarm_lock:
        estado = alarm_state.upper()  # Asegurar que siempre está en mayúsculas
    return jsonify({"estado": estado, "blinking": alarm_blinking})


# ==== APAGAR TODOS LOS LEDS AL SALIR ====
@app.teardown_appcontext
def cleanup(exception=None):
    global mqtt_client
    alarm_stop_blinking = True
    if 'mqtt_client' in globals():
        mqtt_client.loop_stop()
    GPIO.cleanup()

# ==== EJECUTAR SERVIDOR ====
if __name__ == '__main__':
    init_mqtt()
    app.run(host='0.0.0.0', port=8080, debug=True)

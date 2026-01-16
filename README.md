# Sistema de Domótica Inteligente con BeagleBone Black y ESP32

Sistema integrado de control domótico que combina BeagleBone Black, ESP32 y una interfaz web interactiva para la gestión automatizada de iluminación y seguridad del hogar.

## 📋 Descripción del Proyecto

Este proyecto implementa un sistema completo de automatización del hogar que integra:
- **Control de iluminación** mediante interfaz web con plano interactivo
- **Sistema de alarma** con ESP32 y comunicación MQTT
- **Automatización inteligente** basada en sensores (puerta e iluminación ambiental)
- **Modo verano** para control automático de luces según la luz natural

## 🎯 Objetivos

1. Controlar las luces de diferentes habitaciones mediante una interfaz web intuitiva
2. Implementar un sistema de alarma con combinación de seguridad configurable
3. Automatizar el encendido de luces al detectar apertura de puerta
4. Activar iluminación automática al anochecer en modo verano
5. Integrar BeagleBone Black y ESP32 mediante protocolo MQTT

## 🔧 Componentes Hardware

### BeagleBone Black (BBB)
- **Función**: Servidor web y control de iluminación
- **Pines GPIO utilizados**:
  - P8_7: LED Baño Master
  - P8_8: LED Baño Secundario
  - P8_9: LED Pasillo
  - P8_10: LED Sala
  - P8_11: LED Cocina
  - P8_12: LED Dormitorio Master
  - P8_14: LED Dormitorio Secundario
- **Pines ADC**:
  - P9_39 (AIN0): Sensor de puerta (infrarrojo)
  - P9_40 (AIN1): Sensor de luminosidad

### ESP32
- **Función**: Sistema de alarma con comunicación MQTT
- **Pines utilizados**:
  - GPIO 25: Pulsador 1 (combinación de seguridad)
  - GPIO 26: Pulsador 2 (combinación de seguridad)
  - GPIO 33: Sensor infrarrojo (detección de movimiento)
  - GPIO 2: LED indicador de estado
  - GPIO 4 (T0): Sensor táctil (despertar de modo sueño)

### Sensores
- **Sensor infrarrojo de puerta**: Detecta apertura/cierre (umbral: 650/1024)
- **Sensor de luminosidad**: Detecta luz ambiental día/noche (umbral: 200/1024)
- **Sensor infrarrojo de movimiento**: Dispara alarma cuando está activada

## 🏗️ Arquitectura del Sistema

```
┌─────────────────┐         MQTT          ┌──────────────┐
│     ESP32       │ ◄──────────────────►  │ MQTT Broker  │
│   (Alarma)      │   alarma/estado       │ (localhost)  │
└─────────────────┘                        └──────────────┘
                                                  ▲
                                                  │ MQTT
                                                  ▼
┌─────────────────┐         HTTP          ┌──────────────┐
│  Navegador Web  │ ◄──────────────────►  │ BeagleBone   │
│   (Cliente)     │   Flask App :8080     │   Black      │
└─────────────────┘                        │  + Sensores  │
                                           │  + LEDs      │
                                           └──────────────┘
```

## ✨ Características Principales

### Control de Iluminación
- **Plano interactivo** de la vivienda con 7 habitaciones
- **Control individual** de cada habitación con slider de intensidad (0-100%)
- **Botones globales**: Encender/Apagar todas las luces
- **Persistencia visual** del estado de cada habitación

### Sistema de Alarma (ESP32)
- **Estados**: DESACTIVADA, ACTIVADA, DISPARADA, DURMIENDO
- **Combinación de seguridad configurable** vía puerto serial (hasta 20 pasos)
- **Almacenamiento en EEPROM** de la combinación (persiste tras reset)
- **Watchdog timer** (15 segundos) durante ingreso de combinación
- **Modo sueño profundo** tras 30 segundos inactivo (despertar por touch)
- **Indicador LED** con diferentes patrones según estado
- **Publicación MQTT** del estado cada 5 segundos

### Automatización Inteligente
1. **Detección de puerta abierta**: Enciende automáticamente pasillo y sala
2. **Modo verano**: 
   - Al anochecer: enciende dormitorio master, cocina y pasillo al 60%
   - Al amanecer: apaga automáticamente estas luces
3. **Indicador de alarma** en tiempo real en la interfaz web

### Sensores en Tiempo Real
- **Polling cada 1 segundo** del estado de sensores
- **Visualización** de estado de puerta (Abierta/Cerrada) y luz ambiente (Luz/Oscuro)
- **Cooldown de 5 segundos** para evitar acciones repetitivas

## 📂 Estructura del Proyecto

```
Sistemas-Embebidos-Practica-Final/
├── README.md                          # Este archivo
├── controlDomotica.py                 # Servidor Flask (BeagleBone Black)
├── ESP32_ProyectoFinal/
│   └── ESP32_ProyectoFinal.ino       # Firmware del sistema de alarma
├── templates/
│   └── index.html                     # Interfaz web interactiva
└── static/
    └── sources/
        └── plano.svg                  # Plano de la vivienda
```

## 🚀 Instalación y Configuración

### Requisitos Previos

#### BeagleBone Black
```bash
# Actualizar sistema
sudo apt-get update
sudo apt-get upgrade

# Instalar Python y dependencias
sudo apt-get install python3 python3-pip

# Instalar librerías Python
sudo pip3 install flask
sudo pip3 install Adafruit_BBIO
sudo pip3 install paho-mqtt
```

#### ESP32
- Arduino IDE o PlatformIO
- Librerías necesarias:
  - WiFi (incluida)
  - ESP32MQTTClient
  - EEPROM (incluida)
  - esp_task_wdt (incluida)

#### Broker MQTT
```bash
# Instalar Mosquitto en BeagleBone Black o servidor
sudo apt-get install mosquitto mosquitto-clients
sudo systemctl enable mosquitto
sudo systemctl start mosquitto
```

### Configuración

#### 1. ESP32 (ESP32_ProyectoFinal.ino)
Modificar las siguientes líneas según tu configuración:

```cpp
// WiFi
const char* ssid = "TU_SSID";
const char* password = "TU_PASSWORD";

// MQTT
const char* mqtt_broker = "mqtt://IP_BROKER:1883";

// Combinación por defecto (cambiar según preferencia)
const int COMB_DEFAULT[4] = {1, 2, 2, 1};
```

#### 2. BeagleBone Black (controlDomotica.py)
Ajustar la dirección del broker MQTT:

```python
MQTT_BROKER = "localhost"  # o IP del broker
MQTT_PORT = 1883
```

#### 3. Umbrales de Sensores
Calibrar según tus sensores específicos en `controlDomotica.py`:

```python
UMBRAL_PUERTA = 650  # Valor ADC para puerta abierta
UMBRAL_LUZ = 200     # Valor ADC para oscuridad
```

## 🎮 Uso del Sistema

### Iniciar el Sistema

#### 1. En BeagleBone Black
```bash
# Iniciar broker MQTT (si no está corriendo)
sudo systemctl start mosquitto

# Ejecutar servidor Flask
cd /ruta/al/proyecto
python3 controlDomotica.py
```

El servidor estará disponible en `http://IP_BBB:8080`

#### 2. En ESP32
1. Cargar el firmware `ESP32_ProyectoFinal.ino`
2. Abrir monitor serial (115200 baud)
3. Configurar combinación de seguridad si es necesario
4. El ESP32 se conectará automáticamente a WiFi y MQTT

### Operación del Sistema de Alarma

#### Configurar Combinación (vía Serial)
Al iniciar el ESP32, aparece un menú:
```
1. Mantener combinación actual
2. Cambiar combinación
3. Resetear a combinación por defecto (1-2-2-1)
```

Para cambiar combinación:
- Seleccionar opción 2
- Introducir nueva combinación: `1-2-1-2` o `1,2,1,2`
- La combinación se guarda en EEPROM

#### Activar/Desactivar Alarma
1. Ingresar la combinación correcta usando los pulsadores
2. El sistema alterna entre ACTIVADA y DESACTIVADA
3. Feedback visual mediante LED:
   - **Apagado**: Desactivada
   - **Encendido fijo**: Activada
   - **Parpadeando**: Disparada

#### Estados de la Alarma
- **DESACTIVADA**: Sistema inactivo, entra en sueño tras 30s
- **ACTIVADA**: Monitoreando sensor de movimiento
- **DISPARADA**: Alarma activada por detección de movimiento
- **DURMIENDO**: Modo ahorro de energía (despertar por touch)

### Interfaz Web

#### Control Manual de Luces
1. Hacer clic en una habitación del plano
2. Usar el botón "Encender/Apagar luz" o
3. Ajustar intensidad con el slider (0-100%)

#### Controles Globales
- **Encender todas**: Todas las luces al 100%
- **Apagar todas**: Apaga todas las luces
- **Modo Verano ON/OFF**: Activa/desactiva automatización por luz ambiental

#### Monitoreo
- **Estado de Sensores**: Visualización en tiempo real
- **Estado de Alarma**: Indicador con código de colores
  - Gris: Desactivada
  - Amarillo: Activada
  - Azul: Durmiendo
  - Rojo parpadeante: Disparada

## 🔌 Endpoints API

### Flask (BeagleBone Black)

#### GET `/`
Página principal con interfaz web

#### POST `/set_led`
Control on/off de un LED
```json
{
  "habitacion": "sala",
  "estado": true
}
```

#### POST `/set_intensity`
Control de intensidad de un LED
```json
{
  "habitacion": "sala",
  "intensidad": 75
}
```

#### POST `/set_all`
Control de todas las luces
```json
{
  "intensidad": 100
}
```

#### GET `/leer_sensores`
Lectura de sensores
```json
{
  "puerta_valor": 750,
  "puerta_abierta": true,
  "luz_valor": 150,
  "luz_dia": false,
  "umbrales": {
    "puerta": 650,
    "luz": 200
  }
}
```

#### GET `/estado_alarma`
Estado actual de la alarma
```json
{
  "estado": "ACTIVADA",
  "blinking": false
}
```

### MQTT Topics

#### `alarma/estado`
- **Publisher**: ESP32
- **Subscriber**: BeagleBone Black
- **Mensajes**: `DESACTIVADA`, `ACTIVADA`, `DISPARADA`, `DURMIENDO`
- **Retain**: true (último estado conocido)
- **QoS**: 0

## 🛠️ Solución de Problemas

### ESP32 no se conecta a WiFi
- Verificar SSID y password
- Comprobar que la red es 2.4GHz (ESP32 no soporta 5GHz)
- Revisar monitor serial para mensajes de error

### ESP32 no se conecta a MQTT
- Verificar que el broker MQTT está corriendo
- Comprobar IP y puerto del broker
- Revisar firewall que no bloquee puerto 1883

### LEDs no responden en BeagleBone Black
- Verificar conexiones GPIO
- Comprobar permisos de acceso a GPIO
- Revisar que los pines no estén siendo usados por otro proceso

### Sensores dan lecturas erráticas
- Calibrar umbrales según ambiente específico
- Verificar conexiones analógicas
- Comprobar alimentación estable

### La alarma se dispara sola
- Ajustar `irUmbral` en el código ESP32
- Verificar sensor infrarrojo no esté recibiendo interferencias
- Calibrar en ambiente de instalación real

### Watchdog resetea el ESP32
- Tiempo entre pulsaciones excede 15 segundos
- Aumentar `WDT_TIMEOUT` si es necesario
- Verificar que los pulsadores funcionen correctamente

## 📡 Comunicación MQTT

El sistema utiliza MQTT para sincronizar el estado de la alarma entre ESP32 y BeagleBone Black:

1. ESP32 publica estado cada 5 segundos y al cambiar
2. BeagleBone Black se suscribe y actualiza la interfaz web
3. La interfaz web consulta el estado mediante endpoint HTTP
4. Cuando la alarma se DISPARA, BeagleBone activa parpadeo aleatorio de todas las luces

## 🔐 Seguridad

### Combinación de Alarma
- Almacenada en EEPROM del ESP32
- Configurable vía puerto serial
- Longitud: 2-20 pasos
- Valores permitidos: 1 o 2 (pulsadores)
- Validación con byte mágico (0xA5)

### Watchdog Timer
- Timeout de 15 segundos durante ingreso de combinación
- Previene bloqueos del sistema
- Resetea ESP32 si no se completa a tiempo

### Modo Sueño
- Activación automática tras 30 segundos de inactividad
- Ahorro de energía significativo
- Despertar instantáneo por sensor táctil

## 👥 Contribuciones

Proyecto desarrollado como práctica final de Sistemas Embebidos.

## 📄 Licencia

Este proyecto es de código abierto y está disponible para fines educativos.

## 📞 Soporte

Para problemas, preguntas o sugerencias, por favor abrir un issue en el repositorio de GitHub.

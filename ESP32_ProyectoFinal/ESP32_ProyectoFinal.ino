#include <WiFi.h>
#include <ESP32MQTTClient.h>
#include "esp_sleep.h"
#include "esp_system.h"
#include "driver/rtc_io.h"
#include <EEPROM.h>
#include <esp_task_wdt.h>

// ============================================
// CONFIGURACIÓN WIFI Y MQTT
// ============================================
const char* ssid = "LabMUIIR01";
const char* password = "muiirLab32";

const char* mqtt_broker = "mqtt://192.168.11.100:1883";
const char* mqtt_topic_state = "alarma/estado";

ESP32MQTTClient mqttClient;

// ============================================
// DEFINICIÓN DE PINES
// ============================================
const int PULSADOR_1 = 25;          // Primer pulsador de la combinación
const int PULSADOR_2 = 26;          // Segundo pulsador de la combinación
const int SENSOR_IR = 33;           // Sensor de infrarrojos
const int LED_ESTADO = 2;           // LED indicador de estado
const int TOUCH_PIN = T0;           // Pin táctil para despertar (GPIO4)

// ============================================
// CONFIGURACIÓN EEPROM
// ============================================
#define EEPROM_SIZE 64              // Tamaño de la EEPROM a usar
#define EEPROM_MAGIC_BYTE 0xA5      // Byte mágico para verificar inicialización
#define EEPROM_ADDR_MAGIC 0         // Dirección del byte mágico
#define EEPROM_ADDR_LONGITUD 1      // Dirección de la longitud de combinación
#define EEPROM_ADDR_COMB_START 2    // Dirección de inicio de la combinación

// ============================================
// COMBINACIÓN DE SEGURIDAD
// ============================================
const int MAX_LONGITUD_COMBINACION = 20;  // Longitud máxima permitida
int LONGITUD_COMBINACION = 4;              // Longitud actual (se carga de EEPROM)
int COMBINACION_CORRECTA[MAX_LONGITUD_COMBINACION];  // Se carga de EEPROM
int combinacionActual[MAX_LONGITUD_COMBINACION];
int indiceCombinacion = 0;

// Combinación por defecto (si no hay nada en EEPROM)
const int COMB_DEFAULT_LONGITUD = 4;
const int COMB_DEFAULT[4] = {1, 2, 2, 1};

// ============================================
// ESTADOS DE LA ALARMA
// ============================================
enum EstadoAlarma {
  DESACTIVADA,
  ACTIVADA,
  DISPARADA
};

EstadoAlarma estadoActual = DESACTIVADA;
String estadoAnterior = "";

// ============================================
// CONFIGURACIÓN DEL WATCHDOG
// ============================================
#define WDT_TIMEOUT 15  // 15 segundos de timeout
bool watchdogActivo = false;

// ============================================
// VARIABLES DE CONTROL
// ============================================
unsigned long ultimoEnvioMQTT = 0;
const long intervaloMQTT = 5000;  // Enviar estado cada 5 segundos
bool combinacionIniciada = false;

// Variables para debouncing
unsigned long ultimoDebounce1 = 0;
unsigned long ultimoDebounce2 = 0;
const long tiempoDebounce = 50;
bool ultimoEstado1 = HIGH;
bool ultimoEstado2 = HIGH;
bool estado1Estable = HIGH;
bool estado2Estable = HIGH;
int irUmbral = 2450;

// ============================================
// FUNCIONES DE EEPROM
// ============================================
void inicializarEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  
  byte magicByte = EEPROM.read(EEPROM_ADDR_MAGIC);
  
  if (magicByte != EEPROM_MAGIC_BYTE) {
    // Primera vez o EEPROM corrupta - cargar valores por defecto
    Serial.println(" EEPROM no inicializada - Cargando combinación por defecto");
    guardarCombinacionEnEEPROM(COMB_DEFAULT, COMB_DEFAULT_LONGITUD);
  } else {
    Serial.println("✓ EEPROM ya inicializada");
  }
  
  cargarCombinacionDesdeEEPROM();
}

void guardarCombinacionEnEEPROM(const int* comb, int longitud) {
  if (longitud > MAX_LONGITUD_COMBINACION) {
    Serial.println("✗ Error: Combinación demasiado larga");
    return;
  }
  
  // Escribir byte mágico
  EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC_BYTE);
  
  // Escribir longitud
  EEPROM.write(EEPROM_ADDR_LONGITUD, longitud);
  
  // Escribir combinación
  for (int i = 0; i < longitud; i++) {
    EEPROM.write(EEPROM_ADDR_COMB_START + i, comb[i]);
  }
  
  EEPROM.commit();
  
  Serial.println("✓ Combinación guardada en EEPROM");
  Serial.print("  Longitud: ");
  Serial.println(longitud);
  Serial.print("  Secuencia: ");
  for (int i = 0; i < longitud; i++) {
    Serial.print(comb[i]);
    if (i < longitud - 1) Serial.print("-");
  }
  Serial.println();
}

void cargarCombinacionDesdeEEPROM() {
  // Leer longitud
  LONGITUD_COMBINACION = EEPROM.read(EEPROM_ADDR_LONGITUD);
  
  if (LONGITUD_COMBINACION > MAX_LONGITUD_COMBINACION || LONGITUD_COMBINACION < 1) {
    Serial.println("✗ Error: Longitud inválida en EEPROM, usando default");
    LONGITUD_COMBINACION = COMB_DEFAULT_LONGITUD;
    for (int i = 0; i < LONGITUD_COMBINACION; i++) {
      COMBINACION_CORRECTA[i] = COMB_DEFAULT[i];
    }
    return;
  }
  
  // Leer combinación
  for (int i = 0; i < LONGITUD_COMBINACION; i++) {
    COMBINACION_CORRECTA[i] = EEPROM.read(EEPROM_ADDR_COMB_START + i);
  }
  
  Serial.println("✓ Combinación cargada desde EEPROM:");
  Serial.print("  Longitud: ");
  Serial.println(LONGITUD_COMBINACION);
  Serial.print("  Secuencia: ");
  for (int i = 0; i < LONGITUD_COMBINACION; i++) {
    Serial.print(COMBINACION_CORRECTA[i]);
    if (i < LONGITUD_COMBINACION - 1) Serial.print("-");
  }
  Serial.println();
}

bool parsearNuevaCombinacion(String comando, int* nuevaComb, int* nuevaLongitud) {
  // Formato esperado: "1-2-2-1" o "1,2,2,1"
  comando.trim();
  
  // Reemplazar comas por guiones
  comando.replace(',', '-');
  comando.replace(' ', '-');
  
  int indice = 0;
  int start = 0;
  
  for (int i = 0; i <= comando.length(); i++) {
    if (i == comando.length() || comando[i] == '-') {
      if (i > start) {
        String num = comando.substring(start, i);
        int valor = num.toInt();
        
        if (valor < 1 || valor > 2) {
          Serial.println("✗ Error: Solo se permiten valores 1 o 2");
          return false;
        }
        
        if (indice >= MAX_LONGITUD_COMBINACION) {
          Serial.println("✗ Error: Combinación demasiado larga");
          return false;
        }
        
        nuevaComb[indice++] = valor;
      }
      start = i + 1;
    }
  }
  
  if (indice < 2) {
    Serial.println("✗ Error: La combinación debe tener al menos 2 pasos");
    return false;
  }
  
  *nuevaLongitud = indice;
  return true;
}

// ============================================
// CONFIGURACIÓN SERIAL DE COMBINACIÓN
// ============================================
void menuConfiguracion() {
  Serial.println("\n╔═══════════════════════════════════╗");
  Serial.println("║  MENÚ DE CONFIGURACIÓN            ║");
  Serial.println("╚═══════════════════════════════════╝");
  Serial.println("1. Mantener combinación actual");
  Serial.println("2. Cambiar combinación");
  Serial.println("3. Resetear a combinación por defecto (1-2-2-1)");
  Serial.println("\nSelecciona una opción (1-3):");
  
  // Esperar 10 segundos por respuesta
  unsigned long tiempoInicio = millis();
  const long timeout = 10000;
  
  while (millis() - tiempoInicio < timeout) {
    if (Serial.available() > 0) {
      char opcion = Serial.read();
      
      // Limpiar buffer
      while (Serial.available() > 0) {
        Serial.read();
      }
      
      switch (opcion) {
        case '1':
          Serial.println("\n✓ Manteniendo combinación actual");
          return;
          
        case '2': {
          Serial.println("\n╔═══════════════════════════════════╗");
          Serial.println("║  NUEVA COMBINACIÓN                ║");
          Serial.println("╚═══════════════════════════════════╝");
          Serial.println("Introduce la nueva combinación:");
          Serial.println("Formato: 1-2-2-1 o 1,2,2,1");
          Serial.println("(Solo valores 1 o 2, máximo 20 pasos)");
          Serial.println("Esperando entrada...\n");
          
          // Esperar entrada de combinación
          tiempoInicio = millis();
          String entrada = "";
          
          while (millis() - tiempoInicio < 30000) {  // 30 segundos para introducir
            if (Serial.available() > 0) {
              char c = Serial.read();
              if (c == '\n' || c == '\r') {
                if (entrada.length() > 0) {
                  int nuevaComb[MAX_LONGITUD_COMBINACION];
                  int nuevaLongitud;
                  
                  if (parsearNuevaCombinacion(entrada, nuevaComb, &nuevaLongitud)) {
                    guardarCombinacionEnEEPROM(nuevaComb, nuevaLongitud);
                    cargarCombinacionDesdeEEPROM();
                    Serial.println("✓ Combinación actualizada correctamente\n");
                  } else {
                    Serial. println("✗ Formato inválido.  Manteniendo combinación anterior\n");
                  }
                  return;
                }
              } else {
                entrada += c;
                Serial.print(c);  // Echo
              }
            }
          }
          Serial.println("\n✗ Timeout - Manteniendo combinación actual");
          return;
        }
        case '3':
          Serial.println("\n✓ Reseteando a combinación por defecto (1-2-2-1)");
          guardarCombinacionEnEEPROM(COMB_DEFAULT, COMB_DEFAULT_LONGITUD);
          cargarCombinacionDesdeEEPROM();
          return;
          
        default: 
          Serial.println("\n✗ Opción inválida.  Manteniendo combinación actual");
          return;
      }
    }
  }
  
  Serial.println("\n Timeout - Manteniendo combinación actual");
}

// ============================================
// FUNCIONES DEL WATCHDOG (esp_task_wdt.h)
// ============================================
void iniciarWatchdog() {
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };

  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);  // Tarea loop()

  watchdogActivo = true;
  Serial.println(" Watchdog iniciado (Task WDT, " + String(WDT_TIMEOUT) + "s)");
}

void resetearWatchdog() {
  if (watchdogActivo) {
    esp_task_wdt_reset();  // Resetear el watchdog
  }
}

void detenerWatchdog() {
  if (watchdogActivo) {
    esp_task_wdt_delete(NULL);  // Eliminar la tarea del watchdog
    watchdogActivo = false;
    Serial. println(" Watchdog detenido");
  }
}

// ============================================
// FUNCIONES DE CONEXIÓN
// ============================================
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a WiFi:  ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  Serial.print("Dirección IP: ");
  Serial.println(WiFi.localIP());
}

// Required global callback for connection events
void onMqttConnect(esp_mqtt_client_handle_t client) {
  if (mqttClient. isMyTurn(client)) {
    Serial.println("✓ MQTT conectado!");
  }
}

// Required global event handler
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
esp_err_t handleMQTT(esp_mqtt_event_handle_t event) {
  mqttClient.onEventCallback(event);
  return ESP_OK;
}
#else
void handleMQTT(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
  auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
  mqttClient.onEventCallback(event);
}
#endif

// ============================================
// FUNCIONES DE PUBLICACIÓN MQTT
// ============================================
void publicarEstado() {
  String estado = "";
  switch (estadoActual) {
    case DESACTIVADA:  
      estado = "DESACTIVADA";
      break;
    case ACTIVADA:  
      estado = "ACTIVADA";
      break;
    case DISPARADA:  
      estado = "DISPARADA";
      break;
  }
  
  if (estado != estadoAnterior) {
    mqttClient.publish(mqtt_topic_state, estado.c_str(), 0, true);
    Serial.print("Estado publicado: ");
    Serial.println(estado);
    estadoAnterior = estado;
  }
}

void publicarEstadoPeriodico() {
  unsigned long ahora = millis();
  if (ahora - ultimoEnvioMQTT >= intervaloMQTT) {
    ultimoEnvioMQTT = ahora;
    String estado = "";
    switch (estadoActual) {
      case DESACTIVADA:  
        estado = "DESACTIVADA";
        break;
      case ACTIVADA:  
        estado = "ACTIVADA";
        break;
      case DISPARADA: 
        estado = "DISPARADA";
        break;
    }
    mqttClient.publish(mqtt_topic_state, estado.c_str(), 0, false);
  }
}

// ============================================
// FUNCIONES DE CONTROL DE ALARMA
// ============================================
void activarAlarma() {
  if (estadoActual == DESACTIVADA) {
    estadoActual = ACTIVADA;
    Serial.println("╔════════════════════════════╗");
    Serial.println("║   ALARMA ACTIVADA ✓        ║");
    Serial.println("╚════════════════════════════╝");
    publicarEstado();
    
    // Parpadeo rápido para indicar activación
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_ESTADO, HIGH);
      delay(100);
      digitalWrite(LED_ESTADO, LOW);
      delay(100);
    }
    digitalWrite(LED_ESTADO, HIGH);
  }
}

void desactivarAlarma() {
  EstadoAlarma estadoPrevio = estadoActual;
  estadoActual = DESACTIVADA;
  
  if (estadoPrevio == DISPARADA) {
    Serial.println("╔════════════════════════════╗");
    Serial.println("║ ALARMA DESACTIVADA         ║");
    Serial.println("║ (estaba disparada)         ║");
    Serial.println("╚════════════════════════════╝");
  } else {
    Serial.println("╔════════════════════════════╗");
    Serial.println("║ ALARMA DESACTIVADA ✓       ║");
    Serial.println("╚════════════════════════════╝");
  }
  
  digitalWrite(LED_ESTADO, LOW);
  publicarEstado();
}

void dispararAlarma() {
  if (estadoActual == ACTIVADA) {
    estadoActual = DISPARADA;
    Serial.println("╔════════════════════════════╗");
    Serial.println("║ ⚠️  ALARMA DISPARADA ⚠️     ║");
    Serial.println("║ ¡INTRUSO DETECTADO!        ║");
    Serial.println("╚════════════════════════════╝");
    publicarEstado();
  }
}

// ============================================
// FUNCIONES DE GESTIÓN DE COMBINACIÓN
// ============================================
void resetearCombinacion() {
  for (int i = 0; i < MAX_LONGITUD_COMBINACION; i++) {
    combinacionActual[i] = 0;
  }
  indiceCombinacion = 0;
  combinacionIniciada = false;
  detenerWatchdog();
  Serial.println("Combinación reseteada");
}

void mostrarProgresoCombinacion() {
  Serial.print("Progreso: [");
  for (int i = 0; i < LONGITUD_COMBINACION; i++) {
    if (i < indiceCombinacion) {
      Serial.print(combinacionActual[i]);
    } else {
      Serial.print("_");
    }
    if (i < LONGITUD_COMBINACION - 1) Serial.print("-");
  }
  Serial.print("] (");
  Serial.print(indiceCombinacion);
  Serial.print("/");
  Serial.print(LONGITUD_COMBINACION);
  Serial.println(")");
}

bool verificarCombinacionCorrecta() {
  for (int i = 0; i < LONGITUD_COMBINACION; i++) {
    if (combinacionActual[i] != COMBINACION_CORRECTA[i]) {
      return false;
    }
  }
  return true;
}

void registrarPulsacion(int boton) {
  if (!combinacionIniciada) {
    combinacionIniciada = true;
    iniciarWatchdog();
    Serial.println("\n╔════════════════════════════╗");
    Serial.println("║ Combinación iniciada       ║");
    Serial.print("║ Esperando:  ");
    for (int i = 0; i < LONGITUD_COMBINACION; i++) {
      Serial.print(COMBINACION_CORRECTA[i]);
      if (i < LONGITUD_COMBINACION - 1) Serial.print("-");
    }
    for (int i = 0; i < (13 - LONGITUD_COMBINACION * 2); i++) {
      Serial.print(" ");
    }
    Serial.println("║");
    Serial.println("╚════════════════════════════╝");
  }
  
  resetearWatchdog();
  
  if (indiceCombinacion < LONGITUD_COMBINACION) {
    combinacionActual[indiceCombinacion] = boton;
    indiceCombinacion++;
    
    Serial.print("Pulsador ");
    Serial.print(boton);
    Serial.println(" presionado");
    mostrarProgresoCombinacion();
    
    // Feedback visual
    digitalWrite(LED_ESTADO, HIGH);
    delay(50);
    if (estadoActual == DESACTIVADA) {
      digitalWrite(LED_ESTADO, LOW);
    }
    
    // Verificar si se completó la combinación
    if (indiceCombinacion == LONGITUD_COMBINACION) {
      detenerWatchdog();
      
      if (verificarCombinacionCorrecta()) {
        Serial.println("\n✓ ¡Combinación CORRECTA!");
        
        // Alternar estado de la alarma
        if (estadoActual == DESACTIVADA) {
          activarAlarma();
        } else {
          desactivarAlarma();
        }
      } else {
        Serial.println("\n✗ Combinación INCORRECTA");
        Serial.print("Esperado: ");
        for (int i = 0; i < LONGITUD_COMBINACION; i++) {
          Serial.print(COMBINACION_CORRECTA[i]);
          if (i < LONGITUD_COMBINACION - 1) Serial.print("-");
        }
        Serial.println();
        
        // Parpadeo de error
        for (int i = 0; i < 5; i++) {
          digitalWrite(LED_ESTADO, HIGH);
          delay(100);
          digitalWrite(LED_ESTADO, LOW);
          delay(100);
        }
      }
      
      resetearCombinacion();
    }
  }
}

// ============================================
// FUNCIÓN DE LECTURA DE PULSADORES
// ============================================
void leerPulsadores() {
  bool lectura1 = digitalRead(PULSADOR_1);
  bool lectura2 = digitalRead(PULSADOR_2);
  
  // Debouncing pulsador 1
  if (lectura1 != ultimoEstado1) {
    ultimoDebounce1 = millis();
  }
  
  if ((millis() - ultimoDebounce1) > tiempoDebounce) {
    if (lectura1 != estado1Estable) {
      estado1Estable = lectura1;
      
      if (estado1Estable == LOW) {
        registrarPulsacion(1);
      }
    }
  }
  
  ultimoEstado1 = lectura1;
  
  // Debouncing pulsador 2
  if (lectura2 != ultimoEstado2) {
    ultimoDebounce2 = millis();
  }
  
  if ((millis() - ultimoDebounce2) > tiempoDebounce) {
    if (lectura2 != estado2Estable) {
      estado2Estable = lectura2;
      
      if (estado2Estable == LOW) {
        registrarPulsacion(2);
      }
    }
  }
  
  ultimoEstado2 = lectura2;
}

// ============================================
// FUNCIÓN DE VERIFICACIÓN DEL SENSOR IR
// ============================================
void verificarSensorIR() {
  int lectura = analogRead(SENSOR_IR);

  if (lectura > irUmbral) {
    Serial.print("Movimiento detectado IR = ");
    Serial.println(lectura);
    dispararAlarma();
  }
}

// ============================================
// FUNCIÓN DE MODO SUEÑO
// ============================================
void entrarEnModoSueño() {
  Serial.println("\n╔════════════════════════════╗");
  Serial.println("║ Entrando en modo sueño...  ║");
  Serial.println("║ Toca el sensor para        ║");
  Serial.println("║ despertar                  ║");
  Serial.println("╚════════════════════════════╝");
  delay(100);

  // Configurar wakeup por touch (SIN attachInterrupt)
  esp_sleep_enable_touchpad_wakeup();

  // Opcional: umbral automático
  touchSleepWakeUpEnable(T0, 40);

  rtc_gpio_hold_en((gpio_num_t)LED_ESTADO);

  mqttClient.publish(mqtt_topic_state, "DURMIENDO", 0, true);
  delay(100);

  WiFi.disconnect(true);
  delay(100);

  esp_deep_sleep_start();
}

// ============================================
// INDICACIÓN VISUAL DEL ESTADO
// ============================================
void actualizarLED() {
  static unsigned long ultimoParpadeo = 0;
  static bool estadoLED = false;
  
  if (combinacionIniciada) {
    return;
  }
  
  switch (estadoActual) {
    case DESACTIVADA:  
      digitalWrite(LED_ESTADO, LOW);
      break;
      
    case ACTIVADA:  
      digitalWrite(LED_ESTADO, HIGH);
      break;
      
    case DISPARADA:     
      if (millis() - ultimoParpadeo > 200) {
        ultimoParpadeo = millis();
        estadoLED = ! estadoLED;
        digitalWrite(LED_ESTADO, estadoLED);
      }
      break;
  }
}

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("╔═══════════════════════════════════╗");
  Serial.println("║  SISTEMA DE ALARMA ESP32          ║");
  Serial.println("╚═══════════════════════════════════╝");
  
  // Inicializar EEPROM y cargar combinación
  inicializarEEPROM();
  
  // Mostrar menú de configuración
  menuConfiguracion();
  
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD) {
    Serial.println("\n✓ Despertado por sensor táctil");
  } else {
    Serial.println("\n✓ Inicio normal del sistema");
  }
  
  pinMode(PULSADOR_1, INPUT_PULLUP);
  pinMode(PULSADOR_2, INPUT_PULLUP);
  pinMode(SENSOR_IR, INPUT);
  pinMode(LED_ESTADO, OUTPUT);
  
  digitalWrite(LED_ESTADO, LOW);

  analogReadResolution(12);          // 0–4095
  analogSetAttenuation(ADC_11db);    // hasta ~3.3V
  
  setup_wifi();
  
  mqttClient.setURI(mqtt_broker);
  mqttClient.loopStart();
  
  Serial.println("\n╔═══════════════════════════════════╗");
  Serial.println("║ Sistema iniciado correctamente    ║");
  Serial.print("║ Combinación (");
  Serial.print(LONGITUD_COMBINACION);
  Serial.print(" pasos): ");
  for (int i = 0; i < LONGITUD_COMBINACION; i++) {
    Serial.print(COMBINACION_CORRECTA[i]);
    if (i < LONGITUD_COMBINACION - 1) Serial.print("-");
  }
  for (int i = 0; i < (13 - LONGITUD_COMBINACION * 2); i++) {
    Serial.print(" ");
  }
  Serial.println("║");
  Serial.println("║ Watchdog:  Task WDT 15s            ║");
  Serial.println("╚═══════════════════════════════════╝\n");
}

// ============================================
// LOOP PRINCIPAL
// ============================================
void loop() {
  static unsigned long tiempoDesactivada = 0;
  const long tiempoAntesDeEstado = 30000;
  
  if (estadoActual == DESACTIVADA && ! combinacionIniciada) {
    if (tiempoDesactivada == 0) {
      tiempoDesactivada = millis();
    }
    
    if (millis() - tiempoDesactivada > tiempoAntesDeEstado) {
      entrarEnModoSueño();
    }
  } else {
    tiempoDesactivada = 0;
  }
  
  leerPulsadores();
  
  if (estadoActual == ACTIVADA) {
    verificarSensorIR();
  }
  
  actualizarLED();
  publicarEstadoPeriodico();
  
  delay(10);
}
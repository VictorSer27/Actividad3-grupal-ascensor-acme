#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <IRremote.hpp>
#include "DHT.h"
#include <math.h>

// -------------------------
// Pines principales
// -------------------------
#define PIR_PIN 2
#define IR_PIN 3
#define SERVO_PIN 9

// -------------------------
// Sensores ambientales
// -------------------------
#define DHTPIN 10
#define DHTTYPE DHT22
#define LDR_PIN A0

// -------------------------
// Actuadores ambientales
// -------------------------
#define LED_CALOR 11
#define LED_FRIO 12
#define LED_LUZ A1

// -------------------------
// Pulsadores de llamada
// -------------------------
const int pinesBoton[5] = {4, 5, 6, 7, 8};

// -------------------------
// Dispositivos
// -------------------------
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo ascensor;
DHT dht(DHTPIN, DHTTYPE);

// -------------------------
// Variables del ascensor
// -------------------------
int plantaActual = 0;
int plantaDestino = 0;
bool enMovimiento = false;

// Cada planta se representa mediante una posición angular del servo
const int angulosPlanta[5] = {0, 45, 90, 135, 180};

// -------------------------
// Variables ambientales
// -------------------------
float temperatura = 0.0;
float humedad = 0.0;
float lux = 0.0;
int luzPorcentaje = 0;

// -------------------------
// Presencia en cabina
// -------------------------
bool presenciaUsuario = false;
bool presenciaAnterior = false;

// -------------------------
// Constantes del LDR
// -------------------------
const float GAMMA = 0.7;
const float RL10 = 50.0;

// -------------------------
// Setpoints ajustables por mando IR
// -------------------------
float tempDeseada = 25.0;
int luzDeseada = 80;

// Zonas muertas de control
const float ZONA_TEMP = 2.0;
const int ZONA_LUZ = 5;

// Límites de seguridad para evitar valores poco razonables
const float TEMP_SET_MIN = 18.0;
const float TEMP_SET_MAX = 30.0;

const int LUZ_SET_MIN = 30;
const int LUZ_SET_MAX = 100;

// -------------------------
// Estados de control
// -------------------------
String estadoTemp = "OK";
String estadoLuz = "OFF";

// -------------------------
// Control de tiempos
// -------------------------
unsigned long tiempoUltimaLectura = 0;
unsigned long tiempoUltimoCambioPantalla = 0;

const unsigned long INTERVALO_LECTURA = 10000;   // Registro ambiental cada 10 s
const unsigned long INTERVALO_PANTALLA = 2500;   // Cambio de pantalla cada 2,5 s

// 0: ascensor, 1: ambiente, 2: control/presencia, 3: setpoints
int pantallaActual = 0;

// Variables para evitar parpadeo en el LCD
String linea1Anterior = "";
String linea2Anterior = "";

// --------------------------------------------------
// Escribe una línea completa del LCD solo si ha cambiado.
// Evita usar lcd.clear() continuamente y reduce el parpadeo.
// --------------------------------------------------
void escribirLineaLCD(int fila, String texto) {
  if (texto.length() > 16) {
    texto = texto.substring(0, 16);
  }

  while (texto.length() < 16) {
    texto += " ";
  }

  if (fila == 0 && texto != linea1Anterior) {
    lcd.setCursor(0, 0);
    lcd.print(texto);
    linea1Anterior = texto;
  }

  if (fila == 1 && texto != linea2Anterior) {
    lcd.setCursor(0, 1);
    lcd.print(texto);
    linea2Anterior = texto;
  }
}

// --------------------------------------------------
// Pantalla de operación del ascensor.
// --------------------------------------------------
void mostrarEstadoAscensor(String estado) {
  String linea1 = "P actual:" + String(plantaActual);
  String linea2 = "Destino:" + String(plantaDestino) + " " + estado;

  escribirLineaLCD(0, linea1);
  escribirLineaLCD(1, linea2);
}

// --------------------------------------------------
// Pantalla de variables ambientales medidas.
// --------------------------------------------------
void mostrarEstadoAmbiental() {
  String linea1 = "T:" + String(temperatura, 1) + "C H:" + String(humedad, 0) + "%";
  String linea2 = "Luz:" + String(luzPorcentaje) + "%";

  escribirLineaLCD(0, linea1);
  escribirLineaLCD(1, linea2);
}

// --------------------------------------------------
// Pantalla de actuación y presencia.
// --------------------------------------------------
void mostrarEstadoControl() {
  String linea1 = "Temp:" + estadoTemp;
  String linea2 = "Luz:" + estadoLuz + " P:";

  if (presenciaUsuario) {
    linea2 += "SI";
  } else {
    linea2 += "NO";
  }

  escribirLineaLCD(0, linea1);
  escribirLineaLCD(1, linea2);
}

// --------------------------------------------------
// Nueva pantalla de setpoints ajustables.
// --------------------------------------------------
void mostrarSetpoints() {
  String linea1 = "Set T:" + String(tempDeseada, 1) + "C";
  String linea2 = "Set Luz:" + String(luzDeseada) + "%";

  escribirLineaLCD(0, linea1);
  escribirLineaLCD(1, linea2);
}

// --------------------------------------------------
// Calcula iluminación aproximada en lux a partir del LDR.
// --------------------------------------------------
float leerLux() {
  int analogValue = analogRead(LDR_PIN);
  float voltage = analogValue / 1024.0 * 5.0;

  if (voltage <= 0.0) {
    return 100000.0;
  }

  if (voltage >= 4.999) {
    return 0.1;
  }

  float resistance = 2000.0 * voltage / (1.0 - voltage / 5.0);
  float luxCalculado = pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, (1.0 / GAMMA));

  if (!isfinite(luxCalculado)) {
    return 100000.0;
  }

  return luxCalculado;
}

// --------------------------------------------------
// Convierte lux aproximados a porcentaje para simplificar el control.
// --------------------------------------------------
int calcularPorcentajeLuz(float luxMedida) {
  int porcentaje = map((int)luxMedida, 0, 1000, 0, 100);

  if (porcentaje < 0) {
    porcentaje = 0;
  }

  if (porcentaje > 100) {
    porcentaje = 100;
  }

  return porcentaje;
}

// --------------------------------------------------
// Lee temperatura, humedad e iluminación.
// --------------------------------------------------
void leerSensoresAmbientales() {
  float nuevaTemp = dht.readTemperature();
  float nuevaHum = dht.readHumidity();

  if (!isnan(nuevaTemp) && !isnan(nuevaHum)) {
    temperatura = nuevaTemp;
    humedad = nuevaHum;
  }

  lux = leerLux();
  luzPorcentaje = calcularPorcentajeLuz(lux);
}

// --------------------------------------------------
// Lee presencia de usuario en cabina mediante el sensor PIR.
// --------------------------------------------------
void leerPresencia() {
  presenciaUsuario = digitalRead(PIR_PIN) == HIGH;

  if (presenciaUsuario != presenciaAnterior) {
    if (presenciaUsuario) {
      Serial.println("Presencia detectada en cabina");
    } else {
      Serial.println("Cabina sin presencia");
    }

    presenciaAnterior = presenciaUsuario;
  }
}

// --------------------------------------------------
// Control ON-OFF con zona muerta para temperatura.
// El setpoint de temperatura se puede modificar por IR.
// --------------------------------------------------
void controlarTemperatura() {
  if (temperatura < tempDeseada - ZONA_TEMP) {
    digitalWrite(LED_CALOR, HIGH);
    digitalWrite(LED_FRIO, LOW);
    estadoTemp = "CALOR";
  } 
  else if (temperatura > tempDeseada + ZONA_TEMP) {
    digitalWrite(LED_CALOR, LOW);
    digitalWrite(LED_FRIO, HIGH);
    estadoTemp = "FRIO";
  } 
  else {
    digitalWrite(LED_CALOR, LOW);
    digitalWrite(LED_FRIO, LOW);
    estadoTemp = "OK";
  }
}

// --------------------------------------------------
// Control ON-OFF de iluminación artificial.
// El setpoint de iluminación se puede modificar por IR.
// --------------------------------------------------
void controlarIluminacion() {
  if (luzPorcentaje < luzDeseada - ZONA_LUZ) {
    digitalWrite(LED_LUZ, HIGH);
    estadoLuz = "ON";
  } else {
    digitalWrite(LED_LUZ, LOW);
    estadoLuz = "OFF";
  }
}

// --------------------------------------------------
// Ejecuta todas las acciones de control ambiental.
// --------------------------------------------------
void controlarAmbiente() {
  controlarTemperatura();
  controlarIluminacion();
}

// --------------------------------------------------
// Fuerza la pantalla de setpoints tras un ajuste remoto.
// --------------------------------------------------
void mostrarPantallaSetpointsAhora() {
  pantallaActual = 3;
  tiempoUltimoCambioPantalla = millis();
  mostrarSetpoints();
}

// --------------------------------------------------
// Ajusta remotamente el setpoint de temperatura.
// --------------------------------------------------
void ajustarSetpointTemperatura(float incremento) {
  tempDeseada += incremento;

  if (tempDeseada < TEMP_SET_MIN) {
    tempDeseada = TEMP_SET_MIN;
  }

  if (tempDeseada > TEMP_SET_MAX) {
    tempDeseada = TEMP_SET_MAX;
  }

  controlarAmbiente();
  mostrarPantallaSetpointsAhora();

  Serial.print("Nuevo setpoint temperatura: ");
  Serial.print(tempDeseada, 1);
  Serial.println(" C");
}

// --------------------------------------------------
// Ajusta remotamente el setpoint de iluminación.
// --------------------------------------------------
void ajustarSetpointLuz(int incremento) {
  luzDeseada += incremento;

  if (luzDeseada < LUZ_SET_MIN) {
    luzDeseada = LUZ_SET_MIN;
  }

  if (luzDeseada > LUZ_SET_MAX) {
    luzDeseada = LUZ_SET_MAX;
  }

  controlarAmbiente();
  mostrarPantallaSetpointsAhora();

  Serial.print("Nuevo setpoint luz: ");
  Serial.print(luzDeseada);
  Serial.println(" %");
}

// --------------------------------------------------
// Mueve la cabina a la planta seleccionada.
// Durante el movimiento se prioriza la pantalla del ascensor.
// --------------------------------------------------
void moverAPlanta(int destino) {
  if (destino < 0 || destino > 4) {
    return;
  }

  plantaDestino = destino;

  if (plantaDestino == plantaActual) {
    pantallaActual = 0;
    tiempoUltimoCambioPantalla = millis();

    mostrarEstadoAscensor("Aqui");

    Serial.print("Ascensor ya esta en planta ");
    Serial.println(plantaActual);

    return;
  }

  enMovimiento = true;

  pantallaActual = 0;
  tiempoUltimoCambioPantalla = millis();

  mostrarEstadoAscensor("Mov");

  Serial.print("Llamada recibida. Planta destino: ");
  Serial.println(plantaDestino);

  ascensor.write(angulosPlanta[plantaDestino]);

  delay(1000);

  plantaActual = plantaDestino;
  enMovimiento = false;

  pantallaActual = 0;
  tiempoUltimoCambioPantalla = millis();

  mostrarEstadoAscensor("Parado");

  Serial.print("Ascensor detenido en planta ");
  Serial.println(plantaActual);
}

// --------------------------------------------------
// Interpreta teclas numéricas del mando IR como plantas.
// --------------------------------------------------
int interpretarPlantaIR(uint8_t comando) {
  switch (comando) {
    case 104: return 0;  // Tecla 0
    case 48:  return 1;  // Tecla 1
    case 24:  return 2;  // Tecla 2
    case 122: return 3;  // Tecla 3
    case 16:  return 4;  // Tecla 4
    default:  return -1;
  }
}

// --------------------------------------------------
// Gestiona todas las órdenes recibidas por mando IR.
// Mantiene las teclas 0-4 para plantas y añade control remoto de setpoints.
// --------------------------------------------------
void gestionarComandoIR(uint8_t comando) {
  int plantaSeleccionada = interpretarPlantaIR(comando);

  if (plantaSeleccionada != -1) {
    moverAPlanta(plantaSeleccionada);
    return;
  }

  switch (comando) {
    case 2:     // Tecla +
      ajustarSetpointTemperatura(1.0);
      break;

    case 152:   // Tecla -
      ajustarSetpointTemperatura(-1.0);
      break;

    case 144:   // Tecla Next
      ajustarSetpointLuz(5);
      break;

    case 224:   // Tecla Previous
      ajustarSetpointLuz(-5);
      break;

    case 226:   // Tecla Menu
      mostrarPantallaSetpointsAhora();
      Serial.println("Pantalla de setpoints solicitada");
      break;

    default:
      Serial.println("Tecla IR sin funcion asignada");
      break;
  }
}

// --------------------------------------------------
// Lee los pulsadores de llamada de cada planta.
// --------------------------------------------------
void leerPulsadores() {
  for (int i = 0; i < 5; i++) {
    if (digitalRead(pinesBoton[i]) == LOW) {
      Serial.print("Pulsador de planta ");
      Serial.print(i);
      Serial.println(" activado");

      moverAPlanta(i);

      delay(300);
    }
  }
}

// --------------------------------------------------
// Lee el mando infrarrojo.
// --------------------------------------------------
void leerMandoIR() {
  if (IrReceiver.decode()) {
    uint8_t comando = IrReceiver.decodedIRData.command;

    Serial.print("Comando IR recibido: ");
    Serial.println(comando);

    gestionarComandoIR(comando);

    IrReceiver.resume();
  }
}

// --------------------------------------------------
// Actualiza el LCD.
// Si el ascensor está en movimiento, se prioriza la pantalla de operación.
// Si está parado, se alternan las pantallas disponibles.
// --------------------------------------------------
void actualizarPantalla() {
  if (enMovimiento) {
    pantallaActual = 0;
    mostrarEstadoAscensor("Mov");
    return;
  }

  if (millis() - tiempoUltimoCambioPantalla >= INTERVALO_PANTALLA) {
    tiempoUltimoCambioPantalla = millis();
    pantallaActual++;

    if (pantallaActual > 3) {
      pantallaActual = 0;
    }
  }

  if (pantallaActual == 0) {
    mostrarEstadoAscensor("Parado");
  } 
  else if (pantallaActual == 1) {
    mostrarEstadoAmbiental();
  } 
  else if (pantallaActual == 2) {
    mostrarEstadoControl();
  } 
  else {
    mostrarSetpoints();
  }
}

// --------------------------------------------------
// SETUP
// --------------------------------------------------
void setup() {
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();

  ascensor.attach(SERVO_PIN);
  ascensor.write(angulosPlanta[plantaActual]);

  // Se desactiva el feedback LED del receptor IR para evitar interferencias visuales
  IrReceiver.begin(IR_PIN, false);

  for (int i = 0; i < 5; i++) {
    pinMode(pinesBoton[i], INPUT_PULLUP);
  }

  pinMode(PIR_PIN, INPUT);

  pinMode(LED_CALOR, OUTPUT);
  pinMode(LED_FRIO, OUTPUT);
  pinMode(LED_LUZ, OUTPUT);

  digitalWrite(LED_CALOR, LOW);
  digitalWrite(LED_FRIO, LOW);
  digitalWrite(LED_LUZ, LOW);

  dht.begin();

  leerSensoresAmbientales();
  leerPresencia();
  controlarAmbiente();

  Serial.println("Sistema de ascensor iniciado");
  Serial.println("Teclas 0-4: plantas");
  Serial.println("+/-: setpoint temperatura");
  Serial.println("Next/Prev: setpoint luz");
  Serial.println("Menu: pantalla setpoints");

  mostrarEstadoAscensor("Parado");
}

// --------------------------------------------------
// LOOP PRINCIPAL
// --------------------------------------------------
void loop() {
  leerMandoIR();
  leerPulsadores();
  leerPresencia();

  if (millis() - tiempoUltimaLectura >= INTERVALO_LECTURA) {
    tiempoUltimaLectura = millis();

    leerSensoresAmbientales();
    controlarAmbiente();

    Serial.print("Ambiente -> Temp: ");
    Serial.print(temperatura, 1);
    Serial.print(" C | Tset: ");
    Serial.print(tempDeseada, 1);
    Serial.print(" C | Humedad: ");
    Serial.print(humedad, 1);
    Serial.print(" % | Luz: ");
    Serial.print(luzPorcentaje);
    Serial.print(" % | Lset: ");
    Serial.print(luzDeseada);
    Serial.print(" % | Presencia: ");

    if (presenciaUsuario) {
      Serial.print("SI");
    } else {
      Serial.print("NO");
    }

    Serial.print(" | Control temp: ");
    Serial.print(estadoTemp);
    Serial.print(" | Luz artificial: ");
    Serial.println(estadoLuz);
  }

  actualizarPantalla();
}
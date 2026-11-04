// =============================================================
// Paso 10 (version simplificada) - El ESP32 lee el color y la Pi lo muestra
// Placa: ESP32 DEVKIT V1 (HW-463)  |  Core esp32 3.3.12
// Bus A (Wire,  ESCLAVO 0x42): SDA = GPIO21, SCL = GPIO22
// Bus B (Wire1, MAESTRO):      SDA = GPIO32, SCL = GPIO33, INT = GPIO27
// Protocolo (paso 9): comando 6 bytes -> respuesta 14 bytes, CRC-8 (polinomio 0x07)
// Comandos implementados: PING (0x01) y LEER_COLOR (0x03)
// =============================================================

#include <Wire.h>

// ===================== Pines y bus =====================
const int PIN_SDA_A = 21;
const int PIN_SCL_A = 22;
const int PIN_SDA_B = 32;
const int PIN_SCL_B = 33;
const int PIN_INT   = 27;
const uint32_t FRECUENCIA_I2C = 100000;

// ===================== Sensor APDS-9960 (igual que el paso 7) =====================
const uint8_t APDS_DIRECCION = 0x39;
const uint8_t REG_ENABLE  = 0x80;
const uint8_t REG_ATIME   = 0x81;
const uint8_t REG_AILTL   = 0x84;
const uint8_t REG_AILTH   = 0x85;
const uint8_t REG_AIHTL   = 0x86;
const uint8_t REG_AIHTH   = 0x87;
const uint8_t REG_PERS    = 0x8C;
const uint8_t REG_CONTROL = 0x8F;
const uint8_t REG_ID      = 0x92;
const uint8_t REG_CDATAL  = 0x94;
const uint8_t REG_CICLEAR = 0xE6;

const uint8_t ENABLE_APAGADO       = 0x00;
const uint8_t ENABLE_COLOR_CON_INT = 0x13;  // AIEN | AEN | PON
const uint8_t ATIME_103MS          = 0xDB;
const uint8_t GANANCIA_4X          = 0x01;
const uint8_t PERS_CADA_CICLO      = 0x00;

// Si no llega interrupcion en este tiempo, se lee y se limpia INT igualmente
const unsigned long TIEMPO_MAXIMO_SIN_DATO_MS = 500;

// ===================== Protocolo (paso 9) =====================
const uint8_t DIRECCION_ESCLAVO  = 0x42;
const uint8_t LONGITUD_COMANDO   = 6;
const uint8_t LONGITUD_RESPUESTA = 14;
const uint8_t LONGITUD_DATOS     = 10;
const uint8_t VERSION_PROTOCOLO  = 1;
const uint8_t VERSION_FIRMWARE   = 2;   // 2 = version simplificada

const uint8_t CMD_PING       = 0x01;
const uint8_t CMD_LEER_COLOR = 0x03;

const uint8_t RES_OK          = 0x00;
const uint8_t RES_ERR_COMANDO = 0x01;
const uint8_t RES_ERR_CRC     = 0x04;

// ===================== Clasificacion de color =====================
const uint8_t COLOR_NINGUNO = 0;
const uint8_t COLOR_ROJO    = 1;
const uint8_t COLOR_VERDE   = 2;
const uint8_t COLOR_AZUL    = 3;

const uint32_t SUMA_MINIMA_RGB      = 60;   // Menos luz que esto: no se decide
const uint32_t PORCENTAJE_DOMINANTE = 45;   // % minimo para que un canal domine

// ===================== Variables compartidas con ISR y callback =====================
volatile bool datoSensorListo = false;
volatile bool hayComandoNuevo = false;
volatile uint8_t bufferComando[LONGITUD_COMANDO];
volatile uint8_t longitudComando = 0;

// ===================== Ultima lectura (solo la modifica loop) =====================
uint16_t clearCrudo = 0, rojoCrudo = 0, verdeCrudo = 0, azulCrudo = 0;
uint8_t colorDetectado = COLOR_NINGUNO;
uint8_t numeroLectura = 0;
unsigned long ultimoDatoMs = 0;
uint8_t idSensor = 0;

// =============================================================
// Interrupcion del sensor y callback del bus A
// =============================================================
void IRAM_ATTR isrSensor() {
  datoSensorListo = true;
}

void alRecibirComando(int cantidad) {
  if (cantidad < 1) {               // Escritura vacia (i2cdetect): ignorar
    return;
  }
  uint8_t i = 0;
  while (Wire.available()) {
    uint8_t byteLeido = Wire.read();
    if (i < LONGITUD_COMANDO) {
      bufferComando[i] = byteLeido;
    }
    i++;
  }
  longitudComando = i;
  hayComandoNuevo = true;
}

// =============================================================
// Funciones del sensor (bus B) - iguales a las del paso 7
// =============================================================
bool escribirRegistroSensor(uint8_t registro, uint8_t valor) {
  Wire1.beginTransmission(APDS_DIRECCION);
  Wire1.write(registro);
  Wire1.write(valor);
  return Wire1.endTransmission() == 0;
}

bool leerRegistrosSensor(uint8_t registro, uint8_t *destino, uint8_t cantidad) {
  Wire1.beginTransmission(APDS_DIRECCION);
  Wire1.write(registro);
  if (Wire1.endTransmission(false) != 0) {
    return false;
  }
  if (Wire1.requestFrom(APDS_DIRECCION, cantidad) != cantidad) {
    return false;
  }
  for (uint8_t i = 0; i < cantidad; i++) {
    destino[i] = Wire1.read();
  }
  return true;
}

bool limpiarInterrupcionSensor() {
  Wire1.beginTransmission(APDS_DIRECCION);
  Wire1.write(REG_CICLEAR);
  return Wire1.endTransmission() == 0;
}

void detenerConError(const char *mensaje) {
  Serial.println(mensaje);
  while (true) {
    delay(1000);
  }
}

void iniciarSensor() {
  if (!Wire1.begin(PIN_SDA_B, PIN_SCL_B, FRECUENCIA_I2C)) {
    detenerConError("ERROR: no se pudo iniciar Wire1.");
  }
  if (!leerRegistrosSensor(REG_ID, &idSensor, 1)) {
    detenerConError("ERROR: el sensor no responde (NACK) en el bus B.");
  }
  Serial.print("ID del sensor: 0x");
  Serial.println(idSensor, HEX);

  bool configuracionOk =
      escribirRegistroSensor(REG_ENABLE,  ENABLE_APAGADO) &&
      escribirRegistroSensor(REG_ATIME,   ATIME_103MS) &&
      escribirRegistroSensor(REG_CONTROL, GANANCIA_4X) &&
      escribirRegistroSensor(REG_PERS,    PERS_CADA_CICLO) &&
      escribirRegistroSensor(REG_AILTL,   0xFF) &&
      escribirRegistroSensor(REG_AILTH,   0xFF) &&
      escribirRegistroSensor(REG_AIHTL,   0x00) &&
      escribirRegistroSensor(REG_AIHTH,   0x00);
  if (!configuracionOk) {
    detenerConError("ERROR: fallo al configurar el sensor.");
  }

  pinMode(PIN_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_INT), isrSensor, FALLING);
  limpiarInterrupcionSensor();
  if (!escribirRegistroSensor(REG_ENABLE, ENABLE_COLOR_CON_INT)) {
    detenerConError("ERROR: no se pudo encender el sensor.");
  }
  Serial.println("Sensor configurado (interrupcion en GPIO27).");
}

// =============================================================
// CRC-8 (polinomio 0x07, valor inicial 0x00) - igual que SMBus
// =============================================================
uint8_t calcularCrc8(const uint8_t *datos, uint8_t longitud) {
  uint8_t crc = 0x00;
  for (uint8_t i = 0; i < longitud; i++) {
    crc = crc ^ datos[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x07;
      } else {
        crc = crc << 1;
      }
    }
  }
  return crc;
}

// =============================================================
// Color: clasificacion y lectura
// =============================================================
const char *nombreColor(uint8_t color) {
  if (color == COLOR_ROJO)  return "rojo";
  if (color == COLOR_VERDE) return "verde";
  if (color == COLOR_AZUL)  return "azul";
  return "ninguno";
}

uint8_t clasificarColor(uint16_t rojo, uint16_t verde, uint16_t azul) {
  uint32_t suma = (uint32_t)rojo + verde + azul;
  if (suma < SUMA_MINIMA_RGB) {
    return COLOR_NINGUNO;
  }
  uint32_t porcentajeRojo  = (uint32_t)rojo  * 100 / suma;
  uint32_t porcentajeVerde = (uint32_t)verde * 100 / suma;
  uint32_t porcentajeAzul  = (uint32_t)azul  * 100 / suma;
  if (porcentajeRojo  >= PORCENTAJE_DOMINANTE) return COLOR_ROJO;
  if (porcentajeVerde >= PORCENTAJE_DOMINANTE) return COLOR_VERDE;
  if (porcentajeAzul  >= PORCENTAJE_DOMINANTE) return COLOR_AZUL;
  return COLOR_NINGUNO;
}

void leerYMostrarColor() {
  uint8_t datos[8];
  bool lecturaOk = leerRegistrosSensor(REG_CDATAL, datos, 8);
  limpiarInterrupcionSensor();      // Siempre, para que INT pueda volver a bajar
  ultimoDatoMs = millis();
  if (!lecturaOk) {
    Serial.println("ERROR: fallo la lectura del sensor.");
    return;
  }

  clearCrudo = ((uint16_t)datos[1] << 8) | datos[0];
  rojoCrudo  = ((uint16_t)datos[3] << 8) | datos[2];
  verdeCrudo = ((uint16_t)datos[5] << 8) | datos[4];
  azulCrudo  = ((uint16_t)datos[7] << 8) | datos[6];
  colorDetectado = clasificarColor(rojoCrudo, verdeCrudo, azulCrudo);
  numeroLectura++;                  // 255 + 1 -> 0

  Serial.printf("Lectura #%3u: C=%5u R=%5u G=%5u B=%5u -> %s\n",
                numeroLectura, clearCrudo, rojoCrudo, verdeCrudo, azulCrudo,
                nombreColor(colorDetectado));
}

// =============================================================
// Protocolo: procesamiento de comandos
// =============================================================
void escribirU16(uint8_t *destino, uint16_t valor) {  // Little-endian
  destino[0] = valor & 0xFF;
  destino[1] = valor >> 8;
}

void procesarComando() {
  uint8_t comando[LONGITUD_COMANDO];
  for (uint8_t i = 0; i < LONGITUD_COMANDO; i++) {
    comando[i] = bufferComando[i];
  }
  uint8_t longitud = longitudComando;
  hayComandoNuevo = false;

  if (longitud != LONGITUD_COMANDO) {
    Serial.printf("Comando ignorado: %u bytes (se esperan %u)\n", longitud, LONGITUD_COMANDO);
    return;
  }

  uint8_t cmd = comando[0];
  uint8_t seq = comando[1];
  uint8_t resultado = RES_OK;
  uint8_t datos[LONGITUD_DATOS] = {0};

  if (calcularCrc8(comando, LONGITUD_COMANDO - 1) != comando[LONGITUD_COMANDO - 1]) {
    resultado = RES_ERR_CRC;
  } else if (cmd == CMD_PING) {
    datos[0] = VERSION_PROTOCOLO;
    datos[1] = VERSION_FIRMWARE;
    datos[2] = idSensor;
  } else if (cmd == CMD_LEER_COLOR) {
    escribirU16(&datos[0], clearCrudo);
    escribirU16(&datos[2], rojoCrudo);
    escribirU16(&datos[4], verdeCrudo);
    escribirU16(&datos[6], azulCrudo);
    datos[8] = colorDetectado;
    datos[9] = numeroLectura;
  } else {
    resultado = RES_ERR_COMANDO;
  }

  uint8_t respuesta[LONGITUD_RESPUESTA];
  respuesta[0] = cmd;
  respuesta[1] = resultado;
  respuesta[2] = seq;
  for (uint8_t i = 0; i < LONGITUD_DATOS; i++) {
    respuesta[3 + i] = datos[i];
  }
  respuesta[LONGITUD_RESPUESTA - 1] = calcularCrc8(respuesta, LONGITUD_RESPUESTA - 1);
  Wire.slaveWrite(respuesta, LONGITUD_RESPUESTA);

  if (cmd != CMD_LEER_COLOR || resultado != RES_OK) {
    Serial.printf("CMD 0x%02X seq %u -> resultado %u\n", cmd, seq, resultado);
  }
}

// =============================================================
// setup() y loop()
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Paso 10: el ESP32 lee el color y la Pi lo muestra ===");

  const uint8_t vectorPrueba[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  if (calcularCrc8(vectorPrueba, 9) != 0xF4) {
    detenerConError("ERROR: CRC-8 mal implementado.");
  }
  Serial.println("CRC-8 OK (vector de prueba = 0xF4)");

  iniciarSensor();
  ultimoDatoMs = millis();

  Wire.onReceive(alRecibirComando);
  while (!Wire.begin(DIRECCION_ESCLAVO, PIN_SDA_A, PIN_SCL_A, FRECUENCIA_I2C)) {
    Serial.println("Bus A sin reposo (SDA/SCL en 0). Pi encendida y conectada? Reintento en 1 s...");
    delay(1000);
  }
  Serial.println("Esclavo listo en 0x42. Esperando comandos de la Pi...");
}

void loop() {
  if (hayComandoNuevo) {            // Primero: la Pi espera la respuesta
    procesarComando();
  }

  if (datoSensorListo) {            // Camino normal: llego la interrupcion
    datoSensorListo = false;
    leerYMostrarColor();
  } else if (millis() - ultimoDatoMs > TIEMPO_MAXIMO_SIN_DATO_MS) {
    // Camino de recuperacion: INT pudo quedar trabado en bajo
    Serial.println("AVISO: sin interrupcion del sensor en 500 ms. Se lee y se limpia INT.");
    leerYMostrarColor();
  }
}
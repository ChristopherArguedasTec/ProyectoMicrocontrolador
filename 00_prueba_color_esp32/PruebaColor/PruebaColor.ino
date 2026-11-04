

#include <Wire.h>          // Librería de Arduino para comunicación I2C

//  Configuración de conexión
#define APDS_ADDR 0x39     // Dirección I2C fija del APDS-9960 (datasheet)
#define SDA_PIN   21       // Pin del ESP32 usado como SDA (datos)
#define SCL_PIN   22       // Pin del ESP32 usado como SCL (reloj)

//  Registros del sensor (datasheet) 
#define REG_ENABLE  0x80   // ENABLE: enciende el sensor y activa funciones
#define REG_ATIME   0x81   // ATIME: tiempo de integración del sensor de color
#define REG_CONTROL 0x8F   // CONTROL: ganancia del sensor de color
#define REG_ID      0x92   // ID: identificación del chip (0xAB = APDS-9960)
#define REG_STATUS  0x93   // STATUS: indica si hay un dato nuevo listo
#define REG_CDATAL  0x94   // Primer registro de datos de color (Clear, byte bajo)

//  Parámetros de detección 
// LUZ_MINIMA: si el canal Clear (luz total) es menor a este valor,
// no hay suficiente luz u objeto cerca, y no se evalúa el color.
const uint16_t LUZ_MINIMA = 30;
// UMBRAL: fracción mínima que debe tener el color dominante respecto
// a la suma R+G+B para considerarse detectado (0.42 = 42 %).
const float    UMBRAL     = 0.42;

// 
// Función: escribir un valor en un registro del sensor
// 
void writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(APDS_ADDR);  // Inicia comunicación con el sensor
  Wire.write(reg);                    // Indica a qué registro se va a escribir
  Wire.write(val);                    // Envía el valor a guardar en ese registro
  Wire.endTransmission();             // Termina la transmisión (condición STOP)
}

// 
// Función: leer un byte de un registro del sensor
// 
uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(APDS_ADDR);  // Inicia comunicación con el sensor
  Wire.write(reg);                    // Indica qué registro se quiere leer
  Wire.endTransmission(false);        // "false" = repeated start (no suelta el bus)
  Wire.requestFrom((uint8_t)APDS_ADDR, (uint8_t)1);  // Pide 1 byte al sensor
  return Wire.read();                 // Devuelve el byte recibido
}

// 
// Función: leer los 4 canales de color (Clear, Red, Green, Blue)
// Cada canal es de 16 bits, guardado en 2 registros (byte bajo y alto).
// Se leen los 8 bytes seguidos desde 0x94 hasta 0x9B.
// 
void readRGBC(uint16_t &c, uint16_t &r, uint16_t &g, uint16_t &b) {
  Wire.beginTransmission(APDS_ADDR);  // Inicia comunicación con el sensor
  Wire.write(REG_CDATAL);             // Empieza en el registro 0x94 (Clear bajo)
  Wire.endTransmission(false);        // Repeated start para leer a continuación
  Wire.requestFrom((uint8_t)APDS_ADDR, (uint8_t)8);  // Pide los 8 bytes de color

  uint8_t d[8];                       // Arreglo para guardar los 8 bytes
  for (int i = 0; i < 8; i++) d[i] = Wire.read();  // Lee byte por byte

  // Une cada par de bytes (bajo | alto << 8) en un valor de 16 bits
  c = d[0] | (d[1] << 8);             // Clear: luz total
  r = d[2] | (d[3] << 8);             // Rojo
  g = d[4] | (d[5] << 8);             // Verde
  b = d[6] | (d[7] << 8);             // Azul
}

// 
// SETUP: se ejecuta una sola vez al encender o reiniciar el ESP32
// 
void setup() {
  Serial.begin(115200);               // Inicia el Serial Monitor a 115200 baud
  delay(500);                         // Espera a que el monitor y el sensor estén listos
  Wire.begin(SDA_PIN, SCL_PIN);       // Inicia I2C en los pines 21 (SDA) y 22 (SCL)

  //  Paso 1: verificar que el sensor responda 
  uint8_t id = readReg(REG_ID);       // Lee el registro de identificación
  Serial.printf("ID del sensor: 0x%02X ", id);
  if (id == 0xAB || id == 0xA8) Serial.println("(APDS-9960 OK)");  // 0xAB original, 0xA8 algunos clones
  else Serial.println("(no reconocido, revisa conexiones)");       // 0xFF o 0x00 = no hay comunicación

  //  Paso 2: configurar el sensor de color 
  writeReg(REG_ENABLE, 0x00);         // Apaga todo mientras se configura (recomendado en el datasheet)
  writeReg(REG_ATIME, 0xDB);          // Integración: 256 - 0xDB = 37 ciclos × 2.78 ms ≈ 103 ms
  writeReg(REG_CONTROL, 0x01);        // Ganancia de color 4x (0x02 = 16x si hay poca luz)

  //  Paso 3: encender el sensor 
  writeReg(REG_ENABLE, 0x03);         // Bit 0 PON = 1 (encendido), bit 1 AEN = 1 (activa color/ALS)
  delay(150);                         // Espera a que termine la primera medición
}

// 
// LOOP: se repite continuamente
// 
void loop() {
  //  Paso 1: revisar si hay un dato nuevo 
  // El bit 0 del registro STATUS (AVALID) vale 1 cuando terminó una medición.
  // Si todavía no hay dato nuevo, sale del loop y vuelve a intentar.
  if (!(readReg(REG_STATUS) & 0x01)) return;

  //  Paso 2: leer los valores de color 
  uint16_t c, r, g, b;
  readRGBC(c, r, g, b);

  //  Paso 3: calcular qué color domina 
  bool rojo = false, verde = false, azul = false;  // Por defecto, ningún color detectado
  float suma = (float)r + g + b;                    // Suma total de los tres colores

  // Solo se evalúa si hay suficiente luz y la suma no es cero
  if (c >= LUZ_MINIMA && suma > 0) {
    // Proporción de cada color respecto al total (entre 0 y 1)
    float fr = r / suma, fg = g / suma, fb = b / suma;

    // El color detectado debe ser el mayor de los tres Y superar el UMBRAL
    if (fr > fg && fr > fb && fr > UMBRAL)      rojo  = true;
    else if (fg > fr && fg > fb && fg > UMBRAL) verde = true;
    else if (fb > fr && fb > fg && fb > UMBRAL) azul  = true;
  }

  //  Paso 4: mostrar resultados en el Serial Monitor 
  // Imprime los valores crudos y "OK" en el color detectado ("--" si no)
  Serial.printf("R:%5u G:%5u B:%5u C:%5u  |  Rojo: %s  Verde: %s  Azul: %s\n",
                r, g, b, c,
                rojo  ? "OK" : "--",
                verde ? "OK" : "--",
                azul  ? "OK" : "--");

  //  Paso 5: pequeña pausa antes de la siguiente lectura 
  delay(200);
}
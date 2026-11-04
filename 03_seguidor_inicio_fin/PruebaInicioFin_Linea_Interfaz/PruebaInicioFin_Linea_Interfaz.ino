// =============================================================
// LF6 - Seguidor de línea (LF5) + INICIO y FIN desde la Raspberry Pi por I2C
// Placa: ESP32 DEVKIT V1 (HW-463)  |  Core esp32 3.3.12
// Bus A (Wire, ESCLAVO 0x42): SDA = GPIO21, SCL = GPIO22
// Protocolo v1: comando 6 bytes -> respuesta 14 bytes, CRC-8 (polinomio 0x07)
// Comandos: PING (0x01), LEER_ESTADO (0x02), INICIAR (0x10), DETENER (0x11)
// =============================================================

#include <Wire.h>

// --- Pines del driver ---
const int PIN_PWMA = 25;
const int PIN_AIN1 = 26;
const int PIN_AIN2 = 14;
const int PIN_PWMB = 13;
const int PIN_BIN1 = 16;
const int PIN_BIN2 = 17;
const int PIN_STBY = 4;
const int PIN_LED  = 2;   // LED azul de la placa

// --- Pines del sensor de línea, en orden S1, S2, S3, S4, S5 ---
const int NUM_SENSORES = 5;
const int PINES_SENSORES[NUM_SENSORES] = {36, 39, 34, 35, 19};

// --- Bus I2C con la Raspberry Pi ---
const int PIN_SDA_A = 21;
const int PIN_SCL_A = 22;
const uint32_t FRECUENCIA_I2C = 100000;

// --- Sentido de los motores (calibrado en la prueba D2) ---
const bool INVERTIR_IZQUIERDA = true;
const bool INVERTIR_DERECHA   = true;

// --- Orientación del sensor ---
const bool S1_A_LA_IZQUIERDA = true;

// --- Interruptor general de los motores ---
const bool MOTORES_HABILITADOS = true;

// --- PWM ---
const uint32_t FRECUENCIA_PWM = 20000;
const uint8_t  RESOLUCION_PWM = 10;
const int32_t  PWM_TOPE = 1023;

// --- Velocidad y límite de voltaje ---
const float VOLTAJE_BATERIA = 8.4;   // batería llena
const float VOLTAJE_MOTOR   = 4.5;   // voltaje nominal de los motores
const int32_t PWM_BASE = (int32_t)(VOLTAJE_MOTOR / VOLTAJE_BATERIA * PWM_TOPE);  // = 548
const int32_t PWM_LIMITE_MOTOR = PWM_BASE;   // ninguna orden puede pasar de 4.5 V
const int32_t PWM_TOPE_SEGURIDAD = 716;      // revisión: PWM_BASE nunca debe pasar del 70 %

// --- Control PD ---
const int   PESOS[NUM_SENSORES] = {-2, -1, 0, 1, 2};
const float KP = 0.6 * PWM_BASE;       // corrección por unidad de error
const float KD = 2.0 * PWM_BASE;       // corrección por cambio del error en un ciclo
const float ALFA_FILTRO = 0.2;         // fracción que avanza el error filtrado en cada ciclo
const float REDUCCION_EN_CURVA = 0.3;  // con error 2, la velocidad base baja 30 %

// --- Pérdida de línea ---
const unsigned long TIEMPO_TOLERANCIA_MS = 200;               // pérdida breve: mantiene la última orden
const int32_t PWM_BUSQUEDA = (int32_t)(0.75 * PWM_BASE);       // velocidad de los pulsos de búsqueda
const unsigned long PULSO_MS = 80;                            // duración de cada pulso de giro
const unsigned long PAUSA_MS = 40;                            // frenado entre pulsos
const int NUM_TRAMOS = 4;
const int PULSOS_POR_TRAMO[NUM_TRAMOS] = {3, 6, 9, 12};

// --- Seguridad y tiempos ---
const unsigned long TIEMPO_TODO_NEGRO_MS = 500;
const unsigned long TIEMPO_WATCHDOG_MS   = 1000;   // sin comandos de la Pi por 1 s: se detiene
const unsigned long DURACION_FRENO_MS    = 300;    // freno activo antes de apagar el driver
const unsigned long PERIODO_CONTROL_MS   = 5;
const unsigned long PERIODO_IMPRESION_MS = 200;

// --- Protocolo ---
const uint8_t DIRECCION_ESCLAVO  = 0x42;
const uint8_t LONGITUD_COMANDO   = 6;
const uint8_t LONGITUD_RESPUESTA = 14;
const uint8_t LONGITUD_DATOS     = 10;
const uint8_t VERSION_PROTOCOLO  = 1;
const uint8_t VERSION_FIRMWARE   = 3;   // 3 = LF6

const uint8_t CMD_PING        = 0x01;
const uint8_t CMD_LEER_ESTADO = 0x02;
const uint8_t CMD_INICIAR     = 0x10;
const uint8_t CMD_DETENER     = 0x11;

const uint8_t RES_OK            = 0x00;
const uint8_t RES_ERR_COMANDO   = 0x01;
const uint8_t RES_ERR_CRC       = 0x04;
const uint8_t RES_ERR_SIN_LINEA = 0x05;   // INICIO rechazado: no hay línea bajo el sensor

// Estado que se informa a la Pi (byte 0 de los datos de LEER_ESTADO)
const uint8_t INFORME_DETENIDO  = 0;
const uint8_t INFORME_SIGUIENDO = 1;
const uint8_t INFORME_BUSCANDO  = 4;

// Motivo del último paro (byte 3 de LEER_ESTADO y número de parpadeos del LED)
const uint8_t MOTIVO_NINGUNO          = 0;   // detenido por FIN o recién encendido
const uint8_t MOTIVO_BARRIDO          = 1;   // terminó el barrido sin encontrar la línea
const uint8_t MOTIVO_TODO_NEGRO       = 2;   // todos los sensores en negro por más de 0.5 s
const uint8_t MOTIVO_SIN_COMUNICACION = 3;   // la Pi dejó de enviar comandos (watchdog)

// --- Estado general ---
enum Estado { SIGUIENDO, TOLERANCIA, BUSCANDO, DETENIDO };
const char *NOMBRES_ESTADO[] = {"SIG", "TOL", "BUS", "DET"};
Estado estado = DETENIDO;            // al encender espera el INICIO de la interfaz
uint8_t motivoParo = MOTIVO_NINGUNO;
int ultimoLado = 0;                  // -1 izquierda, 0 centro, +1 derecha
unsigned long inicioPerdidaMs = 0;
bool todoNegroActivo = false;
unsigned long inicioTodoNegroMs = 0;
unsigned long ultimoInstanteControlMs = 0;
unsigned long ultimoInstanteImpresionMs = 0;
unsigned long ultimoComandoMs = 0;   // último comando válido de la Pi (watchdog)
bool frenoPendiente = false;
unsigned long inicioFrenoMs = 0;
int lecturas[NUM_SENSORES];

// --- Control PD ---
float errorFiltrado = 0;
float errorFiltradoAnterior = 0;
bool reiniciarFiltro = true;
int32_t comandoIzq = 0;
int32_t comandoDer = 0;

// --- Búsqueda por pulsos ---
int direccionBusqueda = -1;          // -1 gira a la izquierda, +1 a la derecha
int tramoBusqueda = 0;
int pulsosDados = 0;
bool enPulso = false;
unsigned long inicioFaseMs = 0;

// --- Variables compartidas con el callback del bus I2C ---
volatile bool hayComandoNuevo = false;
volatile uint8_t bufferComando[LONGITUD_COMANDO];
volatile uint8_t longitudComando = 0;

// =============================================================
// Motores
// =============================================================
void moverLado(int pinPwm, int pinIn1, int pinIn2, int32_t velocidad, bool invertir) {
  if (invertir) velocidad = -velocidad;
  if (velocidad > PWM_LIMITE_MOTOR) velocidad = PWM_LIMITE_MOTOR;     // límite de 4.5 V
  if (velocidad < -PWM_LIMITE_MOTOR) velocidad = -PWM_LIMITE_MOTOR;
  if (velocidad >= 0) {
    digitalWrite(pinIn1, HIGH);
    digitalWrite(pinIn2, LOW);
    ledcWrite(pinPwm, (uint32_t)velocidad);
  } else {
    digitalWrite(pinIn1, LOW);
    digitalWrite(pinIn2, HIGH);
    ledcWrite(pinPwm, (uint32_t)(-velocidad));
  }
}

void mover(int32_t izq, int32_t der) {
  moverLado(PIN_PWMA, PIN_AIN1, PIN_AIN2, izq, INVERTIR_IZQUIERDA);
  moverLado(PIN_PWMB, PIN_BIN1, PIN_BIN2, der, INVERTIR_DERECHA);
}

void frenar() {
  digitalWrite(PIN_AIN1, HIGH);
  digitalWrite(PIN_AIN2, HIGH);
  digitalWrite(PIN_BIN1, HIGH);
  digitalWrite(PIN_BIN2, HIGH);
  ledcWrite(PIN_PWMA, 0);
  ledcWrite(PIN_PWMB, 0);
}

void soltar() {
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);
  ledcWrite(PIN_PWMA, 0);
  ledcWrite(PIN_PWMB, 0);
}

// Frena ya; completarFreno() apaga el driver 300 ms después sin bloquear el programa
void detenerMotores() {
  frenar();
  frenoPendiente = true;
  inicioFrenoMs = millis();
  estado = DETENIDO;
}

void completarFreno() {
  if (frenoPendiente && millis() - inicioFrenoMs >= DURACION_FRENO_MS) {
    soltar();
    digitalWrite(PIN_STBY, LOW);
    frenoPendiente = false;
  }
}

// Paro por seguridad: no vuelve a arrancar solo, solo con INICIO desde la interfaz
void detenerPorSeguridad(const char *motivo, uint8_t codigo) {
  detenerMotores();
  motivoParo = codigo;
  Serial.print("DETENIDO: ");
  Serial.println(motivo);
}

// LED: fijo = esperando INICIO; parpadeos = motivo del paro; apagado = en marcha
void actualizarLed() {
  if (estado != DETENIDO) {
    digitalWrite(PIN_LED, LOW);
    return;
  }
  if (motivoParo == MOTIVO_NINGUNO) {
    digitalWrite(PIN_LED, HIGH);
    return;
  }
  unsigned long duracionParpadeos = motivoParo * 400UL;
  unsigned long t = millis() % (duracionParpadeos + 1000);
  bool encendido = (t < duracionParpadeos) && (t % 400 < 200);
  digitalWrite(PIN_LED, encendido ? HIGH : LOW);
}

// =============================================================
// Seguimiento de línea (igual que LF5)
// =============================================================
// Lee los 5 sensores; devuelve cuántos ven negro (LOW)
int leerSensores() {
  int numNegros = 0;
  for (int i = 0; i < NUM_SENSORES; i++) {
    lecturas[i] = digitalRead(PINES_SENSORES[i]);
    if (lecturas[i] == LOW) numNegros++;
  }
  return numNegros;
}

// Más error = menos velocidad base y más freno en la rueda interior
void calcularComando(float error, float derivada) {
  float correccion = KP * error + KD * derivada;
  float base = PWM_BASE * (1.0 - REDUCCION_EN_CURVA * fabs(error) / 2.0);
  float izq = base;
  float der = base;
  if (correccion < 0) izq = base + correccion;   // línea a la izquierda: frena la rueda izquierda
  else der = base - correccion;                  // línea a la derecha: frena la rueda derecha
  if (izq < -base) izq = -base;
  if (der < -base) der = -base;
  comandoIzq = (int32_t)izq;
  comandoDer = (int32_t)der;
}

// Calcula el error, lo filtra y aplica el control PD
void seguirLinea(int numNegros) {
  int sumaPesos = 0;
  for (int i = 0; i < NUM_SENSORES; i++) {
    if (lecturas[i] == LOW) sumaPesos += PESOS[i];
  }
  if (!S1_A_LA_IZQUIERDA) sumaPesos = -sumaPesos;

  float error = (float)sumaPesos / numNegros;
  if (sumaPesos < 0) ultimoLado = -1;
  else if (sumaPesos > 0) ultimoLado = 1;
  else ultimoLado = 0;

  if (reiniciarFiltro) {
    // Al arrancar o al recuperar la línea: sin historia, la derivada empieza en 0
    errorFiltrado = error;
    errorFiltradoAnterior = error;
    reiniciarFiltro = false;
  } else {
    errorFiltradoAnterior = errorFiltrado;
    errorFiltrado += ALFA_FILTRO * (error - errorFiltrado);
  }
  float derivada = errorFiltrado - errorFiltradoAnterior;
  calcularComando(errorFiltrado, derivada);
}

void iniciarBusqueda(unsigned long ahora) {
  estado = BUSCANDO;
  direccionBusqueda = (ultimoLado != 0) ? ultimoLado : -1;
  tramoBusqueda = 0;
  pulsosDados = 0;
  enPulso = true;
  inicioFaseMs = ahora;
  Serial.print("Linea perdida, buscando hacia ");
  Serial.println(direccionBusqueda < 0 ? "la izquierda" : "la derecha");
}

// Avanza el barrido por pulsos; devuelve false si se detuvo por no encontrar la línea
bool avanzarBusqueda(unsigned long ahora) {
  if (enPulso) {
    if (ahora - inicioFaseMs >= PULSO_MS) {
      enPulso = false;
      inicioFaseMs = ahora;
      pulsosDados++;
    }
  } else if (ahora - inicioFaseMs >= PAUSA_MS) {
    if (pulsosDados >= PULSOS_POR_TRAMO[tramoBusqueda]) {
      tramoBusqueda++;
      if (tramoBusqueda >= NUM_TRAMOS) {
        detenerPorSeguridad("no se encontro la linea tras el barrido", MOTIVO_BARRIDO);
        return false;
      }
      pulsosDados = 0;
      direccionBusqueda = -direccionBusqueda;
    }
    enPulso = true;
    inicioFaseMs = ahora;
  }
  comandoIzq = direccionBusqueda * PWM_BUSQUEDA;    // giro sobre su eje
  comandoDer = -direccionBusqueda * PWM_BUSQUEDA;
  return true;
}

void aplicarMotores() {
  if (!MOTORES_HABILITADOS) return;
  if (estado == BUSCANDO && !enPulso) frenar();     // pausa entre pulsos
  else mover(comandoIzq, comandoDer);
}

void imprimirEstado(int numNegros) {
  Serial.print(NOMBRES_ESTADO[estado]);
  Serial.print(" ");
  for (int i = 0; i < NUM_SENSORES; i++) {
    Serial.print(lecturas[i] == LOW ? "# " : ". ");
  }
  Serial.print(" negros=");
  Serial.print(numNegros);
  Serial.print(" errorF=");
  Serial.print(errorFiltrado, 2);
  Serial.print(" izq=");
  Serial.print(comandoIzq);
  Serial.print(" der=");
  Serial.println(comandoDer);
}

// =============================================================
// INICIO y FIN (los pide la Pi)
// =============================================================
// Arranca solo si hay línea bajo el sensor; si no, la Pi recibe RES_ERR_SIN_LINEA
uint8_t iniciarMarcha() {
  if (estado != DETENIDO) return RES_OK;            // ya estaba en marcha
  int numNegros = leerSensores();
  if (numNegros == 0 || numNegros == NUM_SENSORES) return RES_ERR_SIN_LINEA;

  frenoPendiente = false;
  soltar();
  comandoIzq = 0;
  comandoDer = 0;
  reiniciarFiltro = true;
  todoNegroActivo = false;
  motivoParo = MOTIVO_NINGUNO;
  estado = SIGUIENDO;
  if (MOTORES_HABILITADOS) digitalWrite(PIN_STBY, HIGH);
  Serial.println("INICIO: en marcha.");
  return RES_OK;
}

void detenerMarcha() {
  if (estado != DETENIDO) detenerMotores();
  motivoParo = MOTIVO_NINGUNO;
  Serial.println("FIN: detenido.");
}

uint8_t estadoParaInforme() {
  if (estado == DETENIDO) return INFORME_DETENIDO;
  if (estado == BUSCANDO) return INFORME_BUSCANDO;
  return INFORME_SIGUIENDO;                         // SIGUIENDO o TOLERANCIA
}

// Si la Pi deja de enviar comandos mientras el carro se mueve, se detiene
void revisarWatchdog() {
  if (estado != DETENIDO && millis() - ultimoComandoMs > TIEMPO_WATCHDOG_MS) {
    detenerPorSeguridad("sin comunicacion con la Raspberry Pi", MOTIVO_SIN_COMUNICACION);
  }
}

// =============================================================
// Protocolo I2C (igual que el paso 10)
// =============================================================
void alRecibirComando(int cantidad) {
  if (cantidad < 1) {               // Escritura vacía (i2cdetect): ignorar
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

// CRC-8 (polinomio 0x07, valor inicial 0x00) - igual que SMBus
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

// Datos de LEER_ESTADO (también se devuelven tras INICIAR y DETENER)
void llenarEstado(uint8_t *datos) {
  datos[0] = estadoParaInforme();
  datos[1] = 0;            // banderas (velocidad, bifurcación): fase siguiente
  datos[2] = 0;            // último color: fase siguiente
  datos[3] = motivoParo;
  // datos[4..9]: contadores rojo, verde y azul (fase siguiente), quedan en 0
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
  } else {
    ultimoComandoMs = millis();     // comando válido: la Pi sigue conectada
    if (cmd == CMD_PING) {
      datos[0] = VERSION_PROTOCOLO;
      datos[1] = VERSION_FIRMWARE;
    } else if (cmd == CMD_LEER_ESTADO) {
      llenarEstado(datos);
    } else if (cmd == CMD_INICIAR) {
      resultado = iniciarMarcha();
      llenarEstado(datos);
    } else if (cmd == CMD_DETENER) {
      detenerMarcha();
      llenarEstado(datos);
    } else {
      resultado = RES_ERR_COMANDO;
    }
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

  if (cmd != CMD_LEER_ESTADO || resultado != RES_OK) {
    Serial.printf("CMD 0x%02X seq %u -> resultado %u\n", cmd, seq, resultado);
  }
}

// =============================================================
// setup() y loop()
// =============================================================
void detenerConError(const char *mensaje) {
  Serial.println(mensaje);
  while (true) {
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== LF6: seguidor de linea con INICIO/FIN desde la Raspberry Pi ===");

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  for (int i = 0; i < NUM_SENSORES; i++) pinMode(PINES_SENSORES[i], INPUT);

  if (!ledcAttach(PIN_PWMA, FRECUENCIA_PWM, RESOLUCION_PWM) ||
      !ledcAttach(PIN_PWMB, FRECUENCIA_PWM, RESOLUCION_PWM)) {
    detenerConError("ERROR: no se pudo configurar el PWM.");
  }
  soltar();

  Serial.print("PWM_BASE = ");
  Serial.println(PWM_BASE);
  if (PWM_BASE > PWM_TOPE_SEGURIDAD) {
    detenerConError("ERROR: PWM_BASE supera el tope de seguridad. Revise VOLTAJE_MOTOR.");
  }

  const uint8_t vectorPrueba[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  if (calcularCrc8(vectorPrueba, 9) != 0xF4) {
    detenerConError("ERROR: CRC-8 mal implementado.");
  }

  Wire.onReceive(alRecibirComando);
  while (!Wire.begin(DIRECCION_ESCLAVO, PIN_SDA_A, PIN_SCL_A, FRECUENCIA_I2C)) {
    Serial.println("Bus A sin reposo (SDA/SCL en 0). Pi encendida y conectada? Reintento en 1 s...");
    delay(1000);
  }
  Serial.println("Esclavo listo en 0x42. Esperando INICIO desde la interfaz...");
}

void loop() {
  if (hayComandoNuevo) {            // Primero: la Pi espera la respuesta
    procesarComando();
  }
  completarFreno();
  revisarWatchdog();
  actualizarLed();

  if (estado == DETENIDO) return;

  unsigned long ahora = millis();
  if (ahora - ultimoInstanteControlMs < PERIODO_CONTROL_MS) return;
  ultimoInstanteControlMs = ahora;

  int numNegros = leerSensores();
  if (numNegros != NUM_SENSORES) todoNegroActivo = false;

  if (numNegros == 0) {
    // --- Sin línea: primero tolerancia, después búsqueda por pulsos ---
    if (estado == SIGUIENDO) {
      estado = TOLERANCIA;
      inicioPerdidaMs = ahora;
    } else if (estado == TOLERANCIA && ahora - inicioPerdidaMs > TIEMPO_TOLERANCIA_MS) {
      iniciarBusqueda(ahora);
    }
    if (estado == BUSCANDO && !avanzarBusqueda(ahora)) return;
    // En TOLERANCIA se mantiene la última orden (comandoIzq y comandoDer no cambian)
  } else {
    // --- Hay línea ---
    if (estado != SIGUIENDO) {
      if (estado == BUSCANDO) Serial.println("Linea recuperada.");
      estado = SIGUIENDO;
      reiniciarFiltro = true;
    }

    if (numNegros == NUM_SENSORES) {
      // Todo negro: cruce, marca o carro levantado. Se mantiene la última orden.
      if (!todoNegroActivo) {
        todoNegroActivo = true;
        inicioTodoNegroMs = ahora;
      } else if (ahora - inicioTodoNegroMs > TIEMPO_TODO_NEGRO_MS) {
        detenerPorSeguridad("todos los sensores en negro (carro levantado?)", MOTIVO_TODO_NEGRO);
        return;
      }
    } else {
      seguirLinea(numNegros);
    }
  }

  aplicarMotores();

  if (ahora - ultimoInstanteImpresionMs >= PERIODO_IMPRESION_MS) {
    ultimoInstanteImpresionMs = ahora;
    imprimirEstado(numNegros);
  }
}

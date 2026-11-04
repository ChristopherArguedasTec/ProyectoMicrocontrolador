// =============================================================
// Prueba D2 (calibrada) - 4 motores con 2 TB6612FNG (senales compartidas)
// Placa: ESP32 DEVKIT V1 en placa de expansion G/V/S  |  Core esp32 3.3.12
// Lado IZQUIERDO = canal A de ambos drivers   Lado DERECHO = canal B de ambos drivers
// Conexiones (fila S): GPIO25->PWMA  GPIO26->AIN1  GPIO14->AIN2
//                      GPIO13->PWMB  GPIO16->BIN1  GPIO17->BIN2  GPIO4->STBY
// Secuencia (4 s por etapa): adelante lento, adelante rapido, freno, atras,
//                            curva izquierda, curva derecha, giro en el sitio, rueda libre
// =============================================================

// ---------- Pines del ESP32 hacia los TB6612 ----------
const int PIN_PWMA = 25;   // Velocidad lado izquierdo
const int PIN_AIN1 = 26;   // Sentido lado izquierdo
const int PIN_AIN2 = 14;   // Sentido lado izquierdo
const int PIN_PWMB = 13;   // Velocidad lado derecho
const int PIN_BIN1 = 16;   // Sentido lado derecho (RX2 en algunas placas)
const int PIN_BIN2 = 17;   // Sentido lado derecho (TX2 en algunas placas)
const int PIN_STBY = 4;    // 1 = drivers activos, 0 = standby

// ---------- Calibracion del montaje (verificada con la Prueba D3) ----------
const bool INVERTIR_IZQUIERDA = true;
const bool INVERTIR_DERECHA   = true;

// ---------- PWM ----------
const uint32_t FRECUENCIA_PWM = 20000;  // 20 kHz: no se escucha
const uint8_t  RESOLUCION_PWM = 10;     // Valores de 0 a 1023
const int32_t  PWM_MAXIMO     = 716;    // 70 %: con 8.4 V el motor recibe ~5.9 V
const int32_t  PWM_LENTO      = 358;    // 35 %: mitad del maximo (velocidad 1)

const unsigned long DURACION_ETAPA_MS = 4000;

// Mueve un lado. velocidad: de -PWM_MAXIMO a +PWM_MAXIMO (positivo = adelante, negativo = atras)
void moverLado(int pinPwm, int pinIn1, int pinIn2, int32_t velocidad, bool invertir) {
  if (invertir) {
    velocidad = -velocidad;               // Compensa la orientacion del montaje
  }
  if (velocidad > PWM_MAXIMO) {
    velocidad = PWM_MAXIMO;               // Limite superior
  }
  if (velocidad < -PWM_MAXIMO) {
    velocidad = -PWM_MAXIMO;              // Limite inferior
  }

  if (velocidad >= 0) {                   // IN1 alto, IN2 bajo
    digitalWrite(pinIn1, HIGH);
    digitalWrite(pinIn2, LOW);
    ledcWrite(pinPwm, (uint32_t)velocidad);
  } else {                                // IN1 bajo, IN2 alto
    digitalWrite(pinIn1, LOW);
    digitalWrite(pinIn2, HIGH);
    ledcWrite(pinPwm, (uint32_t)(-velocidad));
  }
}

// Mueve ambos lados con velocidades independientes
void mover(int32_t velocidadIzquierda, int32_t velocidadDerecha) {
  moverLado(PIN_PWMA, PIN_AIN1, PIN_AIN2, velocidadIzquierda, INVERTIR_IZQUIERDA);
  moverLado(PIN_PWMB, PIN_BIN1, PIN_BIN2, velocidadDerecha,   INVERTIR_DERECHA);
}

// Freno activo en los 4 motores: IN1 = IN2 = alto
void frenar() {
  digitalWrite(PIN_AIN1, HIGH);
  digitalWrite(PIN_AIN2, HIGH);
  digitalWrite(PIN_BIN1, HIGH);
  digitalWrite(PIN_BIN2, HIGH);
  ledcWrite(PIN_PWMA, 0);
  ledcWrite(PIN_PWMB, 0);
}

// Rueda libre en los 4 motores: IN1 = IN2 = bajo
void soltar() {
  digitalWrite(PIN_AIN1, LOW);
  digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW);
  digitalWrite(PIN_BIN2, LOW);
  ledcWrite(PIN_PWMA, 0);
  ledcWrite(PIN_PWMB, 0);
}

// Muestra el nombre de la etapa y la mantiene durante DURACION_ETAPA_MS
void etapa(const char *mensaje) {
  Serial.println(mensaje);
  delay(DURACION_ETAPA_MS);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("=== Prueba D2 (calibrada): 4 motores ===");

  pinMode(PIN_AIN1, OUTPUT);
  pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT);
  pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_STBY, OUTPUT);
  digitalWrite(PIN_STBY, LOW);            // Drivers apagados mientras se configura

  bool pwmOk = ledcAttach(PIN_PWMA, FRECUENCIA_PWM, RESOLUCION_PWM) &&
               ledcAttach(PIN_PWMB, FRECUENCIA_PWM, RESOLUCION_PWM);
  if (!pwmOk) {
    Serial.println("ERROR: no se pudo configurar el PWM.");
    while (true) {
      delay(1000);
    }
  }

  soltar();
  digitalWrite(PIN_STBY, HIGH);           // Drivers activos
  Serial.println("Drivers activos. La secuencia empieza en 5 s...");
  delay(5000);
}

void loop() {
  mover(PWM_LENTO, PWM_LENTO);
  etapa("1) ADELANTE velocidad 1 (lenta): las 4 ruedas hacia adelante");

  mover(PWM_MAXIMO, PWM_MAXIMO);
  etapa("2) ADELANTE velocidad 2 (rapida, el doble): las 4 ruedas hacia adelante");

  frenar();
  etapa("3) FRENO: las 4 ruedas se detienen de golpe");

  mover(-PWM_LENTO, -PWM_LENTO);
  etapa("4) REVERSA lenta: las 4 ruedas hacia atras");

  frenar();
  etapa("5) FRENO");

  mover(PWM_LENTO, PWM_MAXIMO);
  etapa("6) CURVA a la IZQUIERDA: ruedas derechas mas rapidas");

  mover(PWM_MAXIMO, PWM_LENTO);
  etapa("7) CURVA a la DERECHA: ruedas izquierdas mas rapidas");

  mover(-PWM_LENTO, PWM_LENTO);
  etapa("8) GIRO EN EL SITIO a la izquierda: izquierdas atras, derechas adelante");

  soltar();
  etapa("9) RUEDA LIBRE: las 4 ruedas se detienen poco a poco");

  Serial.println("--- Fin de la secuencia, se repite ---");
}
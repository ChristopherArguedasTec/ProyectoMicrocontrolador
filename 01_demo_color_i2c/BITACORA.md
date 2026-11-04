# 01. Demo de color por I2C (20/10/2026)

## Objetivo
Comprobar la comunicación I2C entre la Raspberry Pi y el ESP32 con un caso real: el ESP32 lee el color y la Pi lo muestra.

## Qué hace
- **ESP32 (`demo_color_carrito.ino`):** lee el sensor APDS-9960 y clasifica la lectura como rojo, verde, azul o ninguno. Un canal se considera dominante si tiene al menos el 45 % de la suma RGB.
- **Pi (`demo_color.py`):** envía `PING` y luego pide `LEER_COLOR` cada 0.5 s, e imprime los valores y el color.
- **Pi (`protocolo_esp32.py`):** arma los comandos, calcula el CRC-8 y verifica cada respuesta.

## Protocolo
- Comando de 6 bytes: `[CMD, SEQ, P0, P1, P2, CRC]`.
- Respuesta de 14 bytes: `[CMD, RES, SEQ, 10 datos, CRC]`.
- CRC-8 con polinomio 0x07 y valor inicial 0. Vector de prueba: `"123456789"` da `0xF4`.
- La Pi espera 10 ms entre escribir y leer, porque el ESP32 precarga la respuesta con `Wire.slaveWrite`.

## Conexiones
| Bus | Señal | ESP32 | Otro extremo |
|---|---|---|---|
| A (ESP32 esclavo 0x42) | SDA | GPIO 21 | Pi, pin 3 |
| A | SCL | GPIO 22 | Pi, pin 5 |
| A | GND | GND | Pi, pin 6 |
| B (ESP32 maestro) | SDA | GPIO 32 | APDS-9960 SDA |
| B | SCL | GPIO 33 | APDS-9960 SCL |
| B | INT | GPIO 27 | APDS-9960 INT |
| Alimentación del sensor | VCC | 3.3 V | APDS-9960 VCC |

## Interrupciones
El sensor avisa por INT que hay un dato nuevo. La rutina `isrSensor()` (flanco de bajada) solo levanta una bandera, y `loop()` hace la lectura.

## Problemas y decisiones
- **Error al cargar `Wrong boot mode`:** se resolvió manteniendo presionado BOOT durante `Connecting...`.
- **`Bus Invalid State` al iniciar el esclavo:** ocurría con la Pi apagada, porque SDA y SCL quedaban en 0. Se enciende primero la Pi y se agregó un bucle de reintento.
- **La lectura se congelaba:** el contador de una versión anterior se detuvo, probablemente porque INT quedó trabado en bajo. Si pasan 500 ms sin interrupción, ahora se lee el sensor y se limpia INT.
- **Se simplificó la demo:** la versión completa, con acciones, resultaba poco práctica para esta etapa. Quedaron solo `PING` y `LEER_COLOR`.

## Resultado
La Pi recibió y mostró las lecturas de color. El sensor respondió con el ID `0x9E`.

## Pendiente
Integrar el sensor al carro y calibrar los umbrales con los colores reales de la pista.

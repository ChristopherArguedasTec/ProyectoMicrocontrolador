# 00. Prueba del sensor de color con el ESP32 (primera prueba, antes del 20/10/2026)

## Objetivo
Comprobar que el sensor APDS-9960 responde por I2C al ESP32 y que entrega lecturas útiles para distinguir rojo, verde y azul. Fue lo primero que se hizo en el proyecto, antes de conectar la Raspberry Pi.

## Qué hace
`PruebaColor.ino` usa solo la librería `Wire`, sin librerías del sensor, y configura los registros directamente según la hoja de datos:

1. Lee el registro `ID` (0x92) e indica en el Serial Monitor si reconoce el chip.
2. Apaga el sensor (`ENABLE = 0x00`), configura el tiempo de integración y la ganancia, y lo enciende con `PON` y `AEN` (`ENABLE = 0x03`).
3. En el `loop()`, revisa el bit `AVALID` del registro `STATUS` (0x93). Cuando hay un dato nuevo, lee los 8 bytes de los canales Clear, Red, Green y Blue desde 0x94.
4. Clasifica el color e imprime los valores crudos y el resultado cada 200 ms. Formato de cada línea (los números son solo ilustrativos):

```
R:%5u G:%5u B:%5u C:%5u  |  Rojo: OK  Verde: --  Azul: --
```

## Conexiones
| Señal | ESP32 | APDS-9960 |
|---|---|---|
| SDA | GPIO 21 | SDA |
| SCL | GPIO 22 | SCL |
| Alimentación | 3.3 V | VCC |
| Tierra | GND | GND |

El pin INT del sensor no se usa en esta prueba.

## Configuración del sensor
| Registro | Valor | Efecto |
|---|---|---|
| `ATIME` (0x81) | 0xDB | 256 − 219 = 37 ciclos × 2.78 ms ≈ 103 ms de integración |
| `CONTROL` (0x8F) | 0x01 | Ganancia de color 4x (0x02 = 16x si hay poca luz) |
| `ENABLE` (0x80) | 0x03 | `PON` = 1 y `AEN` = 1 |

## Criterio de detección
- **Luz mínima:** si el canal Clear es menor que 30, no se evalúa el color (no hay objeto cerca o falta luz).
- **Proporción:** se calcula la fracción de cada canal respecto a R + G + B.
- **Color dominante:** el canal debe ser el mayor de los tres y superar el 42 % de la suma.

## Decisiones
- **Registros directos en lugar de una librería:** permite entender y controlar la configuración del sensor, y luego llevar el mismo código a la versión con interrupciones.
- **Proporciones en lugar de valores absolutos:** la intensidad cambia con la distancia y la luz ambiente, pero la proporción entre canales es más estable.

## Resultado
El sensor respondió por I2C y entregó lecturas RGBC en el Serial Monitor. Con esta prueba se confirmó que el APDS-9960 sirve para el proyecto.

## Observaciones
- La lectura se hace por consulta (*polling*) del bit `AVALID`, sin interrupciones. Como la rúbrica exige interrupciones en el microcontrolador, en la entrada 01 se pasó a usar el pin INT del sensor.
- El programa reconoce los ID 0xAB y 0xA8. El módulo que se usó en la entrada 01 respondió con 0x9E, así que con esa unidad esta prueba indicaría "no reconocido" aunque el sensor funcione.
- Los pines 21 y 22 quedaron después para el bus I2C con la Raspberry Pi (ESP32 como esclavo), y el sensor se movió a un segundo bus en GPIO 32 y 33.
- El umbral del 42 % se subió al 45 % en la entrada 01.

## Siguiente paso
Pasar la lectura a interrupciones y enviar el color a la Raspberry Pi por I2C (entrada 01).

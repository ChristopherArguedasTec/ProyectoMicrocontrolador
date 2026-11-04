# 03. Seguidor de línea con INICIO y FIN (01/11/2026)

## Objetivo
Hacer que el carro siga la línea y que arranque y se detenga por orden de la Raspberry Pi.

## Qué hace
- **ESP32 (`PruebaInicioFin_Linea_Interfaz.ino`, firmware v3, llamado LF6):** sigue la línea y atiende los comandos `PING`, `LEER_ESTADO`, `INICIAR` y `DETENER`.
- **Pi (`interfaz_grafica.py`):** primera interfaz gráfica, hecha con Tkinter. Se veía por VNC. La entrada 04 la reemplaza.
- **Pi (`protocolo_esp32.py`):** agrega `RES_ERR_SIN_LINEA`, el estado `BUSCANDO` y el motivo del paro.

## Sensor de línea (BFD-1000)
| Señal | Conexión |
|---|---|
| S1, S2, S3, S4, S5 | GPIO 36, 39, 34, 35, 19 |
| VCC | 3.3 V |
| CLP, Near | Sin conectar |

La salida vale 0 sobre negro y 1 sobre blanco. Su sensibilidad es fija y solo distingue bien el tape muy cerca del piso, por lo que se montó a unos 3 mm.

## Algoritmo
1. **Error:** promedio de los pesos de los sensores que ven negro (−2, −1, 0, +1, +2).
2. **Control PD:** usa el error filtrado para suavizar los saltos de los sensores digitales. No se usó término integral, porque se acumularía mientras la línea está perdida.
3. **Curvas:** baja la velocidad base y frena la rueda interior.
4. **Pérdida breve (hasta 200 ms):** mantiene la última orden, para tolerar rebotes del sensor.
5. **Búsqueda:** gira sobre su eje con pulsos de 80 ms y pausas de 40 ms, en un barrido alterno que se va ampliando.

## Seguridad
| Condición | Reacción | LED azul |
|---|---|---|
| Barrido completo sin encontrar la línea | Se detiene | 1 parpadeo |
| Todos los sensores en negro por más de 0.5 s (por ejemplo, carro levantado) | Se detiene | 2 parpadeos |
| Más de 1 s sin comandos válidos de la Pi (watchdog) | Se detiene | 3 parpadeos |
| `INICIAR` sin línea bajo el sensor | No arranca y responde con error | Fijo |

- El carro nunca vuelve a arrancar solo: siempre necesita INICIO.
- `moverLado()` recorta toda orden a PWM 548, que equivale a 4.5 V con la batería llena.

## Problemas y decisiones
- **El sensor solo detectaba el tape casi tocándolo:** no era una falla, sino su rango real de trabajo.
- **Se detenía en curvas o con rebotes del soporte:** de ahí salieron la tolerancia y la búsqueda por pulsos.
- **Se dañó un ESP32:** dejó de responder su memoria flash (`Manufacturer: ff`), con 3.33 V correctos en su pin 3V3, y se reemplazó. La causa más probable, sin confirmar, fue que 8 V llegaron a la línea de 3.3 V al reconectar la batería. Regla adoptada: el + de la batería va solo a VM, y se conecta de último.
- **La reactivación pasó de EN a INICIO:** el enunciado pide que el inicio venga de la interfaz.

## Resultado
INICIO y FIN funcionaron desde la interfaz en el carro.

## Pendiente
- Evaluar el seguimiento en el circuito completo.
- LF6 no tiene una interrupción de hardware propia. La rúbrica exige evidenciar interrupciones en el microcontrolador; la interrupción del sensor de color (entrada 01) debe volver al integrarlo.
- Una marca de la bifurcación que deje todos los sensores en negro por más de 0.5 s detendría el carro. Hay que considerarlo al diseñar la detección.

# 02. Prueba de motores y drivers (24/10/2026)

## Objetivo
Comprobar que los 4 motores responden con 2 drivers TB6612FNG y dejar calibrado el sentido de giro.

## Qué hace
`Demo_RuedasDrivers_Funcional.ino` corresponde a la "Prueba D2 calibrada". Repite esta secuencia, con 4 s por etapa:

1. Adelante lento (35 %).
2. Adelante rápido (70 %).
3. Freno.
4. Reversa lenta.
5. Freno.
6. Curva a la izquierda.
7. Curva a la derecha.
8. Giro en el sitio.
9. Rueda libre.

## Hardware
- 4 motores TT, uno por canal. Canal A = lado izquierdo; canal B = lado derecho.
- Las señales del ESP32 llegan a los dos drivers por puentes.
- **VM de los drivers:** 2 celdas 18650 en serie (8.4 V con carga completa).
- **VCC de los drivers:** 3.3 V del ESP32. Según la hoja de datos, un nivel alto debe ser al menos 0.7 × VCC, y con 5 V las señales de 3.3 V podrían no reconocerse.
- **ESP32:** alimentación aparte por USB, con tierra común.

## Conexiones
| Señal | GPIO |
|---|---|
| PWMA | 25 |
| AIN1 | 26 |
| AIN2 | 14 |
| PWMB | 13 |
| BIN1 | 16 |
| BIN2 | 17 |
| STBY | 4 |

## Decisiones
- **PWM a 20 kHz y 10 bits:** dentro del límite de la hoja de datos (100 kHz) y fuera del rango audible.
- **Tope de 716 de 1023 (70 %):** con 8.4 V da unos 5.9 V, por debajo del máximo de 6 V de los motores.
- **`INVERTIR_IZQUIERDA` e `INVERTIR_DERECHA` en `true`:** compensan cómo quedaron montados los motores.

## Problemas
- Un motor tenía la caja reductora defectuosa y se reemplazó.
- Las ruedas delanteras giraban al revés que las traseras. Se invirtieron los cables de los motores del driver 2.
- Luego un lado quedó invertido, y se corrigió con las constantes `INVERTIR_*`.

## Resultado
Los 4 motores completaron la secuencia con la calibración final.

## Nota
Esta prueba permite hasta 5.9 V. A partir de la entrada 03, el seguidor limita los motores a 4.5 V, porque esta velocidad resultó demasiado alta.

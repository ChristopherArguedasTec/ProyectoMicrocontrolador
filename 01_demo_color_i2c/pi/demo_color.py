#!/usr/bin/env python3
"""
Paso 10 (version simplificada) - La Pi muestra el color que lee el ESP32.
Usa el protocolo del paso 9 (comandos PING y LEER_COLOR).
"""

import time
from smbus2 import SMBus
import protocolo_esp32 as proto

BUS_I2C = 1
PERIODO_S = 0.5   # Una consulta cada medio segundo


def main():
    with SMBus(BUS_I2C) as bus:
        resultado, datos = proto.enviar_comando(bus, proto.CMD_PING)
        print(f"PING -> {proto.NOMBRES_RESULTADO[resultado]}: protocolo v{datos[0]}, "
              f"firmware v{datos[1]}, ID del sensor 0x{datos[2]:02X}")
        print("Mostrando el color leido por el ESP32. Ctrl+C para terminar.\n")

        errores = 0
        try:
            while True:
                try:
                    _, datos = proto.enviar_comando(bus, proto.CMD_LEER_COLOR)
                except (OSError, proto.ErrorProtocolo) as error:
                    errores += 1
                    print(f"Error de comunicacion #{errores}: {error}")
                    time.sleep(PERIODO_S)
                    continue

                color = proto.interpretar_color(datos)
                print(f"Lectura #{color['numero']:3d}: C={color['clear']:5d} "
                      f"R={color['rojo']:5d} G={color['verde']:5d} "
                      f"B={color['azul']:5d} -> {color['color']}")
                time.sleep(PERIODO_S)
        except KeyboardInterrupt:
            print(f"\nFin de la demo. Errores de comunicacion: {errores}")


if __name__ == "__main__":
    main()

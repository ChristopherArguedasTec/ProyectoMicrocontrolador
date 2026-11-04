#!/usr/bin/env python3
"""
Interfaz grafica (etapa 1): botones INICIO y FIN del carro seguidor de linea.
Corre en la Raspberry Pi con Tkinter y se ve desde la computadora por VNC.
Cada 0.2 s consulta el estado del ESP32; esa consulta tambien mantiene
vivo el watchdog del ESP32 (1 s).
"""

import tkinter as tk
from smbus2 import SMBus
import protocolo_esp32 as proto

BUS_I2C = 1
PERIODO_CONSULTA_MS = 200   # Bastante menor que el watchdog del ESP32 (1 s)
INTENTOS_FIN = 3            # FIN es la orden de seguridad: se reintenta

VERDE = "#2e7d32"
ROJO = "#c62828"
NARANJA = "#ef6c00"
GRIS = "#666666"
COLORES_ESTADO = {"DETENIDO": ROJO, "SIGUIENDO": VERDE, "BUSCANDO": NARANJA}


def enviar_fin(bus):
    """Envia FIN hasta INTENTOS_FIN veces. Devuelve True si el ESP32 lo confirmo."""
    for _ in range(INTENTOS_FIN):
        try:
            resultado, _ = proto.enviar_comando(bus, proto.CMD_DETENER)
        except (OSError, proto.ErrorProtocolo):
            continue
        if resultado == proto.RES_OK:
            return True
    return False


class InterfazCarro:
    """Ventana con los botones INICIO y FIN y el estado del carro."""

    def __init__(self, ventana, bus):
        self.ventana = ventana
        self.bus = bus
        ventana.title("Carro seguidor de linea")
        ventana.geometry("480x380")

        tk.Label(ventana, text="Carro seguidor de linea",
                 font=("Arial", 18, "bold")).pack(pady=12)

        marco_botones = tk.Frame(ventana)
        marco_botones.pack(pady=8)
        tk.Button(marco_botones, text="INICIO", command=self.iniciar,
                  font=("Arial", 16, "bold"), width=9, height=2,
                  bg=VERDE, fg="white").pack(side=tk.LEFT, padx=12)
        tk.Button(marco_botones, text="FIN", command=self.detener,
                  font=("Arial", 16, "bold"), width=9, height=2,
                  bg=ROJO, fg="white").pack(side=tk.LEFT, padx=12)

        self.etiqueta_estado = tk.Label(ventana, text="Estado: ---", font=("Arial", 16, "bold"))
        self.etiqueta_estado.pack(pady=(14, 2))
        self.etiqueta_motivo = tk.Label(ventana, text="", font=("Arial", 11))
        self.etiqueta_motivo.pack()
        self.etiqueta_mensaje = tk.Label(ventana, text="", font=("Arial", 12), wraplength=440)
        self.etiqueta_mensaje.pack(pady=10)
        self.etiqueta_conexion = tk.Label(ventana, text="", font=("Arial", 10), fg=GRIS,
                                          wraplength=440)
        self.etiqueta_conexion.pack(side=tk.BOTTOM, pady=8)

        resultado, datos = self.enviar(proto.CMD_PING)
        if resultado == proto.RES_OK:
            self.etiqueta_conexion.config(text=f"ESP32 conectado: protocolo v{datos[0]}, "
                                               f"firmware v{datos[1]}")

        self.consultar_estado()   # Primera consulta; despues se repite sola cada 0.2 s

    def enviar(self, cmd):
        """Envia un comando. Devuelve (resultado, datos) o (None, None) si fallo la comunicacion."""
        try:
            return proto.enviar_comando(self.bus, cmd)
        except (OSError, proto.ErrorProtocolo) as error:
            self.etiqueta_conexion.config(text=f"Error de comunicacion: {error}", fg=ROJO)
            return None, None

    def mostrar_mensaje(self, texto, color):
        self.etiqueta_mensaje.config(text=texto, fg=color)

    def iniciar(self):
        """Se ejecuta al presionar INICIO."""
        resultado, _ = self.enviar(proto.CMD_INICIAR)
        if resultado == proto.RES_OK:
            self.mostrar_mensaje("INICIO aceptado.", VERDE)
        elif resultado == proto.RES_ERR_SIN_LINEA:
            self.mostrar_mensaje("No hay linea bajo el sensor. Coloque el carro sobre el tape.", ROJO)
        elif resultado is not None:
            self.mostrar_mensaje(f"INICIO rechazado: "
                                 f"{proto.NOMBRES_RESULTADO.get(resultado, resultado)}", ROJO)
        else:
            self.mostrar_mensaje("No se pudo enviar INICIO.", ROJO)

    def detener(self):
        """Se ejecuta al presionar FIN."""
        if enviar_fin(self.bus):
            self.mostrar_mensaje("FIN aceptado.", "black")
        else:
            self.mostrar_mensaje("El ESP32 no confirmo el FIN. El watchdog lo detendra en 1 s.", ROJO)

    def consultar_estado(self):
        """Lee el estado del ESP32, actualiza la pantalla y se vuelve a programar."""
        resultado, datos = self.enviar(proto.CMD_LEER_ESTADO)
        if resultado == proto.RES_OK:
            estado = proto.interpretar_estado(datos)
            nombre = estado["estado"]
            self.etiqueta_estado.config(text=f"Estado: {nombre}",
                                        fg=COLORES_ESTADO.get(nombre, "black"))
            if nombre == "DETENIDO" and estado["motivo"] != "ninguno":
                self.etiqueta_motivo.config(text=f"Motivo del paro: {estado['motivo']}")
            else:
                self.etiqueta_motivo.config(text="")
        self.ventana.after(PERIODO_CONSULTA_MS, self.consultar_estado)


def main():
    with SMBus(BUS_I2C) as bus:
        ventana = tk.Tk()
        InterfazCarro(ventana, bus)
        try:
            ventana.mainloop()      # Atiende botones y consultas hasta cerrar la ventana
        finally:
            enviar_fin(bus)         # Siempre se detiene el carro al salir


if __name__ == "__main__":
    main()

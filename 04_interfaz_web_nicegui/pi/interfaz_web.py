#!/usr/bin/env python3
"""
Interfaz web (etapa 1): botones INICIO y FIN y estado del carro, con NiceGUI.
Corre en la Raspberry Pi; se abre desde el navegador de una computadora
en la misma red, en http://IP_DE_LA_PI:8080
Cada 0.2 s consulta el estado del ESP32; esa consulta tambien mantiene
vivo el watchdog del ESP32 (1 s).
"""

import time
from nicegui import app, ui
from smbus2 import SMBus
import protocolo_esp32 as proto

BUS_I2C = 1
PUERTO_WEB = 8080
PERIODO_CONSULTA_S = 0.2     # Bastante menor que el watchdog del ESP32 (1 s)
PERIODO_PANTALLA_S = 0.2     # Cada cuanto cada pagina actualiza lo que muestra
INTENTOS_FIN = 3             # FIN es la orden de seguridad: se reintenta
TIEMPO_SIN_PAGINA_S = 1.0    # Sin ninguna pagina abierta este tiempo: FIN

TEXTOS_ESTADO = {"---": "Estado desconocido", "DETENIDO": "Detenido", "SIGUIENDO": "Siguiendo la línea",
                 "BUSCANDO": "Buscando la línea", "FRENANDO": "Frenando",
                 "RETROCEDIENDO": "Retrocediendo"}
COLORES_ESTADO = {"DETENIDO": "text-red-700", "SIGUIENDO": "text-green-700",
                  "BUSCANDO": "text-orange-600"}

bus = SMBus(BUS_I2C)

# Lo ultimo que se sabe del carro. Es uno solo: todas las paginas abiertas muestran lo mismo.
carro = {"estado": "---", "motivo": "ninguno", "conectado": False, "ultimo_latido": 0.0}


def enviar_fin():
    """Envia FIN hasta INTENTOS_FIN veces. Devuelve True si el ESP32 lo confirmo."""
    for _ in range(INTENTOS_FIN):
        try:
            resultado, _ = proto.enviar_comando(bus, proto.CMD_DETENER)
        except (OSError, proto.ErrorProtocolo):
            continue
        if resultado == proto.RES_OK:
            return True
    return False


def consultar_estado():
    """Lee el estado del ESP32. Si el carro anda y no hay ninguna pagina abierta, lo detiene."""
    try:
        resultado, datos = proto.enviar_comando(bus, proto.CMD_LEER_ESTADO)
    except (OSError, proto.ErrorProtocolo):
        carro["conectado"] = False
        carro["estado"] = "---"     # Sin respuesta no se sabe como esta el carro
        return
    carro["conectado"] = True
    if resultado != proto.RES_OK:
        return

    estado = proto.interpretar_estado(datos)
    carro["estado"] = estado["estado"]
    carro["motivo"] = estado["motivo"]

    sin_pagina = time.monotonic() - carro["ultimo_latido"] > TIEMPO_SIN_PAGINA_S
    if carro["estado"] != "DETENIDO" and sin_pagina:
        print("Ninguna pagina abierta: se envia FIN.")
        enviar_fin()


def iniciar():
    """Se ejecuta al presionar INICIO."""
    try:
        resultado, _ = proto.enviar_comando(bus, proto.CMD_INICIAR)
    except (OSError, proto.ErrorProtocolo):
        ui.notify("No se pudo enviar INICIO.", type="negative")
        return
    if resultado == proto.RES_OK:
        ui.notify("INICIO aceptado.", type="positive")
    elif resultado == proto.RES_ERR_SIN_LINEA:
        ui.notify("No hay línea bajo el sensor. Coloque el carro sobre el tape.", type="negative")
    else:
        ui.notify(f"INICIO rechazado: {proto.NOMBRES_RESULTADO.get(resultado, resultado)}",
                  type="negative")


def detener():
    """Se ejecuta al presionar FIN."""
    if enviar_fin():
        ui.notify("FIN aceptado.", type="info")
    else:
        ui.notify("El ESP32 no confirmó el FIN. El watchdog lo detendrá en 1 s.", type="negative")


@ui.page("/")
def pagina():
    """Dibuja la pagina. Se ejecuta una vez por cada navegador que la abre."""
    ui.query("body").style("background-color: #f8fafc")

    with ui.row().classes("w-full max-w-3xl mx-auto items-center justify-between"):
        with ui.column().classes("gap-0"):
            ui.label("Control del carrito").classes("text-3xl font-bold text-slate-800")
            ui.label("Interfaz de control").classes("text-slate-500")
        conexion = ui.badge("---").props("outline")

    with ui.row().classes("w-full max-w-3xl mx-auto"):
        with ui.card().classes("w-80 rounded-2xl p-6 gap-3"):
            ui.label("Movimiento").classes("text-lg font-medium text-slate-800")
            texto_estado = ui.label("---").classes("text-2xl font-bold")
            texto_motivo = ui.label("").classes("text-sm text-slate-600")
            with ui.row().classes("gap-3 mt-2"):
                ui.button("INICIO", icon="play_arrow", on_click=iniciar, color="green-8") \
                    .classes("rounded-xl font-bold")
                ui.button("FIN", icon="stop", on_click=detener, color="red-7") \
                    .classes("rounded-xl font-bold")

    def actualizar():
        """Copia a esta pagina lo ultimo que se sabe del carro."""
        carro["ultimo_latido"] = time.monotonic()   # Hay al menos una pagina abierta

        if carro["conectado"]:
            conexion.set_text("ESP32 CONECTADO")
            conexion.props("color=green-8")
        else:
            conexion.set_text("SIN COMUNICACIÓN")
            conexion.props("color=red-7")

        nombre = carro["estado"]
        texto_estado.set_text(TEXTOS_ESTADO.get(nombre, nombre))
        texto_estado.classes(replace="text-2xl font-bold " + COLORES_ESTADO.get(nombre, ""))

        if nombre == "DETENIDO" and carro["motivo"] != "ninguno":
            texto_motivo.set_text(f"Motivo del paro: {carro['motivo']}")
        else:
            texto_motivo.set_text("")

    ui.timer(PERIODO_PANTALLA_S, actualizar)


app.timer(PERIODO_CONSULTA_S, consultar_estado)   # Una sola consulta, aunque haya varias paginas
app.on_shutdown(enviar_fin)                       # Siempre se detiene el carro al salir

ui.run(host="0.0.0.0", port=PUERTO_WEB, title="Control del carrito", reload=False, show=False)

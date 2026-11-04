"""
Protocolo de comunicacion Raspberry Pi (maestro) <-> ESP32 (esclavo 0x42).
Disenado en el paso 9: comando de 6 bytes -> respuesta de 14 bytes, con CRC-8.
"""

import time
from smbus2 import i2c_msg

# ---------- Parametros generales ----------
DIRECCION_ESP32 = 0x42
LONGITUD_COMANDO = 6
LONGITUD_RESPUESTA = 14
PAUSA_RESPUESTA_S = 0.01   # 10 ms para que el ESP32 cargue la respuesta

# ---------- Comandos ----------
CMD_PING = 0x01
CMD_LEER_ESTADO = 0x02
CMD_LEER_COLOR = 0x03
CMD_INICIAR = 0x10
CMD_DETENER = 0x11
CMD_CONFIG_ACCIONES = 0x20
CMD_CONFIG_BIFURCACION = 0x21
CMD_REINICIAR_CONTADORES = 0x22

# ---------- Codigos de resultado ----------
RES_OK = 0x00
RES_ERR_COMANDO = 0x01
RES_ERR_EN_MOVIMIENTO = 0x02
RES_ERR_PARAMETRO = 0x03
RES_ERR_CRC = 0x04

NOMBRES_RESULTADO = {
    RES_OK: "OK",
    RES_ERR_COMANDO: "ERR_COMANDO",
    RES_ERR_EN_MOVIMIENTO: "ERR_EN_MOVIMIENTO",
    RES_ERR_PARAMETRO: "ERR_PARAMETRO",
    RES_ERR_CRC: "ERR_CRC",
}

# ---------- Acciones, estados, colores y banderas ----------
ACCION_FRENAR = 0
ACCION_RETROCEDER = 1
ACCION_CAMBIAR_VELOCIDAD = 2

NOMBRES_ESTADO = {0: "DETENIDO", 1: "SIGUIENDO", 2: "FRENANDO", 3: "RETROCEDIENDO"}
NOMBRES_COLOR = {0: "ninguno", 1: "rojo", 2: "verde", 3: "azul"}

BANDERA_VELOCIDAD_ALTA = 0x01
BANDERA_BIFURCACION_DERECHA = 0x02

_secuencia = 0   # Ultimo numero de secuencia usado


class ErrorProtocolo(Exception):
    """La respuesta del ESP32 no paso las verificaciones (CRC o ecos)."""


def calcular_crc8(datos):
    """CRC-8 con polinomio 0x07 y valor inicial 0x00 (el mismo de SMBus)."""
    crc = 0x00
    for byte in datos:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def _siguiente_secuencia():
    """Devuelve el siguiente numero de secuencia (0-255, vuelve a 0 despues de 255)."""
    global _secuencia
    _secuencia = (_secuencia + 1) % 256
    return _secuencia


def leer_u16(datos, posicion):
    """Combina dos bytes little-endian (bajo, alto) en un numero de 16 bits."""
    return datos[posicion] | (datos[posicion + 1] << 8)


def enviar_comando(bus, cmd, p0=0, p1=0, p2=0):
    """
    Envia un comando de 6 bytes, espera y lee la respuesta de 14 bytes.
    Devuelve (resultado, datos), donde datos son los 10 bytes de datos.
    Lanza ErrorProtocolo si la respuesta no es valida.
    """
    seq = _siguiente_secuencia()
    comando = [cmd, seq, p0, p1, p2]
    comando.append(calcular_crc8(comando))

    bus.i2c_rdwr(i2c_msg.write(DIRECCION_ESP32, comando))   # Transaccion 1: escritura
    time.sleep(PAUSA_RESPUESTA_S)
    lectura = i2c_msg.read(DIRECCION_ESP32, LONGITUD_RESPUESTA)
    bus.i2c_rdwr(lectura)                                   # Transaccion 2: lectura
    respuesta = list(lectura)

    if calcular_crc8(respuesta[:13]) != respuesta[13]:
        raise ErrorProtocolo(f"CRC incorrecto en la respuesta: {respuesta}")
    if respuesta[0] != cmd:
        raise ErrorProtocolo(f"Eco de CMD incorrecto: enviado 0x{cmd:02X}, "
                             f"recibido 0x{respuesta[0]:02X}")
    if respuesta[2] != seq:
        raise ErrorProtocolo(f"Respuesta vieja: SEQ enviado {seq}, recibido {respuesta[2]}")

    return respuesta[1], respuesta[3:13]


def interpretar_estado(datos):
    """Convierte los 10 bytes de datos de LEER_ESTADO en un diccionario."""
    return {
        "estado": NOMBRES_ESTADO.get(datos[0], "?"),
        "velocidad_alta": bool(datos[1] & BANDERA_VELOCIDAD_ALTA),
        "bifurcacion_derecha": bool(datos[1] & BANDERA_BIFURCACION_DERECHA),
        "ultimo_color": NOMBRES_COLOR.get(datos[2], "?"),
        "rojo": leer_u16(datos, 4),
        "verde": leer_u16(datos, 6),
        "azul": leer_u16(datos, 8),
    }


def interpretar_color(datos):
    """Convierte los 10 bytes de datos de LEER_COLOR en un diccionario."""
    return {
        "clear": leer_u16(datos, 0),
        "rojo": leer_u16(datos, 2),
        "verde": leer_u16(datos, 4),
        "azul": leer_u16(datos, 6),
        "color": NOMBRES_COLOR.get(datos[8], "?"),
        "numero": datos[9],
    }

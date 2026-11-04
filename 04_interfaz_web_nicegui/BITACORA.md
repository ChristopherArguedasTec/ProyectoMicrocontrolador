# 04. Interfaz web con NiceGUI (03/11/2026)

## Objetivo
Controlar el carro con botones desde el navegador de una computadora, sin depender de VNC.

## Qué hace
`interfaz_web.py` corre en la Raspberry Pi y publica una página en `http://IP_DE_LA_PI:8080`. Muestra:

- Los botones **INICIO** y **FIN**.
- El estado del carro y el motivo del último paro.
- Si hay comunicación con el ESP32.

Usa el firmware de la entrada 03.

## Cómo se ejecuta
En la Raspberry Pi, por SSH:

```bash
cd ~/proyecto_carrito
source venv/bin/activate
python interfaz_web.py
```

La librería se instala con `pip install nicegui`, dentro del entorno virtual.

## Decisiones
- **NiceGUI en lugar de Tkinter y VNC:** RealVNC mostró un aviso de prueba de 7 días, y en el navegador la resolución deja de ser un problema. Además, otro integrante puede abrir la interfaz desde su computadora. El diseño visual se basó en el de un compañero del equipo.
- **Una sola consulta al ESP32:** `app.timer` la hace cada 0.2 s, aunque haya varias páginas abiertas. Todas muestran el mismo estado.
- **Estado desconocido:** si el ESP32 no responde, la página lo dice en lugar de mostrar el último estado conocido.

## Seguridad
- Si nadie tiene la página abierta y el carro se mueve, la Pi envía FIN.
- Recargar la página no detiene el carro.
- Al cerrar el programa con Ctrl+C se envía FIN.
- Si el programa se cae, el watchdog del ESP32 detiene el carro en 1 s.

## Resultado
Primero se probó en una computadora con un ESP32 simulado y NiceGUI 3.19.0. Después funcionó en la Raspberry Pi con el carro.

## Pendiente
- Confirmar con el profesor que es aceptable ver la interfaz desde el navegador de otra computadora, si corre en la Pi.
- Agregar la asociación color-acción, el mensaje de error con el carro en movimiento, los contadores en tiempo real y la elección de la bifurcación.

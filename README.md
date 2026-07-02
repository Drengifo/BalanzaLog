# BalanzaLog

**Sistema autónomo para pesaje prolongado de muestras.**

BalanzaLog es un equipo diseñado y fabricado para registrar el peso de una muestra durante periodos largos, reduciendo la intervención manual y permitiendo ensayos más ordenados y repetibles. El sistema combina una celda de carga con electrónica de control, una pantalla local, registro automático de datos y un actuador que permite operar distintos modos de medición.

La idea del proyecto es disponer de una plataforma simple para ensayos de pesaje continuo, seguimiento de variaciones de masa y pruebas donde conviene minimizar efectos asociados a cargas permanentes, como deformaciones o creep del sistema de medición.

## Qué hace

- Registra peso de forma manual o automática.
- Permite configurar intervalos de medición.
- Guarda datos localmente para análisis posterior.
- Usa una celda de carga con módulo HX711.
- Integra actuador lineal para subir/bajar el sistema de apoyo.
- Puede operar desde Arduino Nano y, en la versión con Raspberry Pi, registrar datos en archivos `.txt` mediante Python.

## Hardware principal

- Arduino Nano
- Celda de carga + HX711
- Actuador lineal con driver
- Pantalla OLED I2C
- Botón de operación
- LED de estado
- Raspberry Pi para registro prolongado de datos

## Software

- Firmware Arduino para lectura de celda, control del actuador y operación local.
- Scripts Python para Raspberry Pi.
- Servicio `systemd` para ejecución automática.
- Registro de datos en archivos de texto.

## Estructura del repositorio

```text
Firmware/
├── BalanzaLogV1/              # Firmware base Arduino
├── Balanza-RP/                # Firmware + scripts para Raspberry Pi
│   ├── balanza_rpi_modo_ciclo.py
│   ├── balanza_rpi_modo_creep.py
│   ├── balanza-log.service
│   └── INSTALACION_RPI.txt
└── balanza-ensayo.ino         # Versión de ensayo continuo

docs/
├── 01_descripcion_general.md
├── 02_guia_rapida.md
└── img/                       # Imágenes del diseño 3D, montaje y pruebas
```

## Videos

- [Video demostrativo 1](https://www.youtube.com/watch?v=8QgUtvVRpH8)
- [Video demostrativo 2](https://www.youtube.com/watch?v=CKyvn4j7elw)

## Documentación rápida

- [Descripción general](docs/01_descripcion_general.md)
- [Guía rápida de uso](docs/02_guia_rapida.md)

## Estado del proyecto

Proyecto funcional en etapa de documentación y ordenamiento de repositorio. El objetivo no es entregar una guía extensa, sino dejar una base clara para entender, replicar o continuar el desarrollo del equipo.

## Licencia

Este repositorio utiliza licencia MIT.

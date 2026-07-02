# 01. Descripción general

BalanzaLog es un equipo autónomo para realizar pesaje prolongado de muestras. Fue diseñado para registrar variaciones de masa durante largos periodos, disminuyendo la necesidad de intervención manual y ordenando el proceso de adquisición de datos.

El sistema integra una celda de carga con módulo HX711, un Arduino Nano para lectura y control local, una pantalla OLED para visualización, un botón de operación, un LED de estado y un actuador lineal que permite subir o bajar el apoyo de la muestra según el modo de ensayo.

## Objetivo del equipo

El objetivo principal es automatizar mediciones de peso en el tiempo. Esto permite trabajar con muestras que requieren seguimiento prolongado y evitar errores asociados a lecturas manuales, manipulación repetida o carga permanente sobre el sistema de medición.

## Arquitectura general

```text
Muestra
  ↓
Celda de carga + HX711
  ↓
Arduino Nano
  ├── Pantalla OLED
  ├── Botón / LED
  ├── Driver de actuador lineal
  └── Comunicación serial con Raspberry Pi
        ↓
      Python + systemd
        ↓
      Archivos .txt de datos
```

## Modos de operación

### Modo ciclo

Realiza una secuencia automática de medición cada cierto intervalo. En este modo, el actuador puede mover el sistema entre mediciones para reducir el efecto de carga permanente sobre la celda o el montaje.

### Modo creep

Permite mantener el sistema en condición de medición y registrar lecturas periódicas sin mover el actuador durante el ensayo. Es útil cuando se desea observar la evolución del peso manteniendo una configuración fija.

## Datos registrados

La versión con Raspberry Pi guarda archivos `.txt` en la carpeta de datos. Cada archivo se genera con fecha y hora de inicio, e incluye columnas como tiempo local, peso medido, modo de operación y notas del ciclo.

## Uso previsto

Este repositorio no busca ser un producto comercial terminado, sino dejar ordenado el firmware, los scripts y una documentación mínima para continuar el desarrollo, replicar el equipo o usarlo como base en ensayos futuros.

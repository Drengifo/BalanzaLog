# 02. Guía rápida de uso

Esta guía resume el uso básico de BalanzaLog. La instalación detallada para Raspberry Pi está en `Firmware/Balanza-RP/INSTALACION_RPI.txt`.

## 1. Cargar firmware

Cargar en el Arduino Nano el firmware correspondiente al modo que se desea probar. Las versiones disponibles están en:

```text
Firmware/BalanzaLogV1/
Firmware/Balanza-RP/
Firmware/balanza-ensayo.ino
```

## 2. Conexiones principales

```text
HX711 DT      -> D5
HX711 SCK     -> D6
Driver IN1    -> D7
Driver IN2    -> D8
Botón         -> D9  con INPUT_PULLUP
LED           -> D10
OLED SDA      -> A4
OLED SCL      -> A5
```

## 3. Uso local

Desde el equipo se puede realizar pesaje manual, activar modo automático, configurar intervalo y visualizar estado básico en pantalla OLED.

## 4. Uso con Raspberry Pi

La Raspberry Pi ejecuta un script Python que se comunica por serial con el Arduino y guarda los datos en archivos `.txt`.

Crear carpeta de trabajo:

```bash
mkdir -p ~/balanza-rpi/data ~/balanza-rpi/state ~/balanza-rpi/logs
```

Instalar dependencias:

```bash
cd ~/balanza-rpi
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Seleccionar modo activo:

```bash
cp balanza_rpi_modo_ciclo.py balanza_activo.py
# o
cp balanza_rpi_modo_creep.py balanza_activo.py
```

Reiniciar servicio:

```bash
sudo systemctl restart balanza-log.service
```

Ver estado:

```bash
sudo systemctl status balanza-log.service
```

Ver logs:

```bash
journalctl -u balanza-log.service -f
```

## 5. Salida de datos

Los archivos generados quedan en:

```text
~/balanza-rpi/data/
```

Cada arranque del script crea un nuevo archivo con marca de tiempo.

## 6. Notas

- Revisar el factor de calibración de la celda antes de un ensayo real.
- Confirmar que el actuador se mueve libremente antes de dejar el sistema funcionando solo.
- Para ensayos largos, alimentar la Raspberry Pi y el Arduino con una fuente estable.
- Guardar respaldo de los archivos `.txt` al finalizar cada prueba.

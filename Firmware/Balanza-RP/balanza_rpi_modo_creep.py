#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Modo creep para Raspberry Pi + Balanza-Log.
Desarrollado por DRengifo (LoRe-Ingenieria.cl)

Rutina:
- al iniciar el servicio:
    1) sube el actuador
    2) espera hasta completar UP_TOTAL_WAIT_S
    3) hace tare una sola vez para este arranque
- luego, cada INTERVAL_MIN minutos:
    4) lee la celda sin mover el actuador (READCELL)
    5) guarda el valor en un .txt

Notas:
- este modo deja la balanza arriba durante el ensayo
- si el sistema completo se reinicia, el firmware actual del Arduino ejecuta
  una bajada en setup, por lo que la continuidad física del creep puede romperse
- por seguridad, este script vuelve a inicializar ARRIBA + TARE al arrancar
"""

from __future__ import annotations

import json
import os
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Callable, Optional

import serial
from serial.tools import list_ports


# ============================================================
# ===================== CONFIGURACION =========================
# ============================================================

APP_NAME = "balanza_rpi_modo_creep"
BASE_DIR = Path.home() / "balanza-rpi"
DATA_DIR = BASE_DIR / "data"
STATE_DIR = BASE_DIR / "state"
LOG_DIR = BASE_DIR / "logs"
STATE_FILE = STATE_DIR / "modo_creep_state.json"

SERIAL_PORT: Optional[str] = None
BAUDRATE = 115200
SERIAL_TIMEOUT_S = 0.20
SERIAL_OPEN_SETTLE_S = 2.0
RECONNECT_DELAY_S = 5.0

INTERVAL_MIN = 10
UP_TOTAL_WAIT_S = 7.0
POST_TARE_SETTLE_S = 1.0
FIRST_RUN_WAIT_FULL_INTERVAL = True

TXT_PREFIX = "balanza_creep"
ENCODING = "utf-8"


# ============================================================
# ===================== UTILIDADES ===========================
# ============================================================


def now_local_str() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def now_file_str() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def ensure_dirs() -> None:
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)


class StateStore:
    def __init__(self, path: Path) -> None:
        self.path = path

    def load(self) -> dict:
        if not self.path.exists():
            return {}
        try:
            return json.loads(self.path.read_text(encoding=ENCODING))
        except Exception:
            return {}

    def save(self, data: dict) -> None:
        tmp = self.path.with_suffix(self.path.suffix + ".tmp")
        tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding=ENCODING)
        os.replace(tmp, self.path)


class DataLogger:
    def __init__(self, base_dir: Path) -> None:
        self.base_dir = base_dir
        self.path = self.base_dir / f"{TXT_PREFIX}_{now_file_str()}.txt"
        self.fh = self.path.open("a", encoding=ENCODING, buffering=1)
        self._write_header()

    def _write_header(self) -> None:
        self.fh.write(f"# app={APP_NAME}\n")
        self.fh.write(f"# started_local={now_local_str()}\n")
        self.fh.write("# columns=ts_local,epoch_s,mode,status,cell_weight_g,notes\n")
        self._sync()

    def _sync(self) -> None:
        self.fh.flush()
        os.fsync(self.fh.fileno())

    def line(self, text: str) -> None:
        self.fh.write(text.rstrip("\n") + "\n")
        self._sync()

    def event(self, status: str, notes: str = "") -> None:
        self.line(f"# {now_local_str()} | {status} | {notes}")

    def sample(self, mode: str, status: str, cell_weight_g: float, notes: str = "") -> None:
        epoch_s = time.time()
        self.line(f"{now_local_str()},{epoch_s:.3f},{mode},{status},{cell_weight_g:.3f},{notes}")


# ============================================================
# ===================== SERIAL ===============================
# ============================================================


@dataclass
class SerialResult:
    ok: bool
    line: str = ""


class BalanzaSerial:
    def __init__(self) -> None:
        self.ser: Optional[serial.Serial] = None
        self.port_name: Optional[str] = None

    def _autodetect_port(self) -> str:
        ports = list(list_ports.comports())
        preferred = []
        others = []

        for p in ports:
            dev = p.device or ""
            desc = (p.description or "").lower()
            if dev.startswith("/dev/ttyUSB") or dev.startswith("/dev/ttyACM"):
                preferred.append(dev)
            elif "usb" in desc or "arduino" in desc or "ch340" in desc or "cp210" in desc:
                others.append(dev)

        candidates = preferred + others
        if not candidates:
            raise RuntimeError("No se encontró puerto serial USB/ACM para la balanza.")
        return candidates[0]

    def open(self) -> None:
        if self.ser and self.ser.is_open:
            return

        port = SERIAL_PORT or self._autodetect_port()
        self.ser = serial.Serial(
            port=port,
            baudrate=BAUDRATE,
            timeout=SERIAL_TIMEOUT_S,
            write_timeout=2.0,
            dsrdtr=False,
            rtscts=False,
        )
        self.port_name = port

        try:
            self.ser.dtr = False
            self.ser.rts = False
        except Exception:
            pass

        time.sleep(SERIAL_OPEN_SETTLE_S)
        self._drain_input(1.0)

    def close(self) -> None:
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.port_name = None

    def _require_open(self) -> serial.Serial:
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Puerto serial no abierto.")
        return self.ser

    def _drain_input(self, duration_s: float = 0.5) -> None:
        ser = self._require_open()
        t0 = time.time()
        while time.time() - t0 < duration_s:
            try:
                _ = ser.readline()
            except Exception:
                break

    def _readline(self) -> str:
        ser = self._require_open()
        raw = ser.readline()
        if not raw:
            return ""
        return raw.decode(ENCODING, errors="replace").strip()

    def _write_command(self, cmd: str) -> None:
        ser = self._require_open()
        payload = (cmd.strip() + "\n").encode(ENCODING)
        ser.write(payload)
        ser.flush()

    def wait_for(self, predicate: Callable[[str], bool], timeout_s: float) -> SerialResult:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            line = self._readline()
            if not line:
                continue
            print(f"[SERIAL] {line}", flush=True)
            if line.startswith("ERROR:"):
                return SerialResult(False, line)
            if predicate(line):
                return SerialResult(True, line)
        return SerialResult(False, "TIMEOUT")

    def command_expect(self, cmd: str, predicate: Callable[[str], bool], timeout_s: float) -> SerialResult:
        self._drain_input(0.2)
        print(f"[CMD] {cmd}", flush=True)
        self._write_command(cmd)
        return self.wait_for(predicate, timeout_s)

    def command_total_wait(self, cmd: str, expected_text: str, total_wait_s: float, timeout_s: float = 10.0) -> None:
        t0 = time.time()
        result = self.command_expect(cmd, lambda line: expected_text in line, timeout_s)
        if not result.ok:
            raise RuntimeError(f"Fallo comando {cmd}: {result.line}")
        elapsed = time.time() - t0
        remaining = total_wait_s - elapsed
        if remaining > 0:
            time.sleep(remaining)

    def tare(self) -> None:
        result = self.command_expect("TARE", lambda line: "OK: TARA REALIZADA" in line, timeout_s=10.0)
        if not result.ok:
            raise RuntimeError(f"Fallo TARE: {result.line}")
        time.sleep(POST_TARE_SETTLE_S)

    def read_cell(self) -> float:
        result = self.command_expect(
            "READCELL",
            lambda line: line.startswith("CELL_G,"),
            timeout_s=6.0,
        )
        if not result.ok:
            raise RuntimeError(f"No se pudo leer READCELL: {result.line}")
        return float(result.line.split(",", 1)[1].strip())


# ============================================================
# ===================== LOGICA DE ENSAYO =====================
# ============================================================


def initialize_creep_mode(bal: BalanzaSerial, logger: DataLogger) -> None:
    bal.command_total_wait("ACT UP", "ACT: UP", UP_TOTAL_WAIT_S, timeout_s=4.0)
    logger.event("UP_OK", f"wait_total_s={UP_TOTAL_WAIT_S}")

    bal.tare()
    logger.event("TARE_OK", "tare inicial realizado")



def connect_device(logger: DataLogger) -> BalanzaSerial:
    bal = BalanzaSerial()
    bal.open()
    logger.event("SERIAL_OK", f"port={bal.port_name}")
    initialize_creep_mode(bal, logger)
    return bal



def compute_next_run_epoch(state: dict) -> float:
    saved = state.get("next_run_epoch")
    now = time.time()

    if isinstance(saved, (int, float)) and saved > 0:
        return float(saved)

    if FIRST_RUN_WAIT_FULL_INTERVAL:
        return now + INTERVAL_MIN * 60.0
    return now


# ============================================================
# ===================== MAIN =================================
# ============================================================


def main() -> int:
    ensure_dirs()
    logger = DataLogger(DATA_DIR)
    state_store = StateStore(STATE_FILE)
    state = state_store.load()

    next_run_epoch = compute_next_run_epoch(state)
    state_store.save(
        {
            "mode": "creep",
            "interval_min": INTERVAL_MIN,
            "next_run_epoch": next_run_epoch,
            "updated_local": now_local_str(),
        }
    )

    logger.event("START", f"data_file={logger.path.name}")
    logger.event("SCHEDULE", f"next_run_local={datetime.fromtimestamp(next_run_epoch).strftime('%Y-%m-%d %H:%M:%S')}")

    bal: Optional[BalanzaSerial] = None

    while True:
        try:
            if bal is None:
                bal = connect_device(logger)

            now = time.time()
            if now < next_run_epoch:
                time.sleep(min(1.0, next_run_epoch - now))
                continue

            logger.event("READ_START", "leyendo celda sin mover")
            cell_weight_g = bal.read_cell()
            logger.sample(mode="creep", status="OK", cell_weight_g=cell_weight_g, notes="lectura sin movimiento")
            logger.event("READ_END", f"cell_weight_g={cell_weight_g:.3f}")

            next_run_epoch = time.time() + INTERVAL_MIN * 60.0
            state_store.save(
                {
                    "mode": "creep",
                    "interval_min": INTERVAL_MIN,
                    "next_run_epoch": next_run_epoch,
                    "updated_local": now_local_str(),
                }
            )

        except KeyboardInterrupt:
            logger.event("STOP", "interrumpido por teclado")
            if bal:
                bal.close()
            return 0

        except Exception as exc:
            logger.event("ERROR", str(exc))
            if bal:
                bal.close()
                bal = None

            recovery_next = time.time() + RECONNECT_DELAY_S
            next_run_epoch = min(next_run_epoch, recovery_next)
            state_store.save(
                {
                    "mode": "creep",
                    "interval_min": INTERVAL_MIN,
                    "next_run_epoch": next_run_epoch,
                    "updated_local": now_local_str(),
                    "last_error": str(exc),
                }
            )
            time.sleep(RECONNECT_DELAY_S)


if __name__ == "__main__":
    sys.exit(main())

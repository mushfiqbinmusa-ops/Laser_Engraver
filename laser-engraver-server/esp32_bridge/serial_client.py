from __future__ import annotations

from typing import Optional

import serial


class ESP32SerialClient:
    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self._serial: Optional[serial.Serial] = None

    def connect(self) -> None:
        self._serial = serial.Serial(self.port, self.baudrate, timeout=1)

    def send_line(self, line: str) -> str:
        if self._serial is None or not self._serial.is_open:
            raise ConnectionError("ESP32 serial client is not connected")
        self._serial.write((line.strip() + "\n").encode("ascii"))
        self._serial.flush()
        try:
            response = self._serial.readline().decode("utf-8", errors="replace").strip()
        except Exception:
            response = "ok"
        return response or "ok"

    def connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    def close(self) -> None:
        if self._serial is not None and self._serial.is_open:
            self._serial.close()
        self._serial = None

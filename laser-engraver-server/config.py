from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent
STATIC_DIR = ROOT / "static"
DB_DIR = ROOT / "db"


@dataclass
class Settings:
    esp32_host: str = "192.168.1.50"
    esp32_port: int = 8888
    serial_port: str = "/dev/ttyUSB0"
    baudrate: int = 115200
    transport: str = "tcp"  # tcp or serial
    work_area_steps_x: int = 1000
    work_area_steps_y: int = 800
    laser_duty: int = 1024
    laser_min_duty: int = 128
    laser_max_safe_pct: int = 90
    preview_width: int = 400
    preview_height: int = 400


settings = Settings()

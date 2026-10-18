from pathlib import Path
from pydantic_settings import BaseSettings, SettingsConfigDict

class Settings(BaseSettings):
    app_name: str = "Laser XY Scanner Control Service"
    version: str = "1.0.0"
    debug: bool = False
    
    # Database
    db_path: Path = Path("jobs.sqlite3")
    
    # Bridge Transport Configuration
    esp32_host: str = "127.0.0.1" # Default to localhost / simulator
    esp32_port: int = 8888
    serial_port: str = "/dev/ttyACM0"
    baudrate: int = 115200
    transport: str = "serial" # Default to serial for physical hardware control

    
    # Machine Defaults & Safety Limits
    work_area_width_mm: float = 50.0
    work_area_height_mm: float = 50.0
    steps_per_mm_x: float = 80.0
    steps_per_mm_y: float = 80.0
    max_speed_sps: int = 25000
    default_jog_speed_sps: int = 100
    laser_max_pct: int = 90
    laser_max_duty: int = 3685

    model_config = SettingsConfigDict(
        env_file=".env",
        extra="ignore"
    )

settings = Settings()

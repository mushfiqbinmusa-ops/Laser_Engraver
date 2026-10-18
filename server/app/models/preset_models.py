from typing import Optional
from pydantic import BaseModel, Field

class MachineConfigModel(BaseModel):
    id: str = "default"
    steps_per_unit_x: float = Field(default=80.0, gt=0)
    steps_per_unit_y: float = Field(default=80.0, gt=0)
    work_area_width_mm: float = Field(default=50.0, gt=0)
    work_area_height_mm: float = Field(default=50.0, gt=0)
    max_travel_steps_x: int = Field(default=4096, gt=0)
    max_travel_steps_y: int = Field(default=4096, gt=0)
    default_speed_sps: int = Field(default=100, gt=0, le=25000)
    max_speed_sps: int = Field(default=25000, gt=0, le=25000)
    laser_ceiling_pct: int = Field(default=90, ge=1, le=100)
    laser_ceiling_duty: int = Field(default=3685, ge=1, le=4095)

class PresetModel(BaseModel):
    id: str
    name: str
    description: Optional[str] = None
    travel_speed_sps: int = Field(default=200, gt=0, le=25000)
    engrave_speed_sps: int = Field(default=100, gt=0, le=25000)
    laser_power_pct: int = Field(default=15, ge=0, le=90)
    laser_duty: int = Field(default=614, ge=0, le=3685)
    passes: int = Field(default=1, ge=1, le=20)
    is_builtin: bool = False

class PresetCreate(BaseModel):
    name: str
    description: Optional[str] = None
    travel_speed_sps: int = Field(default=200, gt=0, le=25000)
    engrave_speed_sps: int = Field(default=100, gt=0, le=25000)
    laser_power_pct: int = Field(default=15, ge=0, le=90)
    passes: int = Field(default=1, ge=1, le=20)

class PresetUpdate(BaseModel):
    name: Optional[str] = None
    description: Optional[str] = None
    travel_speed_sps: Optional[int] = Field(default=None, gt=0, le=25000)
    engrave_speed_sps: Optional[int] = Field(default=None, gt=0, le=25000)
    laser_power_pct: Optional[int] = Field(default=None, ge=0, le=90)
    passes: Optional[int] = Field(default=None, ge=1, le=20)

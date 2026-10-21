from typing import Optional, Literal
from pydantic import BaseModel, Field
from datetime import datetime

class ToolpathSegment(BaseModel):
    seg_idx: int
    dx: int
    dy: int
    speed_sps: int
    duty: int # 0 for rapid transit, >0 for cutting
    is_cut: bool

class PreparedJobPlan(BaseModel):
    job_id: str
    name: str
    plan_hash: str
    # Step coordinates below are expressed at this microstep mode (1/16).
    reference_microstep_mode: int = 16
    total_segments: int
    estimated_duration_s: float
    total_travel_distance_steps: int
    total_cut_distance_steps: int
    segments: list[ToolpathSegment]

class JobResponse(BaseModel):
    id: str
    name: str
    source_filename: Optional[str] = None
    source_hash: str
    status: Literal["draft", "prepared", "running", "completed", "canceled", "failed", "unknown"]
    total_segments: int
    completed_segments: int
    has_preview: bool
    created_at: str
    updated_at: str

class ParametricShapeCreate(BaseModel):
    shape_type: Literal["square", "rectangle", "circle", "grid"]
    name: Optional[str] = None
    width_mm: Optional[float] = Field(default=20.0, gt=0, le=50.0)
    height_mm: Optional[float] = Field(default=20.0, gt=0, le=50.0)
    radius_mm: Optional[float] = Field(default=10.0, gt=0, le=25.0)
    grid_rows: Optional[int] = Field(default=5, ge=2, le=10)
    grid_cols: Optional[int] = Field(default=5, ge=2, le=10)
    preset_id: str = "preset-wood"
    override_laser_power_pct: Optional[int] = Field(default=None, ge=0, le=90)

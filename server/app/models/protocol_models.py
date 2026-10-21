from typing import Optional, Literal
from pydantic import BaseModel, Field
from app.core.laser import SAFETY_MAX_DUTY

class BaseRequest(BaseModel):
    v: int = Field(default=1, description="Protocol version, strictly 1")
    id: Optional[str] = Field(default="", description="Request correlation ID")
    type: str = Field(default="", description="Command identifier")

class PingRequest(BaseRequest):
    type: Literal["system.ping"] = "system.ping"

class SnapshotRequest(BaseRequest):
    type: Literal["system.snapshot"] = "system.snapshot"

class StopRequest(BaseRequest):
    type: Literal["system.stop"] = "system.stop"

class EStopRequest(BaseRequest):
    type: Literal["system.estop"] = "system.estop"

class ResetFaultRequest(BaseRequest):
    type: Literal["system.reset_fault"] = "system.reset_fault"

class DriverSetEnabledRequest(BaseRequest):
    type: Literal["driver.set_enabled"] = "driver.set_enabled"
    enabled: bool

class DriverSetMicrostepRequest(BaseRequest):
    type: Literal["driver.set_microstep"] = "driver.set_microstep"
    mode: Literal[1, 2, 4, 8, 16]

class HomeStartRequest(BaseRequest):
    type: Literal["home.start"] = "home.start"
    axis: Literal["all", "x", "y"] = "all"

class MotionJogRequest(BaseRequest):
    type: Literal["motion.jog"] = "motion.jog"
    axis: Literal["x", "y"]
    steps: int
    speed_sps: Optional[int] = 100

class MotionMoveAbsRequest(BaseRequest):
    type: Literal["motion.move_abs"] = "motion.move_abs"
    x: int
    y: int
    speed_sps: Optional[int] = 150

class LaserPulseRequest(BaseRequest):
    type: Literal["laser.pulse"] = "laser.pulse"
    duty: int = Field(ge=0, le=SAFETY_MAX_DUTY, description="LEDC duty cycle clamped <= 90%")
    duration_ms: int = Field(ge=1, le=500, description="Guarded pulse duration ms")

class LaserSetDutyRequest(BaseRequest):
    type: Literal["laser.set_duty"] = "laser.set_duty"
    duty: int = Field(ge=0, le=SAFETY_MAX_DUTY)

class JobBeginRequest(BaseRequest):
    type: Literal["job.begin"] = "job.begin"
    job_id: str
    total_segments: int

class JobSegmentRequest(BaseRequest):
    type: Literal["job.segment"] = "job.segment"
    job_id: str
    seg_idx: int
    dx: int
    dy: int
    speed_sps: int
    duty: int

class JobEndRequest(BaseRequest):
    type: Literal["job.end"] = "job.end"
    job_id: str

# Responses and Telemetry
class AckResponse(BaseModel):
    v: int = 1
    id: str
    type: Literal["ack"] = "ack"
    accepted: bool
    code: str
    operation_id: Optional[str] = None

class OperationEvent(BaseModel):
    v: int = 1
    type: str
    operation_id: str
    kind: Optional[str] = None
    phase: Optional[str] = None
    steps_completed: Optional[int] = None
    result: Optional[str] = None
    homed: Optional[dict[str, bool]] = None
    pos: Optional[dict[str, int]] = None

class SensorReadout(BaseModel):
    x_raw: int
    x_filtered: int
    y_raw: int
    y_filtered: int

class LaserReadout(BaseModel):
    duty: int
    active: bool
    hard_clamped: bool

class SystemSnapshot(BaseModel):
    v: int = 1
    type: Literal["system.snapshot"] = "system.snapshot"
    state: str
    firmware_version: str
    protocol_version: int
    profile_id: str
    homed: dict[str, bool]
    pos: dict[str, int]
    pos_valid: bool
    sensors: SensorReadout
    laser: LaserReadout
    drivers_enabled: bool
    microstep_mode: Optional[int] = 16
    fault: Optional[str] = None

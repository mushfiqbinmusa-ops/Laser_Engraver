import logging
from typing import Optional
from pydantic import BaseModel
from fastapi import APIRouter, HTTPException, Depends
from app.models.protocol_models import (
    AckResponse, SystemSnapshot, MotionJogRequest, MotionMoveAbsRequest,
    HomeStartRequest, LaserPulseRequest, DriverSetEnabledRequest,
    DriverSetMicrostepRequest
)

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/machine", tags=["machine"])

def get_disp():
    from app.main import get_dispatcher
    d = get_dispatcher()
    if not d:
        raise HTTPException(status_code=503, detail="Controller dispatcher not initialized")
    return d


def _read_machine_config() -> dict:
    try:
        from app.db.database import get_db
        with get_db() as conn:
            row = conn.execute(
                "SELECT * FROM machine_config WHERE id = 'default';"
            ).fetchone()
            if row:
                return dict(row)
    except Exception:
        logger.debug("Machine config unavailable", exc_info=True)
    return {}


def _live_travel_limits(dispatcher) -> tuple[float, float]:
    """Configured travel bounds scaled from 1/16 reference steps to live mode."""
    from app.core.kinematics import REFERENCE_MICROSTEP_MODE
    cfg = _read_machine_config()
    max_x = float(cfg.get("max_travel_steps_x", 4096))
    max_y = float(cfg.get("max_travel_steps_y", 4096))
    snap = dispatcher.latest_snapshot
    mode = int(getattr(snap, "microstep_mode", REFERENCE_MICROSTEP_MODE) or REFERENCE_MICROSTEP_MODE)
    scale = mode / REFERENCE_MICROSTEP_MODE
    return max_x * scale, max_y * scale

class ConnectionConfigRequest(BaseModel):
    # Omitted fields fall back to the server's current settings instead of
    # silently forcing a transport (previously defaulted to TCP 192.168.4.1).
    transport: Optional[str] = None
    host: Optional[str] = None
    port: Optional[int] = None
    serial_port: Optional[str] = None
    baudrate: Optional[int] = None

@router.get("/status")
async def get_status(dispatcher=Depends(get_disp)):
    if not dispatcher.is_fresh() and dispatcher.bridge.is_connected():
        try:
            await dispatcher.send_command({"type": "system.snapshot"}, timeout_s=0.5)
        except Exception:
            pass
    return {
        "connected": dispatcher.bridge.is_connected(),
        "fresh": dispatcher.is_fresh(),
        "snapshot": dispatcher.get_effective_snapshot(),
        "last_seen_epoch": dispatcher.last_snapshot_time
    }

@router.post("/snapshot")
async def trigger_snapshot(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.snapshot"}, timeout_s=1.5)
        return {
            "ack": ack,
            "snapshot": dispatcher.get_effective_snapshot(),
            "connected": dispatcher.bridge.is_connected(),
            "fresh": dispatcher.is_fresh()
        }
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/connection")
async def get_connection_info(dispatcher=Depends(get_disp)):
    from app.core.config import settings
    return {
        "transport": settings.transport,
        "host": settings.esp32_host,
        "port": settings.esp32_port,
        "serial_port": settings.serial_port,
        "baudrate": settings.baudrate,
        "connected": dispatcher.bridge.is_connected() if dispatcher and dispatcher.bridge else False
    }

@router.post("/connect")
async def connect_controller(req: ConnectionConfigRequest):
    from app.main import reconfigure_bridge
    from app.core.config import settings
    success = await reconfigure_bridge(
        transport=req.transport or settings.transport,
        host=req.host or settings.esp32_host,
        port=req.port or settings.esp32_port,
        serial_port=req.serial_port or settings.serial_port,
        baudrate=req.baudrate or settings.baudrate
    )
    return {
        "success": success,
        "transport": req.transport or settings.transport,
        "host": req.host or settings.esp32_host,
        "port": req.port or settings.esp32_port,
        "connected": success
    }

@router.post("/ping")
async def ping(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.ping"})
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/stop")
async def stop(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.stop"})
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/estop")
async def estop(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.estop"})
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/reset-fault")
async def reset_fault(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.reset_fault"})
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/driver")
async def set_driver(req: DriverSetEnabledRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/microstep")
async def set_microstep(req: DriverSetMicrostepRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Microstep change rejected: {ack.code}")
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/home")
async def home(req: HomeStartRequest, dispatcher=Depends(get_disp)):
    try:
        # Firmware allows up to 60s per homing phase; a full X+Y sequence can
        # span several phases, so allow enough headroom for both axes.
        ack, ev = await dispatcher.send_operation(req.model_dump(), op_timeout_s=180.0)
        return {"ack": ack, "event": ev}
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.get("/status/report")
async def get_status_report(dispatcher=Depends(get_disp)):
    from app.core.config import settings
    if dispatcher.bridge.is_connected() and not dispatcher.is_fresh():
        try:
            await dispatcher.send_command({"type": "system.snapshot"}, timeout_s=1.5)
        except Exception:
            pass
    snap = dispatcher.get_effective_snapshot()

    conn_info = {
        "host": settings.esp32_host,
        "port": settings.esp32_port,
        "transport": settings.transport,
        "connected": dispatcher.bridge.is_connected()
    }

    if not dispatcher.bridge.is_connected():
        return {"report": "Machine offline or controller not connected.", "snapshot": snap, "connected": False}

    sx = snap.sensors if hasattr(snap, "sensors") else snap.get("sensors", {})
    pos = snap.pos if hasattr(snap, "pos") else snap.get("pos", {})
    homed = snap.homed if hasattr(snap, "homed") else snap.get("homed", {})
    laser = snap.laser if hasattr(snap, "laser") else snap.get("laser", {})

    def _get(obj, key, default=None):
        if isinstance(obj, dict):
            return obj.get(key, default)
        return getattr(obj, key, default)

    pos_x = _get(pos, "x", 0)
    pos_y = _get(pos, "y", 0)
    homed_x = _get(homed, "x", False)
    homed_y = _get(homed, "y", False)
    sx_x_raw = _get(sx, "x_raw", 1)
    sx_y_raw = _get(sx, "y_raw", 1)
    sx_x_filt = _get(sx, "x_filtered", 1)
    sx_y_filt = _get(sx, "y_filtered", 1)
    laser_active = _get(laser, "active", False)
    laser_duty = _get(laser, "duty", 0)
    snap_state = _get(snap, "state", "unknown")
    drivers_enabled = _get(snap, "drivers_enabled", False)
    microstep_mode = _get(snap, "microstep_mode", 16) or 16
    # 28BYJ-48 64:1 has 2048 baseline full steps/rev. The configured
    # machine_config.steps_per_unit_* is calibrated at 1/16 (80 steps/mm for
    # a 5 steps/mm mechanics); derive the live value from that single source.
    from app.core.kinematics import DEFAULT_REFERENCE_STEPS_PER_UNIT, effective_steps_per_mm
    from app.core.laser import MAX_DUTY, SAFETY_MAX_DUTY, SAFETY_MAX_PCT
    cfg = _read_machine_config()
    reference_steps_per_unit = float(cfg.get("steps_per_unit_x", DEFAULT_REFERENCE_STEPS_PER_UNIT))
    steps_per_mm = effective_steps_per_mm(reference_steps_per_unit, microstep_mode)
    steps_per_rev = 2048 * microstep_mode

    lines = [
        "+=======================================================================+",
        "|                 CONSOLIDATED FIXTURE STATUS REPORT                    |",
        "+=======================================================================+",
        f"| Hardware Target    : ESP32-S3 SuperMini | State: {str(snap_state):<21} |",
        f"| Network Connection : {conn_info['host']:<20} | Port: {conn_info['port']:<21} |",
        f"| Transport / Bridge : {conn_info['transport'].upper():<20} | Connected: {str(conn_info['connected']).upper():<16} |",
        "+-----------------------------------------------------------------------+",
        "| AXIS STATUS & KINEMATICS                                              |",
        "+-----------------------------------------------------------------------+",
        f"| Axis X (Motor A)   : Pos={pos_x:<8} steps (Homed={'YES' if homed_x else 'NO '}, Driver={'ON ' if drivers_enabled else 'OFF'})      |",
        f"| Axis Y (Motor B)   : Pos={pos_y:<8} steps (Homed={'YES' if homed_y else 'NO '}, Driver={'ON ' if drivers_enabled else 'OFF'})      |",
        f"| Microstepping Mode : 1/{microstep_mode:<2} ({steps_per_rev} steps/rev, {steps_per_mm:.1f} steps/mm)            |",
        f"| Commanded Coords   : X={(pos_x/steps_per_mm):.2f} mm | Y={(pos_y/steps_per_mm):.2f} mm                       |",
        "+-----------------------------------------------------------------------+",
        "| OPTICAL SENSOR STATUS (FC-51 / KY-032 LM393)                         |",
        "+-----------------------------------------------------------------------+",
        f"| Sensor X (GPIO 13) : Raw={'HIGH' if sx_x_raw else 'LOW '} State={'DETECTED' if sx_x_filt else 'CLEAR   '} (raw LOW = blocked)   |",
        f"| Sensor Y (GPIO 8)  : Raw={'HIGH' if sx_y_raw else 'LOW '} State={'DETECTED' if sx_y_filt else 'CLEAR   '} (raw LOW = blocked)   |",
        f"| Sensor Polarity    : ACTIVE LOW (filtered 1 = DETECTED / blocked)     |",
        "+-----------------------------------------------------------------------+",
        "| LASER DIODE SAFETY (GPIO 12)                                          |",
        "+-----------------------------------------------------------------------+",
        f"| Laser Emission     : {'ACTIVE RADIATION' if laser_active else 'OFF / SAFE':<20} | Duty: {laser_duty:<4} / {MAX_DUTY}           |",
        f"| Safety Hardware    : {SAFETY_MAX_PCT}% Hard Clamp ({SAFETY_MAX_DUTY}) | Watchdog: 500ms Timer     |",
        "+=======================================================================+"
    ]
    report_text = "\n".join(lines)
    dumped_snap = snap.model_dump() if hasattr(snap, "model_dump") else snap
    return {"report": report_text, "snapshot": dumped_snap, "connected": conn_info["connected"]}

@router.post("/jog")
async def jog(req: MotionJogRequest, dispatcher=Depends(get_disp)):
    try:
        max_x, max_y = _live_travel_limits(dispatcher)
        snap = dispatcher.latest_snapshot
        if snap:
            limit = max_x if req.axis == "x" else max_y
            current = int((snap.pos or {}).get(req.axis, 0))
            if abs(current + req.steps) > limit:
                raise HTTPException(
                    status_code=400,
                    detail=f"Jog out of bounds: target {current + req.steps} exceeds configured limit ±{limit:.0f}"
                )
        ack = await dispatcher.send_command(req.model_dump())
        # If controller is busy executing prior step burst, wait for it to settle and retry
        if not ack.accepted and ack.code == "BUSY":
            import asyncio
            for _ in range(20):
                await asyncio.sleep(0.08)
                if dispatcher.latest_snapshot and dispatcher.latest_snapshot.state == "moving":
                    continue
                ack = await dispatcher.send_command(req.model_dump())
                if ack.accepted or ack.code != "BUSY":
                    break

        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Jog rejected: {ack.code}")
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/move-abs")
async def move_abs(req: MotionMoveAbsRequest, dispatcher=Depends(get_disp)):
    try:
        max_x, max_y = _live_travel_limits(dispatcher)
        if abs(req.x) > max_x or abs(req.y) > max_y:
            raise HTTPException(
                status_code=400,
                detail=f"Move out of bounds: target exceeds configured limit ({max_x:.0f}, {max_y:.0f})"
            )
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Move rejected: {ack.code}")
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

@router.post("/laser/pulse")
async def laser_pulse(req: LaserPulseRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Laser pulse rejected: {ack.code}")
        return ack
    except HTTPException:
        raise
    except TimeoutError as e:
        raise HTTPException(status_code=504, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))

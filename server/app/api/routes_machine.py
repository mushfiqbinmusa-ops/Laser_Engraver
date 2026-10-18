import logging
from fastapi import APIRouter, HTTPException, Depends
from app.models.protocol_models import (
    AckResponse, SystemSnapshot, MotionJogRequest, MotionMoveAbsRequest,
    HomeStartRequest, LaserPulseRequest, DriverSetEnabledRequest
)

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/machine", tags=["machine"])

def get_disp():
    from app.main import get_dispatcher
    d = get_dispatcher()
    if not d:
        raise HTTPException(status_code=503, detail="Controller dispatcher not initialized")
    return d

@router.get("/status")
async def get_status(dispatcher=Depends(get_disp)):
    return {
        "connected": dispatcher.bridge.is_connected(),
        "fresh": dispatcher.is_fresh(),
        "snapshot": dispatcher.latest_snapshot.model_dump() if dispatcher.latest_snapshot else None,
        "last_seen_epoch": dispatcher.last_snapshot_time
    }

@router.post("/ping")
async def ping(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.ping"})
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/stop")
async def stop(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.stop"})
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/estop")
async def estop(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.estop"})
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/reset-fault")
async def reset_fault(dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command({"type": "system.reset_fault"})
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/driver")
async def set_driver(req: DriverSetEnabledRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/home")
async def home(req: HomeStartRequest, dispatcher=Depends(get_disp)):
    try:
        # Start homing and wait for completion
        ack, ev = await dispatcher.send_operation(req.model_dump(), op_timeout_s=30.0)
        return {"ack": ack, "event": ev}
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/jog")
async def jog(req: MotionJogRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Jog rejected: {ack.code}")
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/move-abs")
async def move_abs(req: MotionMoveAbsRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Move rejected: {ack.code}")
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

@router.post("/laser/pulse")
async def laser_pulse(req: LaserPulseRequest, dispatcher=Depends(get_disp)):
    try:
        ack = await dispatcher.send_command(req.model_dump())
        if not ack.accepted:
            raise HTTPException(status_code=400, detail=f"Laser pulse rejected: {ack.code}")
        return ack
    except Exception as e:
        raise HTTPException(status_code=504, detail=str(e))

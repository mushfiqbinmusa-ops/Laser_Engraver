import asyncio
import hashlib
import json
import time
import uuid
import logging
from typing import List, Optional
from fastapi import APIRouter, HTTPException, UploadFile, File, Form, Depends, Response
from app.db.database import get_db
from app.models.job_models import JobResponse, ParametricShapeCreate, PreparedJobPlan
from app.models.preset_models import PresetModel, MachineConfigModel
from app.svg.validator import validate_svg_content, SvgValidationError
from app.svg.parser import SvgParser, Contour
from app.svg.transformer import SvgTransformer
from app.svg.preview import render_preview_png
from app.svg.parametric import ParametricShapeGenerator
from app.svg.job_planner import JobPlanner
from app.core.kinematics import REFERENCE_MICROSTEP_MODE, scale_plan_for_microstep
from app.core.laser import SAFETY_MAX_PCT, pct_to_duty

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/jobs", tags=["jobs"])

# Background execution state
_active_streaming_task: Optional[asyncio.Task] = None
_active_job_id: Optional[str] = None
_abort_streaming: bool = False
_job_lock = asyncio.Lock()

def get_disp():
    from app.main import get_dispatcher
    d = get_dispatcher()
    if not d:
        raise HTTPException(status_code=503, detail="Controller dispatcher not initialized")
    return d

def _get_preset(conn, preset_id: str) -> PresetModel:
    cursor = conn.execute("SELECT * FROM presets WHERE id = ?;", (preset_id,))
    row = cursor.fetchone()
    if not row:
        cursor = conn.execute("SELECT * FROM presets WHERE id = 'preset-wood';")
        row = cursor.fetchone()
    return PresetModel.model_validate(dict(row))

def _get_config(conn) -> MachineConfigModel:
    cursor = conn.execute("SELECT * FROM machine_config WHERE id = 'default';")
    row = cursor.fetchone()
    return MachineConfigModel.model_validate(dict(row))

@router.get("", response_model=List[JobResponse])
def list_jobs():
    with get_db() as conn:
        cursor = conn.execute("""
            SELECT id, name, source_filename, source_hash, status, total_segments, completed_segments, 
                   (preview_png IS NOT NULL) as has_preview, created_at, updated_at
            FROM jobs ORDER BY created_at DESC LIMIT 50;
        """)
        rows = cursor.fetchall()
        return [JobResponse.model_validate(dict(r)) for r in rows]

@router.post("/upload", response_model=JobResponse)
async def upload_svg(
    svg_file: UploadFile = File(...),
    preset_id: str = Form("preset-wood"),
    override_laser_power_pct: Optional[int] = Form(None)
):
    if override_laser_power_pct is not None and not (0 <= override_laser_power_pct <= SAFETY_MAX_PCT):
        raise HTTPException(
            status_code=400,
            detail=f"override_laser_power_pct must be between 0 and {SAFETY_MAX_PCT}"
        )

    content_bytes = await svg_file.read()
    content_str = content_bytes.decode("utf-8", errors="replace")

    try:
        validate_svg_content(content_str)
    except SvgValidationError as e:
        raise HTTPException(status_code=400, detail=str(e))

    contours = SvgParser.extract_contours(content_str)
    if not contours:
        raise HTTPException(status_code=400, detail="No extractable vector paths found in SVG file.")

    job_id = f"job-{uuid.uuid4().hex[:8]}"
    source_hash = hashlib.sha256(content_bytes).hexdigest()
    safe_filename = svg_file.filename or "artwork.svg"

    with get_db() as conn:
        config = _get_config(conn)
        preset = _get_preset(conn, preset_id)

        # Apply temporary power override if specified
        if override_laser_power_pct is not None:
            duty = pct_to_duty(override_laser_power_pct)
            preset = preset.model_copy(update={
                "laser_power_pct": override_laser_power_pct,
                "laser_duty": duty
            })

        toolpath = JobPlanner.prepare_contours(contours, config)
        plan = JobPlanner.build_plan_from_ordered(job_id, safe_filename, toolpath, preset, config)
        preview_bytes = render_preview_png(
            toolpath,
            work_w_mm=config.work_area_width_mm,
            work_h_mm=config.work_area_height_mm
        )

        conn.execute("""
            INSERT INTO jobs (id, name, source_filename, source_hash, status, preset_snapshot_json,
                             machine_config_snapshot_json, plan_json, preview_png, total_segments)
            VALUES (?, ?, ?, ?, 'prepared', ?, ?, ?, ?, ?);
        """, (
            job_id, safe_filename, safe_filename, source_hash,
            preset.model_dump_json(), config.model_dump_json(),
            plan.model_dump_json(), preview_bytes, plan.total_segments
        ))

        cursor = conn.execute("""
            SELECT id, name, source_filename, source_hash, status, total_segments, completed_segments, 
                   1 as has_preview, created_at, updated_at
            FROM jobs WHERE id = ?;
        """, (job_id,))
        row = cursor.fetchone()
        return JobResponse.model_validate(dict(row))

@router.post("/shape", response_model=JobResponse)
async def create_parametric_shape(req: ParametricShapeCreate):
    if req.shape_type == "square":
        contours = ParametricShapeGenerator.generate_square(req.width_mm or 20.0)
        shape_name = f"Square ({req.width_mm}mm)"
    elif req.shape_type == "rectangle":
        contours = ParametricShapeGenerator.generate_rectangle(req.width_mm or 20.0, req.height_mm or 15.0)
        shape_name = f"Rectangle ({req.width_mm}x{req.height_mm}mm)"
    elif req.shape_type == "circle":
        contours = ParametricShapeGenerator.generate_circle(req.radius_mm or 10.0)
        shape_name = f"Circle (r={req.radius_mm}mm)"
    elif req.shape_type == "grid":
        contours = ParametricShapeGenerator.generate_grid(req.width_mm or 20.0, req.height_mm or 20.0, req.grid_rows or 5, req.grid_cols or 5)
        shape_name = f"Grid ({req.grid_rows}x{req.grid_cols})"
    else:
        raise HTTPException(status_code=400, detail="Invalid shape type")

    job_id = f"job-{uuid.uuid4().hex[:8]}"
    name = req.name or shape_name
    source_hash = hashlib.sha256(name.encode("utf-8")).hexdigest()

    with get_db() as conn:
        config = _get_config(conn)
        preset = _get_preset(conn, req.preset_id)

        if req.override_laser_power_pct is not None:
            duty = pct_to_duty(req.override_laser_power_pct)
            preset = preset.model_copy(update={
                "laser_power_pct": req.override_laser_power_pct,
                "laser_duty": duty
            })

        toolpath = JobPlanner.prepare_contours(contours, config)
        plan = JobPlanner.build_plan_from_ordered(job_id, name, toolpath, preset, config)
        preview_bytes = render_preview_png(
            toolpath,
            work_w_mm=config.work_area_width_mm,
            work_h_mm=config.work_area_height_mm
        )

        conn.execute("""
            INSERT INTO jobs (id, name, source_filename, source_hash, status, preset_snapshot_json,
                             machine_config_snapshot_json, plan_json, preview_png, total_segments)
            VALUES (?, ?, ?, ?, 'prepared', ?, ?, ?, ?, ?);
        """, (
            job_id, name, "builtin-shape", source_hash,
            preset.model_dump_json(), config.model_dump_json(),
            plan.model_dump_json(), preview_bytes, plan.total_segments
        ))

        cursor = conn.execute("""
            SELECT id, name, source_filename, source_hash, status, total_segments, completed_segments, 
                   1 as has_preview, created_at, updated_at
            FROM jobs WHERE id = ?;
        """, (job_id,))
        row = cursor.fetchone()
        return JobResponse.model_validate(dict(row))

@router.get("/{job_id}/preview")
def get_preview(job_id: str):
    with get_db() as conn:
        cursor = conn.execute("SELECT preview_png FROM jobs WHERE id = ?;", (job_id,))
        row = cursor.fetchone()
        if not row or not row["preview_png"]:
            raise HTTPException(status_code=404, detail="Preview not found")
        return Response(content=row["preview_png"], media_type="image/png")

@router.post("/{job_id}/start")
async def start_job(
    job_id: str,
    dry_run: bool = False,
    dispatcher=Depends(get_disp)
):
    global _active_streaming_task, _abort_streaming, _active_job_id

    async with _job_lock:
        if _active_streaming_task and not _active_streaming_task.done():
            raise HTTPException(status_code=400, detail="A job is already executing")

        # Readiness Gate check
        if not dispatcher.bridge.is_connected():
            raise HTTPException(status_code=400, detail="Controller is not connected")
        if not dispatcher.is_fresh():
            raise HTTPException(status_code=400, detail="Controller telemetry is stale")
        if not dispatcher.latest_snapshot:
            raise HTTPException(status_code=400, detail="No snapshot received from controller")
        
        snap = dispatcher.latest_snapshot
        if not (snap.homed.get("x") and snap.homed.get("y")):
            raise HTTPException(status_code=400, detail="Cannot start: Both X and Y axes must be homed")
        if snap.fault:
            raise HTTPException(status_code=400, detail=f"Cannot start: Machine in fault state ({snap.fault})")

        with get_db() as conn:
            cursor = conn.execute("SELECT plan_json FROM jobs WHERE id = ?;", (job_id,))
            row = cursor.fetchone()
            if not row:
                raise HTTPException(status_code=404, detail="Job not found")
            plan_dict = json.loads(row["plan_json"])
            plan = PreparedJobPlan.model_validate(plan_dict)

        # Plans are built in reference (1/16) steps. Rescale to the mode the
        # controller is actually in now so dimensions stay correct.
        live_mode = int(snap.microstep_mode or REFERENCE_MICROSTEP_MODE)
        plan = scale_plan_for_microstep(plan, live_mode)

        _abort_streaming = False
        _active_job_id = job_id

    async def _stream_job_worker():
        global _abort_streaming
        from app.api.ws_events import ws_manager

        with get_db() as conn:
            conn.execute("UPDATE jobs SET status = 'running', completed_segments = 0 WHERE id = ?;", (job_id,))
            event_type = "STARTED_DRY_RUN" if dry_run else "STARTED"
            conn.execute("INSERT INTO job_events (job_id, event_type) VALUES (?, ?);", (job_id, event_type))

        try:
            # 1. job.begin
            ack = await dispatcher.send_command({
                "type": "job.begin",
                "job_id": job_id,
                "total_segments": plan.total_segments
            })
            if not ack.accepted:
                raise RuntimeError(f"job.begin rejected: {ack.code}")

            # 2. Stream segments sequentially with cooperative flow control
            for seg in plan.segments:
                if _abort_streaming:
                    raise asyncio.CancelledError()

                seg_cmd = {
                    "type": "job.segment",
                    "job_id": job_id,
                    "seg_idx": seg.seg_idx,
                    "dx": seg.dx,
                    "dy": seg.dy,
                    "speed_sps": seg.speed_sps,
                    "duty": 0 if dry_run else seg.duty
                }

                # Calculate estimated move duration
                dist = max(abs(seg.dx), abs(seg.dy))
                sps = max(1, seg.speed_sps)
                est_duration_s = dist / float(sps)
                max_wait_s = max(15.0, est_duration_s + 10.0)
                deadline = time.monotonic() + max_wait_s

                # Retry on BUSY until stepper planner finishes prior move on hardware
                ack = None
                while time.monotonic() < deadline:
                    if _abort_streaming:
                        raise asyncio.CancelledError()
                    try:
                        ack = await dispatcher.send_command(seg_cmd, timeout_s=4.0)
                        if ack.accepted:
                            break
                        if ack.code == "BUSY":
                            await asyncio.sleep(0.04)
                            continue
                    except Exception:
                        if _abort_streaming:
                            raise asyncio.CancelledError()
                        await asyncio.sleep(0.04)
                        continue

                if not ack or not ack.accepted:
                    raise RuntimeError(f"Segment {seg.seg_idx} rejected: {ack.code if ack else 'TIMEOUT'}")

                # Update progress
                await ws_manager.broadcast({
                    "type": "job.progress",
                    "job_id": job_id,
                    "completed_segments": seg.seg_idx + 1,
                    "total_segments": plan.total_segments
                })

            # 3. job.end, retrying while the firmware reports BUSY (final move
            #    still in progress). The firmware ACKs job.segment as soon as
            #    the move is queued, so this is the only exact completion
            #    signal and prevents truncating the last segment.
            end_deadline = time.monotonic() + 10.0
            while True:
                if _abort_streaming:
                    raise asyncio.CancelledError()
                end_ack = None
                try:
                    end_ack = await dispatcher.send_command(
                        {"type": "job.end", "job_id": job_id}, timeout_s=4.0
                    )
                except Exception:
                    end_ack = None
                if end_ack and end_ack.accepted:
                    break
                if end_ack and end_ack.code != "BUSY":
                    raise RuntimeError(f"job.end rejected: {end_ack.code}")
                if time.monotonic() >= end_deadline:
                    raise RuntimeError("job.end timed out waiting for final segment to finish")
                await asyncio.sleep(0.05)

            with get_db() as conn:
                conn.execute("UPDATE jobs SET status = 'completed', completed_segments = ? WHERE id = ?;", (plan.total_segments, job_id))
                conn.execute("INSERT INTO job_events (job_id, event_type) VALUES (?, 'COMPLETED');", (job_id,))

            await ws_manager.broadcast({
                "type": "job.completed",
                "job_id": job_id
            })

        except asyncio.CancelledError:
            logger.info(f"Job {job_id} was canceled by operator")
            try:
                await dispatcher.send_command({"type": "system.stop"})
            except Exception:
                pass
            with get_db() as conn:
                conn.execute("UPDATE jobs SET status = 'canceled' WHERE id = ?;", (job_id,))
                conn.execute("INSERT INTO job_events (job_id, event_type) VALUES (?, 'CANCELED');", (job_id,))

        except Exception as e:
            logger.error(f"Job {job_id} execution error: {e}")
            try:
                await dispatcher.send_command({"type": "system.stop"})
            except Exception:
                pass
            with get_db() as conn:
                conn.execute("UPDATE jobs SET status = 'failed' WHERE id = ?;", (job_id,))
                conn.execute("INSERT INTO job_events (job_id, event_type, details_json) VALUES (?, 'FAILED', ?);",
                             (job_id, json.dumps({"error": str(e)})))
        finally:
            _active_job_id = None

    _active_streaming_task = asyncio.create_task(_stream_job_worker())
    return {"ok": True, "job_id": job_id, "status": "started"}

@router.post("/{job_id}/cancel")
async def cancel_job(job_id: str, dispatcher=Depends(get_disp)):
    global _abort_streaming, _active_streaming_task, _active_job_id
    async with _job_lock:
        if _active_job_id and _active_job_id != job_id:
            raise HTTPException(status_code=400, detail=f"Job {job_id} is not currently running")
        _abort_streaming = True
        if _active_streaming_task:
            _active_streaming_task.cancel()
        _active_job_id = None
        await dispatcher.send_command({"type": "system.stop"})
        return {"ok": True, "job_id": job_id, "status": "canceled"}

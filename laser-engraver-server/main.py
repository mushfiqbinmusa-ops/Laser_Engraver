from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from fastapi import FastAPI, File, HTTPException, UploadFile, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse, JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from config import STATIC_DIR, settings
from db.database import get_job, save_job
from esp32_bridge.serial_client import ESP32SerialClient
from esp32_bridge.tcp_client import ESP32TCPClient
from svg_processor.gcode_gen import generate_gcode
from svg_processor.parser import parse_svg
from svg_processor.preview import render_preview
from svg_processor.sorter import sort_paths_nearest_neighbour
from svg_processor.transform import fit_to_work_area

ESP32_CLIENT = None
if settings.transport == "tcp":
    ESP32_CLIENT = ESP32TCPClient(settings.esp32_host, settings.esp32_port)
else:
    ESP32_CLIENT = ESP32SerialClient(settings.serial_port, settings.baudrate)

app = FastAPI(title="Laser Engraver Server")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

app.mount("/static", StaticFiles(directory=str(STATIC_DIR)), name="static")


@app.get("/")
def index() -> HTMLResponse:
    return HTMLResponse((STATIC_DIR / "index.html").read_text())


@app.get("/api/health")
def health() -> dict[str, Any]:
    connected = False
    if ESP32_CLIENT is not None and hasattr(ESP32_CLIENT, "connected"):
        connected = ESP32_CLIENT.connected()
    return {"ok": True, "esp_connected": connected, "transport": settings.transport}


@app.post("/api/control/home")
async def home() -> dict[str, Any]:
    if ESP32_CLIENT is None:
        raise HTTPException(500, "No ESP32 client configured")
    try:
        if settings.transport == "tcp":
            await ESP32_CLIENT.connect()
        else:
            ESP32_CLIENT.connect()
        response = await ESP32_CLIENT.send_line("G28") if settings.transport == "tcp" else ESP32_CLIENT.send_line("G28")
    except Exception as exc:  # pragma: no cover - hardware failure path
        raise HTTPException(503, f"ESP32 unreachable: {exc}") from exc
    return {"ok": True, "response": response}


@app.post("/api/control/laser/power")
async def laser_power(level: int) -> dict[str, Any]:
    if not 0 <= level <= 100:
        raise HTTPException(400, "power must be between 0 and 100")
    duty = int((level / 100) * 4095)
    try:
        if settings.transport == "tcp":
            if not ESP32_CLIENT.connected():
                await ESP32_CLIENT.connect()
            response = await ESP32_CLIENT.send_line(f"M3 S{duty}")
        else:
            if not ESP32_CLIENT.connected():
                ESP32_CLIENT.connect()
            response = ESP32_CLIENT.send_line(f"M3 S{duty}")
    except Exception as exc:  # pragma: no cover - hardware failure path
        raise HTTPException(503, f"ESP32 unreachable: {exc}") from exc
    return {"ok": True, "duty": duty, "response": response}


@app.post("/api/upload")
async def upload(svg_file: UploadFile = File(...)) -> dict[str, Any]:
    if not svg_file.filename or not svg_file.filename.endswith(".svg"):
        raise HTTPException(400, "Only SVG uploads are supported")

    svg_bytes = await svg_file.read()
    polylines = parse_svg(svg_bytes)
    if not polylines:
        raise HTTPException(400, "No valid SVG paths found")

    transformed = fit_to_work_area(
        polylines,
        work_area_steps_x=settings.work_area_steps_x,
        work_area_steps_y=settings.work_area_steps_y,
    )
    ordered = sort_paths_nearest_neighbour(transformed)
    gcode_lines = generate_gcode(ordered, laser_duty=settings.laser_duty)
    preview_bytes = render_preview(ordered, width=settings.preview_width, height=settings.preview_height)

    job_data = {
        "status": "ready",
        "filename": svg_file.filename,
        "gcode": gcode_lines,
        "total_paths": len(ordered),
        "laser_duty": settings.laser_duty,
    }
    job_id = save_job(svg_file.filename, job_data, preview_bytes)
    return {
        "ok": True,
        "job_id": job_id,
        "filename": svg_file.filename,
        "gcode_line_count": len(gcode_lines),
        "path_count": len(ordered),
        "preview_url": f"/api/jobs/{job_id}/preview",
    }


@app.get("/api/jobs/{job_id}/preview")
def job_preview(job_id: int):
    job = get_job(job_id)
    if job is None:
        raise HTTPException(404, "Job not found")
    preview = job["preview"]
    if preview is None:
        raise HTTPException(404, "Preview missing")
    return StreamingResponse(iter([preview]), media_type="image/png")


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    try:
        while True:
            message = {
                "ok": True,
                "esp_connected": ESP32_CLIENT.connected() if ESP32_CLIENT is not None else False,
                "transport": settings.transport,
            }
            await websocket.send_json(message)
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)

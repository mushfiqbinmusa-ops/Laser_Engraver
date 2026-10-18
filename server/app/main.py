import asyncio
import logging
from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from typing import Optional
from app.bridge.base import ControllerBridge
from app.core.config import settings
from app.db.migrations import run_migrations
from app.bridge.tcp_bridge import TcpControllerBridge
from app.bridge.serial_bridge import SerialControllerBridge
from app.bridge.dispatcher import ControllerDispatcher
from app.api.ws_events import router as ws_router, ws_manager
from app.api.routes_machine import router as machine_router
from app.api.routes_presets import router as presets_router
from app.api.routes_jobs import router as jobs_router

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(name)s: %(message)s")
logger = logging.getLogger(__name__)

# Global singletons
_bridge: Optional[ControllerBridge] = None
_dispatcher: Optional[ControllerDispatcher] = None
_supervisor_task: Optional[asyncio.Task] = None

def get_dispatcher() -> Optional[ControllerDispatcher]:
    return _dispatcher

async def _bridge_supervisor():
    while True:
        try:
            if _bridge and not _bridge.is_connected():
                logger.info(f"Attempting connection to ESP32 ({settings.transport})...")
                connected = await _bridge.connect()
                if connected and _dispatcher:
                    # Immediately request snapshot upon reconnect
                    try:
                        await _dispatcher.send_command({"type": "system.snapshot"}, timeout_s=2.0)
                    except Exception:
                        pass
        except Exception as e:
            logger.warning(f"Bridge supervisor error: {e}")
        await asyncio.sleep(3.0)

@asynccontextmanager
async def lifespan(app: FastAPI):
    global _bridge, _dispatcher, _supervisor_task
    logger.info("Initializing Laser XY Scanner Control Service...")
    
    # 1. Run database migrations
    migrations_applied = run_migrations()
    logger.info(f"Applied {migrations_applied} database migrations.")

    # 2. Initialize controller bridge & dispatcher
    if settings.transport == "serial":
        _bridge = SerialControllerBridge(settings.serial_port, settings.baudrate)
    else:
        _bridge = TcpControllerBridge(settings.esp32_host, settings.esp32_port)

    _dispatcher = ControllerDispatcher(_bridge)
    # Broadcast all incoming controller events directly to connected WebSocket clients
    _dispatcher.add_event_listener(ws_manager.broadcast)

    # 3. Start connection supervisor task
    _supervisor_task = asyncio.create_task(_bridge_supervisor())

    yield

    # Shutdown
    if _supervisor_task:
        _supervisor_task.cancel()
        try:
            await _supervisor_task
        except asyncio.CancelledError:
            pass

    if _dispatcher and _bridge and _bridge.is_connected():
        try:
            await _dispatcher.send_command({"type": "system.stop"}, timeout_s=1.0)
        except Exception:
            pass

    if _bridge:
        await _bridge.disconnect()
    logger.info("Shutdown complete.")

app = FastAPI(
    title=settings.app_name,
    version=settings.version,
    lifespan=lifespan
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=False,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(ws_router)
app.include_router(machine_router)
app.include_router(presets_router)
app.include_router(jobs_router)

# Mount static web app if dist directory exists
static_dir = Path(__file__).parent.parent / "static"
if static_dir.exists():
    app.mount("/", StaticFiles(directory=str(static_dir), html=True), name="static")

if __name__ == "__main__":
    import uvicorn
    uvicorn.run("app.main:app", host="0.0.0.0", port=8000, reload=True)

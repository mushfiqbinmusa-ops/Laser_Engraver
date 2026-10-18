import json
import logging
from typing import Set
from fastapi import APIRouter, WebSocket, WebSocketDisconnect

logger = logging.getLogger(__name__)

router = APIRouter()

class ConnectionManager:
    def __init__(self):
        self.active_connections: Set[WebSocket] = set()

    async def connect(self, websocket: WebSocket) -> None:
        await websocket.accept()
        self.active_connections.add(websocket)

    def disconnect(self, websocket: WebSocket) -> None:
        self.active_connections.discard(websocket)

    async def broadcast(self, message: dict) -> None:
        raw = json.dumps(message)
        for connection in list(self.active_connections):
            try:
                await connection.send_text(raw)
            except Exception:
                self.active_connections.discard(connection)

ws_manager = ConnectionManager()

@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    from app.main import get_dispatcher
    await ws_manager.connect(websocket)
    dispatcher = get_dispatcher()
    
    # Send immediate snapshot on connect
    if dispatcher and dispatcher.latest_snapshot:
        try:
            await websocket.send_text(dispatcher.latest_snapshot.model_dump_json())
        except Exception:
            pass

    try:
        while True:
            # Keepalive / ping from client
            data = await websocket.receive_text()
            if data == "ping":
                await websocket.send_text(json.dumps({"type": "pong"}))
    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.warning(f"WebSocket client error: {e}")
    finally:
        ws_manager.disconnect(websocket)

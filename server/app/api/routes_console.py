import logging

from fastapi import APIRouter, Query, WebSocket, WebSocketDisconnect

from app.console.hub import console_hub

logger = logging.getLogger(__name__)
router = APIRouter(tags=["console"])


@router.get("/api/console/history")
async def console_history(limit: int = Query(default=500, ge=1, le=2000)):
    return {"entries": console_hub.history(limit)}


@router.websocket("/ws/console")
async def console_stream(websocket: WebSocket):
    await websocket.accept()
    queue = console_hub.subscribe()
    try:
        await websocket.send_json({
            "type": "console.history",
            "entries": console_hub.history(500),
        })
        while True:
            entry = await queue.get()
            await websocket.send_json({"type": "console.entry", "entry": entry})
    except WebSocketDisconnect:
        pass
    except Exception as exc:
        logger.debug(f"Console WebSocket closed: {exc}")
    finally:
        console_hub.unsubscribe(queue)

import asyncio
import json
import time
from collections import deque
from typing import Any, Deque, Optional


def classify_line(direction: str, text: str) -> str:
    """Classify a raw controller line for the operator console."""
    if direction == "tx":
        return "command"

    try:
        data = json.loads(text)
    except Exception:
        return "device_log"

    if not isinstance(data, dict):
        return "device_log"

    frame_type = str(data.get("type", ""))
    if frame_type == "system.snapshot":
        return "telemetry"
    if frame_type == "ack":
        return "ack"
    if frame_type.startswith("operation.") or frame_type.startswith("job."):
        return "event"
    if frame_type:
        return "frame"
    return "device_log"


class ConsoleHub:
    """Bounded raw serial log shared by the serial bridge and WebSocket clients."""

    def __init__(self, maxlen: int = 2000, subscriber_queue_size: int = 500):
        self._entries: Deque[dict[str, Any]] = deque(maxlen=maxlen)
        self._subscribers: set[asyncio.Queue[dict[str, Any]]] = set()
        self._subscriber_queue_size = subscriber_queue_size
        self._seq = 0

    def _make_entry(self, direction: str, text: str, kind: Optional[str] = None) -> dict[str, Any]:
        self._seq += 1
        return {
            "seq": self._seq,
            "ts": time.time(),
            "direction": direction,
            "kind": kind or classify_line(direction, text),
            "text": text,
        }

    def add(self, direction: str, text: str, kind: Optional[str] = None) -> dict[str, Any]:
        entry = self._make_entry(direction, text, kind)
        self._entries.append(entry)
        for queue in list(self._subscribers):
            if queue.full():
                try:
                    queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
            try:
                queue.put_nowait(entry)
            except asyncio.QueueFull:
                pass
        return entry

    def add_rx(self, text: str) -> dict[str, Any]:
        return self.add("rx", text)

    def add_tx(self, text: str) -> dict[str, Any]:
        return self.add("tx", text)

    def history(self, limit: int = 500) -> list[dict[str, Any]]:
        if limit <= 0:
            return []
        return list(self._entries)[-limit:]

    def subscribe(self) -> asyncio.Queue[dict[str, Any]]:
        queue: asyncio.Queue[dict[str, Any]] = asyncio.Queue(maxsize=self._subscriber_queue_size)
        self._subscribers.add(queue)
        return queue

    def unsubscribe(self, queue: asyncio.Queue[dict[str, Any]]) -> None:
        self._subscribers.discard(queue)

    def clear(self) -> None:
        self._entries.clear()


console_hub = ConsoleHub()

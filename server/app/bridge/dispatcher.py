import asyncio
import json
import logging
import time
from typing import Optional, Callable, Awaitable, Any
from app.bridge.base import ControllerBridge
from app.models.protocol_models import AckResponse, SystemSnapshot, OperationEvent

logger = logging.getLogger(__name__)

class ControllerDispatcher:
    def __init__(self, bridge: ControllerBridge):
        self.bridge = bridge
        self.bridge.set_line_handler(self._handle_incoming_line)
        self._lock = asyncio.Lock()
        self._req_counter = 0
        self._pending_acks: dict[str, asyncio.Future[AckResponse]] = {}
        self._pending_ops: dict[str, asyncio.Future[OperationEvent]] = {}
        self._completed_ops_cache: dict[str, OperationEvent] = {}
        self._event_listeners: list[Callable[[dict[str, Any]], Awaitable[None]]] = []
        
        self.latest_snapshot: Optional[SystemSnapshot] = None
        self.last_snapshot_time: float = 0.0
        self.freshness_threshold_s: float = 2.0

    def set_bridge(self, bridge: ControllerBridge) -> None:
        self.bridge = bridge
        self.bridge.set_line_handler(self._handle_incoming_line)

    def add_event_listener(self, listener: Callable[[dict[str, Any]], Awaitable[None]]) -> None:
        self._event_listeners.append(listener)

    def reset_state(self) -> None:
        self.latest_snapshot = None
        self.last_snapshot_time = 0.0

    def get_effective_snapshot(self) -> dict[str, Any]:
        if self.latest_snapshot:
            return self.latest_snapshot.model_dump()
        return {
            "v": 1,
            "type": "system.snapshot",
            "state": "idle_unhomed",
            "firmware_version": "1.0.0",
            "protocol_version": 1,
            "profile_id": "esp32s3_a4988_v1",
            "homed": {"x": False, "y": False},
            "pos": {"x": 0, "y": 0},
            "pos_valid": False,
            "sensors": {"x_raw": 1, "x_filtered": 0, "y_raw": 1, "y_filtered": 0},
            "laser": {"duty": 0, "active": False, "hard_clamped": False},
            "drivers_enabled": True,
            "microstep_mode": 16,
            "fault": None
        }

    def is_fresh(self) -> bool:
        if not self.bridge.is_connected() or not self.latest_snapshot:
            return False
        return (time.time() - self.last_snapshot_time) < self.freshness_threshold_s

    async def _handle_incoming_line(self, line: str) -> None:
        try:
            data = json.loads(line)
        except Exception as e:
            logger.warning(f"Malformed JSON from controller: {line} - {e}")
            return

        frame_type = data.get("type")

        # 1. Acknowledgment
        if frame_type == "ack":
            req_id = data.get("id")
            if req_id and req_id in self._pending_acks:
                ack_fut = self._pending_acks.pop(req_id)
                if not ack_fut.done():
                    try:
                        ack = AckResponse.model_validate(data)
                        ack_fut.set_result(ack)
                    except Exception as ex:
                        ack_fut.set_exception(ex)

        # 2. Snapshot
        elif frame_type == "system.snapshot":
            try:
                snap = SystemSnapshot.model_validate(data)
                self.latest_snapshot = snap
                self.last_snapshot_time = time.time()
            except Exception as e:
                logger.error(f"Error parsing system snapshot: {e}")

        # 3. Operation Lifecycle Events
        elif frame_type in ("operation.started", "operation.progress", "operation.completed"):
            op_id = data.get("operation_id")
            if frame_type == "operation.completed" and op_id:
                try:
                    ev = OperationEvent.model_validate(data)
                except Exception as ex:
                    logger.error(f"Error parsing operation event: {ex}")
                    ev = None

                if ev:
                    if op_id in self._pending_ops:
                        op_fut = self._pending_ops.pop(op_id)
                        if not op_fut.done():
                            op_fut.set_result(ev)
                    else:
                        # Cache completed event in case send_operation hasn't registered yet
                        self._completed_ops_cache[op_id] = ev
                        if len(self._completed_ops_cache) > 50:
                            oldest_k = next(iter(self._completed_ops_cache))
                            self._completed_ops_cache.pop(oldest_k, None)

        # Broadcast to subscribed listeners (e.g. WebSocket clients)
        for listener in self._event_listeners:
            try:
                await listener(data)
            except Exception as ex:
                logger.error(f"Error notifying listener: {ex}")

    async def send_command(self, cmd_dict: dict[str, Any], timeout_s: float = 3.0) -> AckResponse:
        async with self._lock:
            self._req_counter += 1
            req_id = f"req-{self._req_counter:04d}"
            payload = {**cmd_dict, "v": 1, "id": req_id}

            loop = asyncio.get_running_loop()
            ack_future: asyncio.Future[AckResponse] = loop.create_future()
            self._pending_acks[req_id] = ack_future

            frame_json = json.dumps(payload)
            try:
                await self.bridge.send_frame(frame_json)
                ack = await asyncio.wait_for(ack_future, timeout=timeout_s)
                return ack
            except asyncio.TimeoutError:
                self._pending_acks.pop(req_id, None)
                raise TimeoutError(f"Command {payload.get('type')} (id={req_id}) timed out after {timeout_s}s")
            except Exception:
                self._pending_acks.pop(req_id, None)
                raise

    async def send_operation(self, cmd_dict: dict[str, Any], op_timeout_s: float = 30.0) -> tuple[AckResponse, OperationEvent]:
        # First send command and await immediate ack
        ack = await self.send_command(cmd_dict, timeout_s=3.0)
        if not ack.accepted or not ack.operation_id:
            raise RuntimeError(f"Operation rejected by controller: {ack.code}")

        op_id = ack.operation_id
        # Check if operation completed already before future registration
        if op_id in self._completed_ops_cache:
            cached_ev = self._completed_ops_cache.pop(op_id)
            return ack, cached_ev

        loop = asyncio.get_running_loop()
        op_future: asyncio.Future[OperationEvent] = loop.create_future()
        self._pending_ops[op_id] = op_future

        try:
            event = await asyncio.wait_for(op_future, timeout=op_timeout_s)
            return ack, event
        except asyncio.TimeoutError:
            self._pending_ops.pop(op_id, None)
            raise TimeoutError(f"Operation {op_id} timed out after {op_timeout_s}s")
        except Exception:
            self._pending_ops.pop(op_id, None)
            raise

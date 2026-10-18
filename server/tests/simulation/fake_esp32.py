import asyncio
import json
import logging
from typing import Optional

logger = logging.getLogger(__name__)

class FakeEsp32Server:
    def __init__(self, host: str = "127.0.0.1", port: int = 8888):
        self.host = host
        self.port = port
        self.server: Optional[asyncio.Server] = None
        self.clients: list[asyncio.StreamWriter] = []
        
        # State
        self.state = "idle_unhomed"
        self.homed_x = False
        self.homed_y = False
        self.pos_x = 0
        self.pos_y = 0
        self.laser_duty = 0
        self.drivers_enabled = False
        self.fault = None
        self.op_counter = 0

    async def start(self) -> None:
        self.server = await asyncio.start_server(self._handle_client, self.host, self.port)
        logger.info(f"Fake ESP32 listening on {self.host}:{self.port}")

    async def stop(self) -> None:
        if self.server:
            self.server.close()
            await self.server.wait_closed()
            self.server = None
        for writer in self.clients:
            try:
                writer.close()
                await writer.wait_closed()
            except Exception:
                pass
        self.clients.clear()

    async def _broadcast(self, msg_dict: dict) -> None:
        raw = (json.dumps(msg_dict) + "\n").encode("utf-8")
        for writer in list(self.clients):
            try:
                writer.write(raw)
                await writer.drain()
            except Exception:
                if writer in self.clients:
                    self.clients.remove(writer)

    async def _emit_snapshot(self) -> None:
        snap = {
            "v": 1,
            "type": "system.snapshot",
            "state": self.state,
            "firmware_version": "1.0.0-sim",
            "protocol_version": 1,
            "profile_id": "esp32s3_a4988_v1",
            "homed": {"x": self.homed_x, "y": self.homed_y},
            "pos": {"x": self.pos_x, "y": self.pos_y},
            "pos_valid": self.homed_x and self.homed_y and (self.state != "faulted"),
            "sensors": {"x_raw": 1, "x_filtered": 0, "y_raw": 1, "y_filtered": 0},
            "laser": {"duty": self.laser_duty, "active": self.laser_duty > 0, "hard_clamped": False},
            "drivers_enabled": self.drivers_enabled,
            "fault": self.fault
        }
        await self._broadcast(snap)

    async def _handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        self.clients.append(writer)
        # Emit initial snapshot on connection
        await self._emit_snapshot()

        try:
            while True:
                line = await reader.readline()
                if not line:
                    break
                decoded = line.decode("utf-8", errors="replace").strip()
                if decoded:
                    await self._process_command(decoded, writer)
        except Exception as e:
            logger.warning(f"Fake ESP32 client disconnected: {e}")
        finally:
            if writer in self.clients:
                self.clients.remove(writer)

    async def _process_command(self, line: str, writer: asyncio.StreamWriter) -> None:
        try:
            req = json.loads(line)
        except Exception:
            ack = {"v": 1, "id": "err", "type": "ack", "accepted": False, "code": "PARSE_ERROR"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            return

        cmd_id = req.get("id", "none")
        cmd_type = req.get("type")

        if req.get("v") != 1:
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": False, "code": "UNSUPPORTED_VERSION"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            return

        if cmd_type == "system.ping":
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()

        elif cmd_type == "system.snapshot":
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "system.stop":
            self.laser_duty = 0
            if self.state != "faulted":
                self.state = "idle_homed" if (self.homed_x and self.homed_y) else "idle_unhomed"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "system.estop":
            self.laser_duty = 0
            self.drivers_enabled = False
            self.homed_x = False
            self.homed_y = False
            self.state = "faulted"
            self.fault = "ESTOP_ACTIVATED"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "system.reset_fault":
            self.fault = None
            self.state = "idle_homed" if (self.homed_x and self.homed_y) else "idle_unhomed"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "driver.set_enabled":
            self.drivers_enabled = bool(req.get("enabled", False))
            if not self.drivers_enabled:
                self.homed_x = False
                self.homed_y = False
                self.state = "idle_unhomed"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "home.start":
            self.op_counter += 1
            op_id = f"op-home-{self.op_counter}"
            self.state = "homing"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK", "operation_id": op_id}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

            # Simulate fast seek -> pull off -> slow latch
            async def run_sim_homing():
                await asyncio.sleep(0.05)
                self.homed_x = True
                self.homed_y = True
                self.pos_x = 0
                self.pos_y = 0
                self.state = "idle_homed"
                ev = {
                    "v": 1,
                    "type": "operation.completed",
                    "operation_id": op_id,
                    "result": "success",
                    "homed": {"x": True, "y": True},
                    "pos": {"x": 0, "y": 0}
                }
                await self._broadcast(ev)
                await self._emit_snapshot()

            asyncio.create_task(run_sim_homing())

        elif cmd_type == "motion.jog":
            axis = req.get("axis")
            steps = req.get("steps", 0)
            if axis == "x":
                self.pos_x += steps
            elif axis == "y":
                self.pos_y += steps
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "motion.move_abs":
            if not (self.homed_x and self.homed_y):
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": False, "code": "NOT_HOMED"}
            else:
                self.pos_x = req.get("x", self.pos_x)
                self.pos_y = req.get("y", self.pos_y)
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "laser.pulse":
            duty = req.get("duty", 0)
            if duty > 3685:
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": False, "code": "LASER_DUTY_CLAMPED"}
            else:
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()

        elif cmd_type == "job.begin":
            if not (self.homed_x and self.homed_y):
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": False, "code": "NOT_HOMED"}
            else:
                self.state = "running_job"
                ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        elif cmd_type == "job.segment":
            self.pos_x += req.get("dx", 0)
            self.pos_y += req.get("dy", 0)
            self.laser_duty = req.get("duty", 0)
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()

        elif cmd_type == "job.end":
            self.laser_duty = 0
            self.state = "idle_homed"
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": True, "code": "OK"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()
            await self._emit_snapshot()

        else:
            ack = {"v": 1, "id": cmd_id, "type": "ack", "accepted": False, "code": "UNKNOWN_COMMAND"}
            writer.write((json.dumps(ack) + "\n").encode("utf-8"))
            await writer.drain()

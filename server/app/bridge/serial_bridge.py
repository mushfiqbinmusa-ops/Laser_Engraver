import asyncio
import logging
import serial
from typing import Callable, Awaitable, Optional
from app.bridge.base import ControllerBridge

logger = logging.getLogger(__name__)

class SerialControllerBridge(ControllerBridge):
    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.ser: Optional[serial.Serial] = None
        self._connected = False
        self._line_handler: Optional[Callable[[str], Awaitable[None]]] = None
        self._read_task: Optional[asyncio.Task] = None

    def set_line_handler(self, handler: Callable[[str], Awaitable[None]]) -> None:
        self._line_handler = handler

    def is_connected(self) -> bool:
        return self._connected and self.ser is not None and self.ser.is_open

    async def connect(self) -> bool:
        if self._connected:
            return True
        try:
            loop = asyncio.get_running_loop()
            self.ser = await loop.run_in_executor(
                None, lambda: serial.Serial(self.port, self.baudrate, timeout=0.1)
            )
            self._connected = True
            self._read_task = asyncio.create_task(self._reader_loop())
            logger.info(f"Connected to Serial at {self.port}")
            return True
        except Exception as e:
            logger.warning(f"Failed to open Serial port {self.port}: {e}")
            self._connected = False
            return False

    async def disconnect(self) -> None:
        self._connected = False
        if self._read_task:
            self._read_task.cancel()
            try:
                await self._read_task
            except asyncio.CancelledError:
                pass
            self._read_task = None

        if self.ser and self.ser.is_open:
            self.ser.close()
            self.ser = None

    async def send_frame(self, frame_json: str) -> None:
        if not self.is_connected() or not self.ser:
            raise ConnectionError("Not connected to Serial device")
        
        if not frame_json.endswith("\n"):
            frame_json += "\n"
        
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self.ser.write, frame_json.encode("utf-8"))

    async def _reader_loop(self) -> None:
        loop = asyncio.get_running_loop()
        try:
            while self._connected and self.ser and self.ser.is_open:
                line_bytes = await loop.run_in_executor(None, self.ser.readline)
                if line_bytes:
                    decoded = line_bytes.decode("utf-8", errors="replace").strip()
                    if decoded and self._line_handler:
                        try:
                            await self._line_handler(decoded)
                        except Exception as ex:
                            logger.error(f"Error in serial line handler: {ex}")
                else:
                    await asyncio.sleep(0.01)
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Serial reader loop error: {e}")
        finally:
            self._connected = False

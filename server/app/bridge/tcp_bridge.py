import asyncio
import logging
from typing import Callable, Awaitable, Optional
from app.bridge.base import ControllerBridge

logger = logging.getLogger(__name__)

class TcpControllerBridge(ControllerBridge):
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self._connected = False
        self._line_handler: Optional[Callable[[str], Awaitable[None]]] = None
        self._read_task: Optional[asyncio.Task] = None

    def set_line_handler(self, handler: Callable[[str], Awaitable[None]]) -> None:
        self._line_handler = handler

    def is_connected(self) -> bool:
        return self._connected and self.writer is not None

    async def connect(self) -> bool:
        if self._connected:
            return True
        try:
            self.reader, self.writer = await asyncio.wait_for(
                asyncio.open_connection(self.host, self.port),
                timeout=3.0
            )
            self._connected = True
            self._read_task = asyncio.create_task(self._reader_loop())
            logger.info(f"Connected to ESP32 at {self.host}:{self.port}")
            return True
        except Exception as e:
            logger.warning(f"Failed to connect to ESP32 at {self.host}:{self.port} - {e}")
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

        if self.writer:
            try:
                self.writer.close()
                await self.writer.wait_closed()
            except Exception:
                pass
            self.writer = None
            self.reader = None

    async def send_frame(self, frame_json: str) -> None:
        if not self.is_connected() or not self.writer:
            raise ConnectionError("Not connected to controller")
        
        if not frame_json.endswith("\n"):
            frame_json += "\n"
        
        self.writer.write(frame_json.encode("utf-8"))
        await self.writer.drain()

    async def _reader_loop(self) -> None:
        try:
            while self._connected and self.reader:
                line = await self.reader.readline()
                if not line:
                    logger.warning("TCP socket closed by remote ESP32")
                    break
                decoded = line.decode("utf-8", errors="replace").strip()
                if decoded and self._line_handler:
                    try:
                        await self._line_handler(decoded)
                    except Exception as ex:
                        logger.error(f"Error in line handler: {ex}")
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"TCP reader loop encountered error: {e}")
        finally:
            self._connected = False

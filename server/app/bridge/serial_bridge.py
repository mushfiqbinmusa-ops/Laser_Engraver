import asyncio
import glob
import logging
from pathlib import Path
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
        self._raw_line_listener: Optional[Callable[[str], None]] = None
        self._read_task: Optional[asyncio.Task] = None

    def set_line_handler(self, handler: Callable[[str], Awaitable[None]]) -> None:
        self._line_handler = handler

    def set_raw_line_listener(self, listener: Callable[[str], None]) -> None:
        """Called for every received line, before protocol parsing."""
        self._raw_line_listener = listener

    def is_connected(self) -> bool:
        return self._connected and self.ser is not None and self.ser.is_open

    def get_candidate_ports(self) -> list[str]:
        """Auto-detect available USB serial ports (ESP32-S3 CDC or USB-UART bridges)."""
        candidates: list[str] = []
        if self.port and Path(self.port).exists():
            candidates.append(self.port)
        detected = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        for p in detected:
            if p not in candidates:
                candidates.append(p)
        return candidates

    async def connect(self) -> bool:
        if self._connected and self.ser and self.ser.is_open:
            return True

        candidates = self.get_candidate_ports()
        if not candidates:
            logger.warning("No serial ports found (/dev/ttyACM* or /dev/ttyUSB*)")
            self._connected = False
            return False

        loop = asyncio.get_running_loop()
        for candidate in candidates:
            try:
                def _open():
                    s = serial.Serial(candidate, self.baudrate, timeout=0.1)
                    try:
                        s.dtr = True
                        s.rts = False
                    except Exception:
                        pass
                    return s
                self.ser = await loop.run_in_executor(None, _open)
                self.port = candidate
                self._connected = True
                self._read_task = asyncio.create_task(self._reader_loop())
                logger.info(f"Connected to Serial at {self.port} (baudrate={self.baudrate})")
                return True
            except Exception as e:
                logger.debug(f"Could not open serial port {candidate}: {e}")
                self.ser = None

        logger.warning(f"Failed to open any serial port from candidates: {candidates}")
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
                    if decoded:
                        if self._raw_line_listener:
                            try:
                                self._raw_line_listener(decoded)
                            except Exception as ex:
                                logger.debug(f"Raw line listener error: {ex}")
                        if self._line_handler:
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
            if self.ser:
                try:
                    if self.ser.is_open:
                        self.ser.close()
                except Exception:
                    pass
                self.ser = None

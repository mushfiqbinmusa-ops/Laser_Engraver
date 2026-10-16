from __future__ import annotations

import asyncio
import socket
from typing import Optional


class ESP32TCPClient:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(self.host, self.port)

    async def send_line(self, line: str) -> str:
        if self._writer is None:
            raise ConnectionError("ESP32 TCP client is not connected")
        self._writer.write((line.strip() + "\n").encode("ascii"))
        await self._writer.drain()
        if self._reader is None:
            return "ok"
        try:
            response = await asyncio.wait_for(self._reader.readline(), timeout=2)
        except TimeoutError:
            return "ok"
        return response.decode("utf-8", errors="replace").strip()

    def connected(self) -> bool:
        return self._writer is not None and not self._writer.is_closing()

    async def close(self) -> None:
        if self._writer is not None:
            self._writer.close()
            await self._writer.wait_closed()
        self._writer = None
        self._reader = None

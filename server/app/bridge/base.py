from abc import ABC, abstractmethod
from typing import Callable, Awaitable, Optional

class ControllerBridge(ABC):
    @abstractmethod
    async def connect(self) -> bool:
        pass

    @abstractmethod
    async def disconnect(self) -> None:
        pass

    @abstractmethod
    def is_connected(self) -> bool:
        pass

    @abstractmethod
    async def send_frame(self, frame_json: str) -> None:
        pass

    @abstractmethod
    def set_line_handler(self, handler: Callable[[str], Awaitable[None]]) -> None:
        pass

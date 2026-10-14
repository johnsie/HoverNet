#!/usr/bin/env python3
"""Bridge one browser WebSocket to the native RaceServer TCP protocol."""

import asyncio
import logging
import os
import signal
import sys

import websockets
from websockets.exceptions import ConnectionClosed

# Debian's websockets 9.1 passes ``loop=None`` to several asyncio APIs, but
# Python 3.10 removed that argument. Keep the packaged gateway working on
# hosts that still provide the old distro module.
if sys.version_info >= (3, 10):
    _AsyncioLock = asyncio.Lock
    _asyncio_sleep = asyncio.sleep
    _asyncio_wait = asyncio.wait
    _asyncio_wait_for = asyncio.wait_for

    class _CompatibleAsyncioLock(_AsyncioLock):
        def __init__(self, *args, **kwargs):
            kwargs.pop("loop", None)
            super().__init__(*args, **kwargs)

    async def _compatible_asyncio_sleep(*args, **kwargs):
        kwargs.pop("loop", None)
        return await _asyncio_sleep(*args, **kwargs)

    async def _compatible_asyncio_wait(*args, **kwargs):
        kwargs.pop("loop", None)
        return await _asyncio_wait(*args, **kwargs)

    async def _compatible_asyncio_wait_for(*args, **kwargs):
        kwargs.pop("loop", None)
        return await _asyncio_wait_for(*args, **kwargs)

    asyncio.Lock = _CompatibleAsyncioLock
    asyncio.sleep = _compatible_asyncio_sleep
    asyncio.wait = _compatible_asyncio_wait
    asyncio.wait_for = _compatible_asyncio_wait_for

LOG = logging.getLogger("hovernet-websocket-gateway")
MAX_FRAME_SIZE = 3 + 255
MAX_CONNECTIONS = 64


class Gateway:
    def __init__(self):
        self.slots = asyncio.Semaphore(MAX_CONNECTIONS)

    async def handle(self, websocket, path=None):
        request_path = path or getattr(websocket, "path", None)
        if request_path is None:
            request = getattr(websocket, "request", None)
            request_path = getattr(request, "path", None)
        if request_path != "/race/":
            await websocket.close(code=1008, reason="unknown endpoint")
            return

        if self.slots.locked():
            await websocket.close(code=1013, reason="gateway is at capacity")
            return
        async with self.slots:
            try:
                reader, writer = await asyncio.wait_for(
                    asyncio.open_connection("127.0.0.1", 9600), timeout=5)
            except (OSError, asyncio.TimeoutError):
                await websocket.close(code=1013, reason="race server unavailable")
                return

            peer = websocket.remote_address
            LOG.info("opened websocket bridge for %s", peer)
            try:
                await self.relay(websocket, reader, writer)
            except (ConnectionClosed, asyncio.IncompleteReadError, OSError, asyncio.TimeoutError):
                pass
            finally:
                writer.close()
                try:
                    await writer.wait_closed()
                except OSError:
                    pass
                LOG.info("closed websocket bridge for %s", peer)

    async def relay(self, websocket, reader, writer):
        async def browser_to_server():
            async for message in websocket:
                if not isinstance(message, bytes):
                    await websocket.close(code=1003, reason="binary protocol required")
                    return
                if not valid_frame(message):
                    await websocket.close(code=1009, reason="invalid protocol frame")
                    return
                writer.write(message)
                await writer.drain()

        async def server_to_browser():
            buffered = bytearray()
            while True:
                chunk = await asyncio.wait_for(reader.read(4096), timeout=300)
                if not chunk:
                    return
                buffered.extend(chunk)
                if len(buffered) > 65536:
                    raise OSError("RaceServer sent an incomplete oversized frame burst")
                while len(buffered) >= 3:
                    frame_size = 3 + buffered[2]
                    if len(buffered) < frame_size:
                        break
                    frame = bytes(buffered[:frame_size])
                    del buffered[:frame_size]
                    await websocket.send(frame)

        tasks = [asyncio.create_task(browser_to_server()),
                 asyncio.create_task(server_to_browser())]
        done, pending = await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        for task in pending:
            task.cancel()
        await asyncio.gather(*done, *pending, return_exceptions=True)


def valid_frame(frame):
    return 3 <= len(frame) <= MAX_FRAME_SIZE and len(frame) == 3 + frame[2]


async def main():
    logging.basicConfig(level=os.environ.get("LOG_LEVEL", "INFO"),
                        format="%(asctime)s %(levelname)s %(message)s")
    host = os.environ.get("HOVERNET_WS_HOST", "127.0.0.1")
    port = int(os.environ.get("HOVERNET_WS_PORT", "9601"))
    origin = os.environ.get("HOVERNET_WS_ORIGIN", "https://hovernet.outiva.com")
    gateway = Gateway()
    stop = asyncio.get_running_loop().create_future()

    def request_stop():
        if not stop.done():
            stop.set_result(None)

    for signum in (signal.SIGINT, signal.SIGTERM):
        asyncio.get_running_loop().add_signal_handler(signum, request_stop)

    async with websockets.serve(gateway.handle, host, port, origins=[origin],
                                max_size=MAX_FRAME_SIZE, max_queue=16,
                                ping_interval=20, ping_timeout=20,
                                compression=None):
        LOG.info("listening on %s:%d for origin %s", host, port, origin)
        await stop


if __name__ == "__main__":
    asyncio.run(main())

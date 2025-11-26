import os
import asyncio
import collections
from datetime import datetime, timezone
from typing import Annotated
from fastapi import Request, Depends, WebSocket, HTTPException, WebSocketDisconnect

from td3_egb.settings import settings
from td3_egb.utils import parse_line_to_values

import logging

log = logging.getLogger("deps")

class DeviceReader:
    def __init__(self, path=settings.DEV_DATA_PATH, max_history=500, loop=None):
        self.path = path
        self.loop = loop or asyncio.get_event_loop()
        self.fd = None
        self._buffer = bytearray()
        # history por key: dict -> deque
        self.history = collections.defaultdict(lambda: collections.deque(maxlen=max_history))
        # queues por key (None = global)
        # self.queues: key -> set(asyncio.Queue)
        self.queues = {}  # key -> set(queue)
        # mapping queue -> loop where it was created (used to call_soon_threadsafe)
        self._queue_loops = {}
        self._running = False

    async def start(self):
        log.info("Starting DeviceReader")
        if self._running:
            return
        self._running = True
        # open non-blocking
        self.fd = os.open(self.path, os.O_RDONLY | os.O_NONBLOCK)
        # register callback when fd readable
        self.loop.add_reader(self.fd, self._on_readable)

    async def stop(self):
        log.info("Stopping DeviceReader")
        if not self._running:
            return
        self._running = False
        if self.fd is not None:
            self.loop.remove_reader(self.fd)
            os.close(self.fd)
            self.fd = None
            
    def register_queue(self, q: asyncio.Queue, key=None):
        """Registrar suscriptor; key=None = recibe todos, key='...' recibe sólo ese canal.

        Stores queue in self.queues[key] and remembers the loop where the queue was created.
        """
        loop = asyncio.get_event_loop()
        log.info(f"DeviceReader.register_queue key={key} queue_id={id(q)} loop_id={id(loop)}")
        self.queues.setdefault(key, set()).add(q)
        self._queue_loops[q] = loop

    def unregister_queue(self, q: asyncio.Queue, key=None):
        """Unregister a previously registered queue for a key (or global if key is None)."""
        s = self.queues.get(key)
        if not s:
            # also try to remove from any key sets (defensive)
            for k, qset in list(self.queues.items()):
                if q in qset:
                    qset.discard(q)
                    if not qset:
                        self.queues.pop(k, None)
            self._queue_loops.pop(q, None)
            return
        s.discard(q)
        if not s:
            self.queues.pop(key, None)
        self._queue_loops.pop(q, None)

    def get_history_key(self, key: str):
        return list(self.history.get(key, []))

    def get_opened_keys(self, q: asyncio.Queue):
        """Return list of keys a given queue is subscribed to (or None for global)."""
        keys = []
        for k, qset in self.queues.items():
            if q in qset:
                keys.append(k)
        return keys

    def _broadcast(self, key, msg):
        """Send msg to all queues subscribed to `key` and to global (None) subscribers.

        Uses the recorded loop for each queue to schedule puts safely.
        """
        targets = set()
        targets.update(self.queues.get(None, set()))
        targets.update(self.queues.get(key, set()))
        for q in list(targets):
            try:
                loop = self._queue_loops.get(q)
                # log.info(f"DeviceReader._broadcast scheduling put on loop_id={id(loop) if loop else None} queue_id={id(q)} key={key} msg={msg}")
                if loop and getattr(loop, "is_running", lambda: False)():
                    loop.call_soon_threadsafe(q.put_nowait, {"key": key, "msg": msg})
                else:
                    q.put_nowait({"key": key, "msg": msg})
            except asyncio.QueueFull:
                # cliente lento -> drop para ese cliente
                log.warning(f"DeviceReader._broadcast: queue full, dropping message for queue_id={id(q)} key={key}")
            except Exception:
                log.exception("DeviceReader._broadcast: unexpected error while putting to queue")

    def _on_readable(self):
        try:
            data = os.read(self.fd, 4096)
        except BlockingIOError:
            return
        if not data:
            return
        self._buffer.extend(data)
        while True:
            idx = self._buffer.find(b'\n')
            if idx < 0:
                break
            line = bytes(self._buffer[:idx]).rstrip(b'\r')
            del self._buffer[:idx+1]
            try:
                text = line.decode('utf-8', errors='ignore')
            except Exception:
                text = repr(line)
            # log.info(f"Read line: {text}")
            if not text.startswith("D:"):
                continue
            text = text.lstrip("D:")
            try:
                msg = parse_line_to_values(text)
            except Exception as e:
                log.error(f"Error parsing line '{text}': {e}")
                continue
            # log.info(f"Parsed message: {msg}")
            for key, value in msg.items():
                # guardar en history por key
                self.history[key].append(value)
                # broadcast sólo a los suscriptores pertinentes
                self._broadcast(key, value)

def get_device_reader(request: Request = None, websocket: WebSocket = None) -> DeviceReader:
    req = request or websocket
    reader = getattr(req.app.state, "device_reader", None)
    if reader is None:
        # websocket: close gracefully and raise disconnect so handler stops
        if websocket is not None:
            # close the websocket and raise to stop handler
            # note: this function can be async if you prefer awaiting websocket.close()
            # here we raise WebSocketDisconnect so FastAPI will terminate the ws handler
            raise WebSocketDisconnect(code=1011)
        # http: return 500
        raise HTTPException(status_code=500, detail="DeviceReader not initialized in app state")
    return reader

DevReaderInst = Annotated[DeviceReader, Depends(get_device_reader)]
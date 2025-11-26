import asyncio

from fastapi import APIRouter, HTTPException, Depends, WebSocket, WebSocketDisconnect
from starlette import status

from td3_egb.dependencies import DevReaderInst
import td3_egb.schemas as sch

import logging

log = logging.getLogger("api")

router = APIRouter(prefix="/ebg_ws", tags=["EGB - WebSocket"])

@router.websocket("/ws/egb_data")
async def ws_egb_data(websocket: WebSocket, reader: DevReaderInst, key: str = None):
    """WebSocket endpoint that registers a per-client queue with DeviceReader.

    Accepts optional query parameter `?key=...` to subscribe only to that key.
    """
    await websocket.accept()
    q = asyncio.Queue(maxsize=100)
    log.info(f"WebSocket client connected - creating queue id={id(q)} key={key}")
    # pass the requested key to register_queue so DeviceReader can scope broadcasts
    reader.register_queue(q, key=key)
    try:
        while True:
            log.debug("WS handler waiting for EGB data on queue...")
            msg = await q.get()
            log.debug(f"WS handler got msg from queue id={id(q)} msg={msg}")
            if msg is None:
                # defensive
                await asyncio.sleep(0.5)
                continue
            try:
                await websocket.send_json(msg)
                log.debug(f"Sent via WS to client queue id={id(q)}: {msg}")
            except Exception:
                log.exception("Failed sending message to websocket client")
                break
    except WebSocketDisconnect:
        log.info("WS client disconnected")
    except asyncio.exceptions.CancelledError:
        log.info("EGB data websocket cancelled")
    finally:
        reader.unregister_queue(q, key=key)
        log.info("WS client queue unregistered")
        log.info(f"WebSocket client connections - {reader.get_opened_keys(q)}")
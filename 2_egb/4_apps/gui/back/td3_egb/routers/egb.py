import uuid
import logging
import traceback
from typing import List
from datetime import datetime, timedelta, timezone

from fastapi import APIRouter, HTTPException, Depends
from starlette import status

from td3_egb.utils import get_keys, set_keys, enviar_uart, params_egb, data_keys
import td3_egb.schemas as sch
from td3_egb.dependencies import DevReaderInst

log = logging.getLogger("api")

router = APIRouter(prefix="/egb", tags=["EGB - Actions"])


@router.get(
    "/list",
    status_code=status.HTTP_200_OK,
    name="List EGB Processes"
)
async def list_egb_processes():
    """List all EGB processes."""
    try:
        return params_egb
    except Exception as e:
        log.error(f"Error listing EGB processes: {e}")
        raise HTTPException(status_code=500, detail=f"Error listing EGB processes: {e}")

@router.get(
    "/vars",
    status_code=status.HTTP_200_OK,
    name="Get EGB Variables"
)
async def get_egb_variables():
    return data_keys

@router.post(
    "/start",
    status_code=status.HTTP_200_OK,
    name="Start EGB Process"
)
async def start_egb_process():
    """Start the EGB process."""
    try:
        return {
            "status": "started",
            "data": enviar_uart("$start"),
        }
    except Exception as e:
        log.error(f"Error starting EGB process: {e}")
        raise HTTPException(status_code=500, detail=f"Error starting EGB process: {e}")


@router.post(
    "/stop",
    status_code=status.HTTP_200_OK,
    name="Stop EGB Process"
)
async def stop_egb_process():
    """Stop the EGB process."""
    try:
        return {
            "status": "stopped",
            "data": enviar_uart("$stop"),
        }
    except Exception as e:
        log.error(f"Error stopping EGB process: {e}")
        raise HTTPException(status_code=500, detail=f"Error stopping EGB process: {e}")

@router.post(
    "/action",
    status_code=status.HTTP_200_OK,
    name="Perform EGB Action"
)
async def perform_egb_action(
    action: sch.EGBAction
):
    """Perform a specific action on the EGB."""
    log.info(f"Performing action: {action.action} with value: {action.value}")
    # Simulate performing the action
    if action.action == "set":
        if action.name in set_keys and action.value is not None:
            msg = f"$set {set_keys[action.name]} {action.value}"
        else:
            raise HTTPException(status_code=400, detail="Invalid set action")
    elif action.action == "get":
        if action.name in get_keys:
            msg = f"$get {get_keys[action.name]}"
        else:
            raise HTTPException(status_code=400, detail="Invalid get action")
    else:
        raise HTTPException(status_code=400, detail="Invalid action")
    try:
        return {"data": enviar_uart(msg)}
    except Exception as e:
        log.error(f"Error performing EGB action: {e}")
        raise HTTPException(status_code=500, detail=f"Error performing EGB action: {e}")

@router.get(
    "/history",
    status_code=status.HTTP_200_OK,
    name="Get EGB Data History"
)
async def get_egb_data_history(
    key: str,
    reader: DevReaderInst
):
    """Get historical EGB data."""
    try:
        data = reader.get_history_key(key)
        return {
            "key": key,
            "history": data,
            "length": len(data)
        }
    except Exception as e:
        log.error(f"Error getting EGB data history: {e}")
        raise HTTPException(status_code=500, detail=f"Error getting EGB data history: {e}")
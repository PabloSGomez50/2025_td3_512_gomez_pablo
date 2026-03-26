from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

import logging
import importlib
import asyncio
from contextlib import asynccontextmanager
from scheduler import arg_scheduler

from td3_egb.settings import settings
from td3_egb import routers
from td3_egb.dependencies import DeviceReader

log = logging.getLogger("api")

def import_routers(*, application: FastAPI, base_path: str, module_name: str) -> None:
    """Import routers recursively.

    :param application: FastAPI application
    :param base_path: Base path
    :param module_name: Module name
    """
    module_path = f"{base_path}.{module_name}"
    module = importlib.import_module(module_path)

    if hasattr(module, "router"):
        application.include_router(module.router)

    if hasattr(module, "__all__"):
        for submodule_name in module.__all__:
            import_routers(
                application=application,
                base_path=module_path,
                module_name=submodule_name,
            )


def _set_routers(*, application: FastAPI) -> None:
    """Set routers in the application."""
    for router in routers.__all__:
        import_routers(
            application=application,
            base_path="td3_egb.routers",
            module_name=router,
        )


def _set_middleware(*, application: FastAPI) -> None:
    """Set middleware in the application."""
    # Set all CORS enabled origins
    if settings.BACKEND_CORS_ORIGINS:
        application.add_middleware(
            CORSMiddleware,
            allow_origins=settings.BACKEND_CORS_ORIGINS,
            allow_credentials=True,
            allow_methods=["*"],
            allow_headers=["*"],
        )


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Lifespan context manager for FastAPI application."""
    log.info("Starting lifespan context manager")
    app.state.device_reader = DeviceReader(
        max_history=1000,
        path=settings.DEV_DATA_PATH,
        loop=asyncio.get_running_loop()
    )
    try: 
        await app.state.device_reader.start()
    except Exception as e:
        log.error(f"Error starting DeviceReader: {e}")
    # arg_scheduler.add_job(
    #     update_pagos360_payment_status,
    #     "interval",
    #     hours=settings.PAGOS360_UPDATE_INTERVAL,
    #     id="pagos360_updater",
    #     replace_existing=True,
    #     max_instances=1
    # )
    yield
    log.info("Shutting down lifespan context manager")
    try:
        await app.state.device_reader.stop()
    except Exception as e:
        log.error(f"Error stopping DeviceReader: {e}")
    # arg_scheduler.shutdown(wait=False)


def create_app() -> FastAPI:
    """Create main FastAPI application."""
    server = FastAPI(
        title="API - Carga Electronica EGB TD3",
        version="0.0.1",
        lifespan=lifespan,
        openapi_version="3.1.0",
        contact={
            "name": "Pablo Gomez",
            "email": "pablosgomez50@gmail.com",
        },
        # root_path="/back",
    )

    _set_routers(application=server)
    _set_middleware(application=server)

    @server.get("/")
    async def root():
        return {"message": "TD3 API"}

    return server


app = create_app()
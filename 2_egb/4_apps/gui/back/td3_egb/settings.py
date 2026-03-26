from typing import Any, List
from logging.config import dictConfig

from pydantic import model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict
from dotenv import load_dotenv

from td3_egb.enums import Environment

LOGGING_CONFIG: dict[str, Any] = {
    "version": 1,
    "disable_existing_loggers": False,
    "formatters": {
        "default": {
            "()": "uvicorn.logging.DefaultFormatter",
            "fmt": "%(levelprefix)s [%(name)s] %(message)s",
            "use_colors": False,
        },
        "with_timestamp": {
            "()": "uvicorn.logging.DefaultFormatter",
            "fmt": "%(asctime)s %(levelprefix)s [%(name)s] %(message)s",
            "datefmt": "%Y-%m-%d %H:%M:%S",
            "use_colors": False,
        },
    },
    "handlers": {
        "default": {
            "formatter": "default",
            "class": "logging.StreamHandler",
            "stream": "ext://sys.stderr",
        },
        "with_timestamp": {
            "formatter": "with_timestamp",
            "class": "logging.StreamHandler",
            "stream": "ext://sys.stderr",
        },
    },
    "loggers": {
        "api": {"handlers": ["with_timestamp"], "level": "INFO", "propagate": False},
        "deps": {"handlers": ["default"], "level": "INFO", "propagate": False},
    },
}
dictConfig(LOGGING_CONFIG)

class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        env_ignore_empty=True,
        env_nested_delimiter="__",
        arbitrary_types_allowed=True,
        extra="ignore",
    )
    ENV: Environment

    BACKEND_CORS_ORIGINS: List[str] = [
        "http://localhost:3000",
        "http://localhost:5173",
        "http://192.168.10.104:5173",
        "http://172.26.96.1:5173",
        "http://192.168.2.11:5173",
        "http://192.168.10.226:3000",
        "http://10.24.158.72:3000",
        "http://10.48.127.72:3000",
    ]

    SCHEDULER_ENABLED: bool = False
    DEV_PATH: str = "/dev/egb_commands"
    DEV_DATA_PATH: str = "/dev/egb_data"

    def refresh(self):
        load_dotenv(override=True)  # Reload environment variables from .env file
        self.__init__()



settings = Settings()
settings.refresh()
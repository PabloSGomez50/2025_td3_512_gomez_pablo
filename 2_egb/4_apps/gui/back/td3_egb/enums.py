from enum import Enum

class BaseEnum(str, Enum):
    """Clase base para enumeraciones en el proyecto."""

    @classmethod
    def name_to_list(cls) -> list[str]:
        return [ele.name for ele in cls]  # type: ignore[attr-defined]

    @classmethod
    def value_to_list(cls) -> list[str]:
        return [ele.value for ele in cls]  # type: ignore[attr-defined]

class Environment(BaseEnum):
    """Enumeracion de entornos de ejecucion."""

    LOCAL = "local"
    PROD = "prod"

from td3_egb.routers import (
    internal,
    egb,
    egb_ws,
)


public_routers = ["egb", "egb_ws"]
internal_routers = ["internal"]

__all__ = public_routers + internal_routers

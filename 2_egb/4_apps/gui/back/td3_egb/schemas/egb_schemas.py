from pydantic import BaseModel, Field
from typing import List, Dict, Any, Optional

class EGBAction(BaseModel):
    action: str = Field(..., description="The action to perform on the EGB.")
    name: str = Field(..., description="The name associated with the action.")
    value: Optional[int | float] = Field(None)
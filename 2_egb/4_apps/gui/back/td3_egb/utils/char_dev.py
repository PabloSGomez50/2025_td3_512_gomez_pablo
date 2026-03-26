import os
import re
import select
import time
import sys
import termios

from td3_egb.settings import settings
from td3_egb.utils.const import data_keys


def read_uart(timeout=2.0, path_dev=settings.DEV_PATH):
    fd = os.open(path_dev, os.O_RDONLY | os.O_NONBLOCK)
    resp = None
    rlist, _, _ = select.select([fd], [], [], timeout)
    if rlist:
        data = os.read(fd, 1024)
        resp = data.decode(errors='ignore').strip()
    os.close(fd)
    if resp is None:
        raise TimeoutError("Timeout esperando respuesta del dispositivo.")
    return resp

def enviar_uart(msg, timeout=2.0, path_dev=settings.DEV_PATH):
    msg = msg.strip()
    with open(path_dev, "w") as dev:
        dev.write(msg + "\n")
    return read_uart(timeout, path_dev)

def parse_line_to_values(line: str) -> dict:
    """Parse a line into a dictionary."""
    result = {}
    pairs = line.split(';')

    if len(pairs) == len(data_keys):
        for key, pair in zip(data_keys, pairs):
            if "." in pair:
                result[key] = float(pair.strip())
            else:
                result[key] = int(pair.strip())
        return result
    return {}
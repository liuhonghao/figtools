"""One-way event channel to the C++ host.

stdout carries only JSON lines; everything humans read goes to stderr. That is
what lets the host parse progress without a regex, and it is why `print()` is
banned in the scripts.
"""

from __future__ import annotations

import json
import sys
from typing import Any


def emit(event: str, **fields: Any) -> None:
    payload = {"event": event}
    payload.update(fields)
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def log(message: str) -> None:
    sys.stderr.write(str(message) + "\n")
    sys.stderr.flush()


def fail(message: str) -> None:
    emit("error", message=message)
    log("error: " + message)

"""Plays reply audio on the PC's own speakers (SPEAK_ON_PC=1)."""
from __future__ import annotations

import logging
import os
import shutil
import subprocess
import sys
import tempfile
import threading

log = logging.getLogger("geist.localplay")
_lock = threading.Lock()


def _play_blocking(wav: bytes) -> None:
    with _lock:  # one reply at a time
        try:
            if sys.platform == "win32":
                import winsound

                winsound.PlaySound(wav, winsound.SND_MEMORY)
                return
            player = shutil.which("afplay") or shutil.which("aplay") or shutil.which("paplay")
            if not player:
                log.warning("no audio player found (afplay/aplay/paplay)")
                return
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                f.write(wav)
                path = f.name
            try:
                subprocess.run([player, path], check=False, capture_output=True, timeout=120)
            finally:
                os.unlink(path)
        except Exception:  # noqa: BLE001
            log.exception("playing audio on the PC failed")


def play(wav: bytes) -> None:
    """Starts playback in the background and returns immediately."""
    threading.Thread(target=_play_blocking, args=(wav,), daemon=True).start()

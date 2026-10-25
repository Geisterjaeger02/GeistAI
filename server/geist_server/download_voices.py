"""Download Piper voices.

    python -m geist_server.download_voices            # all default languages
    python -m geist_server.download_voices de en es   # just these
"""
from __future__ import annotations

import sys
import urllib.request
from pathlib import Path

from .config import Config

BASE = "https://huggingface.co/rhasspy/piper-voices/resolve/main"


def voice_url(name: str, ext: str) -> str:
    # de_DE-thorsten-medium -> de/de_DE/thorsten/medium/de_DE-thorsten-medium.onnx
    locale, speaker, quality = name.split("-", 2)
    lang = locale.split("_")[0]
    return f"{BASE}/{lang}/{locale}/{speaker}/{quality}/{name}{ext}"


def download(name: str, target: Path) -> bool:
    target.mkdir(parents=True, exist_ok=True)
    for ext in (".onnx", ".onnx.json"):
        dest = target / f"{name}{ext}"
        if dest.exists() and dest.stat().st_size > 0:
            continue
        url = voice_url(name, ext)
        print(f"  {url}")
        try:
            urllib.request.urlretrieve(url, dest)
        except Exception as e:  # noqa: BLE001
            print(f"  ! failed: {e}")
            dest.unlink(missing_ok=True)
            return False
    return True


def main(argv: list[str]) -> int:
    cfg = Config.from_env()
    langs = argv or sorted(cfg.voices)
    ok = True
    for lang in langs:
        name = cfg.voices.get(lang)
        if not name:
            print(f"{lang}: no voice configured (set VOICE_{lang.upper()}=<piper voice name>)")
            ok = False
            continue
        print(f"{lang}: {name}")
        ok &= download(name, cfg.voices_dir)
    print(f"voices are in {cfg.voices_dir.resolve()}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

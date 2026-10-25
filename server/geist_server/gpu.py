"""Makes NVIDIA's CUDA libraries from pip findable on Windows.

faster-whisper (CTranslate2) needs cuBLAS 12 and cuDNN 9 to use an NVIDIA GPU.
The easy way to get them is `pip install -r requirements-gpu.txt`, which puts
the DLLs into site-packages/nvidia/*/bin. Windows doesn't look there on its
own, so we register those folders before faster-whisper is imported.
"""
from __future__ import annotations

import importlib.util
import logging
import os
import sys
from pathlib import Path

log = logging.getLogger("geist.gpu")
_done = False


def nvidia_lib_dirs() -> list[Path]:
    spec = importlib.util.find_spec("nvidia")
    if spec is None or not spec.submodule_search_locations:
        return []
    dirs = []
    for root in spec.submodule_search_locations:
        for sub in sorted(Path(root).glob("*")):
            for name in ("bin", "lib"):
                d = sub / name
                if d.is_dir() and any(d.glob("*.dll" if sys.platform == "win32" else "*.so*")):
                    dirs.append(d)
    return dirs


def register_cuda_libraries() -> None:
    global _done
    if _done:
        return
    _done = True
    dirs = nvidia_lib_dirs()
    if not dirs:
        return
    if sys.platform == "win32":
        for d in dirs:
            try:
                os.add_dll_directory(str(d))
            except OSError:
                pass
        # CTranslate2 loads cuBLAS/cuDNN lazily with a plain LoadLibrary, which uses PATH.
        os.environ["PATH"] = os.pathsep.join(str(d) for d in dirs) + os.pathsep + os.environ.get("PATH", "")
    log.info("using NVIDIA libraries from %s", ", ".join(d.parent.name for d in dirs))


def cuda_devices() -> int:
    try:
        import ctranslate2

        return ctranslate2.get_cuda_device_count()
    except Exception:  # noqa: BLE001
        return 0

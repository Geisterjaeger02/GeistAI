"""Run with:  python -m geist_server"""
import logging
import sys
import time

import uvicorn

from .app import create_app
from .config import Config


log = logging.getLogger("geist")


def warmup(assistant) -> None:
    """Load speech recognition, the language model and the voices before the
    first question, otherwise the first answer can take over a minute."""
    for name, part in (("speech recognition", assistant.stt), ("language model", assistant.llm),
                       ("voices", assistant.tts)):
        if not hasattr(part, "warmup"):
            continue
        t0 = time.perf_counter()
        log.info("loading %s ...", name)
        try:
            part.warmup()
            log.info("%s ready (%.1f s)", name, time.perf_counter() - t0)
        except Exception as e:  # noqa: BLE001 - keep starting, show the problem
            log.warning("could not load %s: %s", name, e)
    log.info("GeistAI is ready - ask away!")


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)-14s %(message)s",
                        datefmt="%H:%M:%S")
    cfg = Config.from_env()
    app = create_app(cfg)
    if "--no-warmup" not in sys.argv:
        warmup(app.state.assistant)
    uvicorn.run(app, host=cfg.host, port=cfg.port, log_level="warning")


if __name__ == "__main__":
    main()

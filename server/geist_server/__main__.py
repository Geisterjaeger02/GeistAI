"""Run with:  python -m geist_server"""
import logging

import uvicorn

from .app import create_app
from .config import Config


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)-14s %(message)s",
                        datefmt="%H:%M:%S")
    cfg = Config.from_env()
    app = create_app(cfg)
    if "--no-warmup" not in __import__("sys").argv:
        # Load Whisper now so the first question isn't slow.
        app.state.assistant.stt.warmup()
    uvicorn.run(app, host=cfg.host, port=cfg.port, log_level="warning")


if __name__ == "__main__":
    main()

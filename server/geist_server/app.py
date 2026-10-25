"""HTTP API for the GeistAI device (see docs/protocol.md)."""
from __future__ import annotations

import asyncio
import logging
import socket
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse, JSONResponse, Response
from pydantic import BaseModel
from starlette.concurrency import run_in_threadpool

from . import __version__
from .config import Config
from .llm import ChatError, LLMClient
from .pipeline import Assistant
from .stt import WhisperSTT
from .tts import PiperTTS

log = logging.getLogger("geist")


class TextIn(BaseModel):
    text: str


def lan_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("10.255.255.255", 1))  # no packet is sent
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def create_app(cfg: Config | None = None, assistant: Assistant | None = None) -> FastAPI:
    cfg = cfg or Config.from_env()
    if assistant is None:
        speak = None
        if cfg.speak_on_pc:
            from .localplay import play as speak
        assistant = Assistant(WhisperSTT(cfg), LLMClient(cfg), PiperTTS(cfg), speak_locally=speak)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        # zeroconf's sync API must not run inside the event loop -> use a worker thread
        zc = await asyncio.to_thread(announce, cfg.port) if cfg.announce_mdns else None
        tts = assistant.tts
        langs = tts.available_languages() if hasattr(tts, "available_languages") else []
        log.info("GeistAI server %s on http://%s:%d  (LLM %s via %s, voices: %s)", __version__,
                 lan_ip(), cfg.port, cfg.llm_model, cfg.llm_backend, ", ".join(langs) or "none")
        yield
        if zc:
            await asyncio.to_thread(zc.close)

    app = FastAPI(title="GeistAI server", version=__version__, lifespan=lifespan)
    app.state.cfg = cfg
    app.state.assistant = assistant

    @app.get("/health")
    def health():
        tts = assistant.tts
        return {
            "ok": True,
            "name": "GeistAI",
            "version": __version__,
            "stt": cfg.whisper_model,
            "llm": f"{cfg.llm_backend}:{cfg.llm_model}",
            "voices": tts.available_languages() if hasattr(tts, "available_languages") else [],
        }

    @app.post("/api/chat")
    async def chat(request: Request, device: str = "unknown", lang: str = "auto", tts: int = 1):
        data = await request.body()
        if not data:
            raise HTTPException(400, "empty body")
        try:
            result = await run_in_threadpool(assistant.handle_audio, data, device, lang, bool(tts))
        except ChatError as e:
            log.error("chat failed: %s", e)
            return JSONResponse({"ok": False, "error": str(e)}, status_code=502)
        return result.to_json()

    @app.post("/api/text")
    def text(body: TextIn, device: str = "web", lang: str = "auto", tts: int = 0):
        try:
            return assistant.handle_text(body.text, device, lang, bool(tts)).to_json()
        except ChatError as e:
            return JSONResponse({"ok": False, "error": str(e)}, status_code=502)

    @app.post("/api/reset")
    def reset(device: str = "unknown"):
        assistant.reset(device)
        return {"ok": True}

    @app.get("/api/audio/{audio_id}.wav")
    def get_audio(audio_id: str):
        data = assistant.cache.get(audio_id)
        if data is None:
            raise HTTPException(404, "expired")
        return Response(content=data, media_type="audio/wav")

    @app.get("/", response_class=HTMLResponse)
    def index():
        return STATUS_PAGE.replace("{{version}}", __version__).replace(
            "{{llm}}", f"{cfg.llm_backend}: {cfg.llm_model}").replace("{{stt}}", cfg.whisper_model)

    return app


def announce(port: int):
    """Advertise _geistai._tcp so the T-Embed finds the server without configuration."""
    try:
        from zeroconf import ServiceInfo, Zeroconf
    except ImportError:
        log.info("zeroconf not installed - set GEIST_SERVER_URL on the device instead")
        return None
    ip = lan_ip()
    host = socket.gethostname().split(".")[0]
    info = ServiceInfo(
        "_geistai._tcp.local.",
        f"GeistAI on {host}._geistai._tcp.local.",
        addresses=[socket.inet_aton(ip)],
        port=port,
        properties={"version": __version__},
        server=f"{host}.local.",
    )
    zc = Zeroconf()
    try:
        zc.register_service(info, allow_name_change=True)
        log.info("announced via mDNS as _geistai._tcp on %s:%d", ip, port)
        return zc
    except Exception as e:  # noqa: BLE001 - mDNS is a convenience, never fatal
        log.warning("mDNS announcement failed (%s: %s). The server still works - put "
                    "GEIST_SERVER_URL=\"http://%s:%d\" into the device's secrets.h.",
                    type(e).__name__, e, ip, port)
        zc.close()
        return None


STATUS_PAGE = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>GeistAI server</title>
<link href="https://fonts.googleapis.com/css2?family=Dancing+Script:wght@700&family=Inter:wght@400;600&display=swap" rel="stylesheet">
<style>
body{margin:0;min-height:100vh;display:grid;place-items:center;font-family:Inter,system-ui,sans-serif;
background:linear-gradient(#28f6ff,#00cef0);color:#001824}
main{background:rgba(255,255,255,.55);padding:28px 32px;border-radius:18px;max-width:560px;width:calc(100% - 32px);box-sizing:border-box}
h1{font-family:'Dancing Script',cursive;font-size:56px;margin:0 0 4px}
code{background:rgba(0,24,36,.08);padding:2px 6px;border-radius:6px}
textarea{width:100%;box-sizing:border-box;border-radius:10px;border:0;padding:10px;font:inherit}
button{margin-top:8px;background:#001824;color:#96faff;border:0;border-radius:999px;padding:8px 18px;font:inherit;cursor:pointer}
#out{white-space:pre-wrap;margin-top:12px}
</style></head><body><main>
<h1>GeistAI</h1>
<p>Server <b>{{version}}</b> is running. Speech: <code>whisper {{stt}}</code> &middot; LLM: <code>{{llm}}</code></p>
<p>Try it without the device (text in any language):</p>
<textarea id="q" rows="2">Hallo! Wer bist du?</textarea>
<button onclick="ask()">Ask</button>
<div id="out"></div>
<script>
async function ask(){
  const out=document.getElementById('out'); out.textContent='…';
  const r=await fetch('/api/text?device=web&lang=auto&tts=1',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({text:document.getElementById('q').value})});
  const j=await r.json(); out.textContent=j.ok?j.reply:('Error: '+j.error);
  if(j.audio){new Audio(j.audio).play();}
}
</script></main></body></html>"""

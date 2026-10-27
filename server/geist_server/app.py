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
from starlette.requests import ClientDisconnect

from . import audio as wavutil

from . import __version__
from .config import Config
from .llm import ChatError, LLMClient
from .llm import ChatOptions
from .pipeline import Assistant
from .stt import WhisperSTT
from .tts import PiperTTS

log = logging.getLogger("geist")


class TextIn(BaseModel):
    text: str


class VoiceIn(BaseModel):
    voice: str


class SpeedIn(BaseModel):
    scale: float


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
        if cfg.tools_enabled:
            log.info("internet lookups on (weather, search, Wikipedia), home: %s",
                     cfg.home_location or "not set - add HOME_LOCATION=<your town> to .env")
        else:
            log.info("internet lookups off (INTERNET=0)")
        if cfg.openai_api_key:
            log.info("ChatGPT available (%s) - choose it on the device: Settings -> AI model",
                     cfg.openai_model)
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
            "internet": cfg.tools_enabled,
            "cloud": bool(cfg.openai_api_key),
        }

    @app.post("/api/chat")
    async def chat(request: Request, device: str = "unknown", lang: str = "auto", tts: int = 1,
                   ai: str = "local", name: str = ""):
        try:
            data = await request.body()  # also handles chunked uploads (streaming devices)
        except ClientDisconnect:
            log.info("[%s] upload cancelled", device)
            return Response(status_code=499)
        if not data:
            raise HTTPException(400, "empty body")
        content_type = request.headers.get("content-type", "")
        if content_type.lower().startswith("audio/l16"):
            rate, channels = wavutil.parse_l16_params(content_type)
            data = wavutil.pcm16_to_wav(data, rate, channels)
        try:
            options = ChatOptions(cloud=ai == "cloud", name=name)
            result = await run_in_threadpool(assistant.handle_audio, data, device, lang, bool(tts),
                                             options)
        except ChatError as e:
            log.error("chat failed: %s", e)
            return JSONResponse({"ok": False, "error": str(e)}, status_code=502)
        return result.to_json()

    @app.post("/api/text")
    def text(body: TextIn, device: str = "web", lang: str = "auto", tts: int = 0,
             ai: str = "local", name: str = ""):
        options = ChatOptions(cloud=ai == "cloud", name=name)
        try:
            return assistant.handle_text(body.text, device, lang, bool(tts), options).to_json()
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

    # ---------------------------------------------------------------- voices
    from .firstrun import set_env
    from .textutil import LANGUAGE_NAMES
    from .voices import Catalog, parse, sample_text

    catalog = Catalog(cfg)

    @app.get("/voices", response_class=HTMLResponse)
    def voices_page():
        return VOICES_PAGE

    @app.get("/api/voices")
    def voices_overview():
        tts = assistant.tts
        langs = list(dict.fromkeys(["de", "en"] + list(cfg.allowed_languages or cfg.voices)))
        return {
            "speed": cfg.tts_length_scale,
            "openai": bool(cfg.openai_api_key),
            "languages": [{"code": lang, "name": LANGUAGE_NAMES.get(lang, lang),
                           "current": tts.voice_id(lang) if hasattr(tts, "voice_id") else ""}
                          for lang in langs],
        }

    @app.get("/api/voices/{lang}")
    def voice_options(lang: str):
        current = assistant.tts.voice_id(lang) if hasattr(assistant.tts, "voice_id") else ""
        cur_engine, cur_name = parse(current)
        return {"current": f"{cur_engine}:{cur_name}", "options": catalog.options(lang)}

    @app.get("/api/voices/{lang}/preview")
    def voice_preview(lang: str, voice: str):
        wav = assistant.tts.synthesize(sample_text(lang, cfg.assistant_name), lang, voice=voice,
                                       wait_s=240)
        if not wav:
            raise HTTPException(503, "this voice could not speak")
        return Response(content=wav, media_type="audio/wav")

    @app.post("/api/voices/{lang}")
    def voice_select(lang: str, body: VoiceIn):
        engine, name = parse(body.voice)
        if not name:
            raise HTTPException(400, "no voice")
        value = name if engine == "piper" else f"{engine}:{name}"
        cfg.voices[lang] = value
        set_env(cfg.env_file, f"VOICE_{lang.upper()}", value)
        log.info("voice for %s is now %s", lang, value)
        return {"ok": True, "voice": value}

    @app.post("/api/speed")
    def speed_select(body: SpeedIn):
        cfg.tts_length_scale = min(2.0, max(0.5, body.scale))
        set_env(cfg.env_file, "TTS_LENGTH_SCALE", f"{cfg.tts_length_scale:g}")
        return {"ok": True, "scale": cfg.tts_length_scale}

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
<p><a href="/voices">Choose the voices &rarr;</a></p>
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


VOICES_PAGE = """<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>GeistAI voices</title>
<link href="https://fonts.googleapis.com/css2?family=Dancing+Script:wght@700&family=Inter:wght@400;600&display=swap" rel="stylesheet">
<style>
body{margin:0;min-height:100vh;font-family:Inter,system-ui,sans-serif;background:linear-gradient(#28f6ff,#00cef0) fixed;color:#001824}
main{max-width:640px;margin:0 auto;padding:24px 16px 60px}
h1{font-family:'Dancing Script',cursive;font-size:52px;margin:0}
h2{font-size:18px;margin:26px 0 8px}
.bar{display:flex;flex-wrap:wrap;gap:8px;align-items:center}
select,button{font:inherit;border:0;border-radius:999px;padding:8px 14px;cursor:pointer}
select{background:#ffffffcc}
.seg button{background:#ffffff80;color:#001824}.seg button.on{background:#001824;color:#96faff}
.group{background:#ffffff8c;border-radius:16px;padding:6px 14px;margin-top:12px}
.group h3{font-size:15px;margin:10px 0 2px}.group p{margin:0 0 6px;font-size:13px;color:#00566c}
.row{display:flex;align-items:center;gap:8px;padding:7px 0;border-top:1px solid #00182412}
.row span{flex:1}.row .meta{font-size:12px;color:#00566c}
.play{background:#ffffffcc;width:38px;height:34px;padding:0}
.pick{background:#001824;color:#96faff}.cur{font-weight:600;color:#00566c;padding:0 8px}
#msg{min-height:20px;margin-top:10px;font-size:14px}
a{color:#001824}
</style></head><body><main>
<h1>GeistAI</h1>
<div id="t-title" style="font-size:20px;font-weight:600"></div>
<h2 id="t-lang"></h2>
<div class="bar"><select id="lang"></select></div>
<h2 id="t-speed"></h2>
<div class="bar seg" id="speed"></div>
<div id="msg"></div>
<div id="list"></div>
<p style="margin-top:30px"><a href="/">&larr; GeistAI</a></p>
<script>
const de = (navigator.language||'').startsWith('de');
const T = de ? {
  title:'Stimmen', lang:'Sprache', speed:'Sprechtempo', speeds:['Langsamer','Normal','Schneller'],
  piper:'Lokal', piperNote:'Kostenlos, läuft auf deinem PC, ohne Internet. Neue Stimmen werden beim ersten Anhören geladen (ca. 60 MB).',
  edge:'Microsoft (natürlich klingend)', edgeNote:'Kostenlos, braucht Internet. Der Antworttext geht an Microsoft.',
  openai:'OpenAI', openaiNote:'Braucht deinen OpenAI-Schlüssel und kostet ein wenig pro Antwort.',
  pick:'Nehmen', current:'aktiv', loading:'Lade …', saved:'Gespeichert! Ab der nächsten Antwort spricht GeistAI so.',
  failed:'Diese Stimme konnte nicht sprechen.', installed:'installiert'
} : {
  title:'Voices', lang:'Language', speed:'Speaking speed', speeds:['Slower','Normal','Faster'],
  piper:'Local', piperNote:'Free, runs on your PC, works offline. New voices download the first time you listen (about 60 MB).',
  edge:'Microsoft (natural sounding)', edgeNote:'Free, needs internet. The answer text is sent to Microsoft.',
  openai:'OpenAI', openaiNote:'Needs your OpenAI key and costs a little per answer.',
  pick:'Use', current:'active', loading:'Loading …', saved:'Saved! GeistAI uses this voice from the next answer on.',
  failed:'This voice could not speak.', installed:'installed'
};
for (const k of ['title','lang','speed']) document.getElementById('t-'+k).textContent = T[k];
const msg = t => document.getElementById('msg').textContent = t || '';
let overview, audio;

async function load() {
  overview = await (await fetch('/api/voices')).json();
  const sel = document.getElementById('lang');
  for (const l of overview.languages) {
    const o = document.createElement('option'); o.value = l.code; o.textContent = l.name; sel.appendChild(o);
  }
  sel.onchange = () => showLang(sel.value);
  const sp = document.getElementById('speed');
  [1.2, 1.0, 0.85].forEach((v, i) => {
    const b = document.createElement('button'); b.textContent = T.speeds[i];
    b.className = Math.abs(overview.speed - v) < 0.01 ? 'on' : '';
    b.onclick = async () => {
      await fetch('/api/speed', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({scale:v})});
      overview.speed = v; [...sp.children].forEach(c => c.className = ''); b.className = 'on'; msg(T.saved);
    };
    sp.appendChild(b);
  });
  showLang(sel.value);
}

async function showLang(lang) {
  const list = document.getElementById('list'); list.textContent = ''; msg(T.loading);
  const data = await (await fetch('/api/voices/' + lang)).json(); msg('');
  for (const eng of ['piper', 'edge', 'openai']) {
    const opts = data.options.filter(o => o.engine === eng);
    if (!opts.length) continue;
    const g = document.createElement('div'); g.className = 'group';
    const h = document.createElement('h3'); h.textContent = T[eng]; g.appendChild(h);
    const p = document.createElement('p'); p.textContent = T[eng + 'Note']; g.appendChild(p);
    for (const o of opts) {
      const r = document.createElement('div'); r.className = 'row';
      const play = document.createElement('button'); play.className = 'play'; play.textContent = '\u25B6';
      play.title = 'Play';
      play.onclick = async () => {
        if (audio) audio.pause();
        play.textContent = '\u2026'; msg(T.loading);
        const res = await fetch('/api/voices/' + lang + '/preview?voice=' + encodeURIComponent(o.id));
        play.textContent = '\u25B6';
        if (!res.ok) { msg(T.failed); return; }
        msg(''); audio = new Audio(URL.createObjectURL(await res.blob())); audio.play();
      };
      const label = document.createElement('span'); label.textContent = o.label;
      if (o.installed) { const m = document.createElement('span'); m.className = 'meta'; m.textContent = ' \u00b7 ' + T.installed; label.appendChild(m); }
      r.append(play, label);
      if (o.id === data.current) {
        const c = document.createElement('span'); c.className = 'cur'; c.textContent = '\u2713 ' + T.current; c.style.flex = '0';
        r.appendChild(c);
      } else {
        const pick = document.createElement('button'); pick.className = 'pick'; pick.textContent = T.pick;
        pick.onclick = async () => {
          const res = await fetch('/api/voices/' + lang, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({voice:o.id})});
          if (res.ok) { await showLang(lang); msg(T.saved); }
        };
        r.appendChild(pick);
      }
      g.appendChild(r);
    }
    list.appendChild(g);
  }
}
load();
</script></main></body></html>"""

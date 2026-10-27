"""All voices GeistAI can speak with, from three sources:

* piper   - local and free. Every voice of the Piper project (downloaded on first use).
* edge    - Microsoft's natural online voices (the ones in Edge's "read aloud"),
            free, needs internet; the answer text is sent to Microsoft.
* openai  - OpenAI's voices, needs OPENAI_API_KEY and costs a little per answer.

A voice id is "engine:name", e.g. "edge:de-DE-KatjaNeural". A bare name means
Piper ("de_DE-thorsten-high"), so older .env files keep working.
"""
from __future__ import annotations

import json
import logging
import threading
import time
from pathlib import Path

from .config import DEFAULT_VOICES, Config

log = logging.getLogger("geist.voices")

PIPER_CATALOG_URL = "https://huggingface.co/rhasspy/piper-voices/resolve/main/voices.json"

OPENAI_VOICES = ["alloy", "ash", "ballad", "coral", "echo", "fable", "nova", "onyx", "sage",
                 "shimmer", "verse"]

# A sentence per language for "listen" buttons ({name} = the assistant's name).
SAMPLES = {
    "de": "Hallo! Ich bin {name}. So klinge ich, wenn ich dir antworte.",
    "en": "Hi! I'm {name}. This is how I sound when I answer you.",
    "es": "¡Hola! Soy {name}. Así sueno cuando te respondo.",
    "fr": "Salut ! Je suis {name}. Voilà ma voix quand je te réponds.",
    "it": "Ciao! Sono {name}. Ecco come suono quando ti rispondo.",
    "pt": "Olá! Eu sou {name}. É assim que eu falo quando te respondo.",
    "nl": "Hoi! Ik ben {name}. Zo klink ik als ik je antwoord geef.",
    "pl": "Cześć! Jestem {name}. Tak brzmię, kiedy ci odpowiadam.",
    "tr": "Merhaba! Ben {name}. Sana cevap verirken sesim böyle.",
    "ru": "Привет! Я {name}. Вот так звучит мой голос.",
    "uk": "Привіт! Я {name}. Ось так звучить мій голос.",
    "cs": "Ahoj! Jsem {name}. Takhle zním, když ti odpovídám.",
    "sk": "Ahoj! Som {name}. Takto znie môj hlas.",
    "sv": "Hej! Jag är {name}. Så här låter jag när jag svarar dig.",
    "da": "Hej! Jeg er {name}. Sådan lyder jeg, når jeg svarer dig.",
    "no": "Hei! Jeg er {name}. Slik høres jeg ut når jeg svarer deg.",
    "fi": "Hei! Olen {name}. Tältä kuulostan, kun vastaan sinulle.",
    "hu": "Szia! {name} vagyok. Így szól a hangom.",
    "ro": "Salut! Sunt {name}. Așa sună vocea mea.",
    "el": "Γεια σου! Είμαι ο {name}. Έτσι ακούγεται η φωνή μου.",
    "ca": "Hola! Sóc {name}. Així sona la meva veu.",
    "sr": "Zdravo! Ja sam {name}. Ovako zvuči moj glas.",
    "ar": "مرحبا! أنا {name}. هكذا يبدو صوتي.",
    "fa": "سلام! من {name} هستم. صدای من این‌طوری است.",
    "hi": "नमस्ते! मैं {name} हूँ। मेरी आवाज़ ऐसी है।",
    "zh": "你好！我是{name}。这就是我的声音。",
    "vi": "Xin chào! Tôi là {name}. Đây là giọng nói của tôi.",
    "id": "Halo! Saya {name}. Beginilah suara saya.",
}


def parse(voice_id: str) -> tuple[str, str]:
    """'edge:de-DE-KatjaNeural' -> ('edge', 'de-DE-KatjaNeural'); bare names are Piper."""
    voice_id = (voice_id or "").strip()
    engine, sep, name = voice_id.partition(":")
    if sep and engine in ("piper", "edge", "openai"):
        return engine, name
    return "piper", voice_id


def sample_text(lang: str, name: str = "GeistAI") -> str:
    return SAMPLES.get(lang, SAMPLES["en"]).format(name=name or "GeistAI")


class Catalog:
    """Lists the voices available per language (cached; network lists refresh daily)."""

    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._piper: dict | None = None
        self._edge: list | None = None
        self._edge_time = 0.0
        self._lock = threading.Lock()

    # ---- Piper
    def piper_catalog(self) -> dict:
        with self._lock:
            if self._piper is not None:
                return self._piper
            cache = self.cfg.voices_dir / "voices.json"
            data = None
            try:
                fresh = cache.is_file() and time.time() - cache.stat().st_mtime < 7 * 86400
                if not fresh:
                    import httpx

                    r = httpx.get(PIPER_CATALOG_URL, timeout=20, follow_redirects=True)
                    r.raise_for_status()
                    cache.parent.mkdir(parents=True, exist_ok=True)
                    cache.write_text(r.text, encoding="utf-8")
                data = json.loads(cache.read_text(encoding="utf-8"))
            except Exception as e:  # offline: the built-in defaults are still offered
                log.info("Piper voice list not available (%s)", e)
                if cache.is_file():
                    try:
                        data = json.loads(cache.read_text(encoding="utf-8"))
                    except ValueError:
                        data = None
            self._piper = data or {}
            return self._piper

    def piper_voices(self, lang: str) -> list[dict]:
        out, seen = [], set()
        for key, v in sorted(self.piper_catalog().items()):
            family = (v.get("language") or {}).get("family", key.split("_")[0])
            if family != lang or v.get("num_speakers", 1) > 1:
                continue
            quality = v.get("quality", key.rsplit("-", 1)[-1])
            if quality == "x_low":
                continue  # too rough to be worth offering
            country = (v.get("language") or {}).get("country_english", "")
            name = v.get("name", key.split("-")[1] if "-" in key else key)
            out.append({"id": f"piper:{key}", "engine": "piper",
                        "label": f"{name.replace('_', ' ').title()} · {quality}"
                                 + (f" · {country}" if country else ""),
                        "installed": (self.cfg.voices_dir / f"{key}.onnx").is_file()})
            seen.add(key)
        default = DEFAULT_VOICES.get(lang)
        if default and default not in seen:  # catalog unavailable: at least the default
            out.insert(0, {"id": f"piper:{default}", "engine": "piper", "label": default,
                           "installed": (self.cfg.voices_dir / f"{default}.onnx").is_file()})
        return out

    # ---- Microsoft
    def edge_list(self) -> list:
        with self._lock:
            if self._edge is not None and time.time() - self._edge_time < 86400:
                return self._edge
        try:
            import asyncio

            import edge_tts

            voices = asyncio.run(edge_tts.list_voices())
        except Exception as e:
            log.info("Microsoft voice list not available (%s)", e)
            voices = self._edge or []
        with self._lock:
            self._edge, self._edge_time = voices, time.time()
        return voices

    def edge_voices(self, lang: str) -> list[dict]:
        out = []
        prefix = "nb-" if lang == "no" else f"{lang}-"
        for v in self.edge_list():
            loc = v.get("Locale", "")
            if not loc.lower().startswith(prefix.lower()):
                continue
            short = v.get("ShortName", "")
            nice = short.split("-", 2)[-1].replace("Neural", "")
            gender = {"Female": "♀", "Male": "♂"}.get(v.get("Gender", ""), "")
            out.append({"id": f"edge:{short}", "engine": "edge",
                        "label": f"{nice} · {loc} {gender}".strip()})
        return out

    # ---- OpenAI
    def openai_voices(self) -> list[dict]:
        if not self.cfg.openai_api_key:
            return []
        return [{"id": f"openai:{v}", "engine": "openai", "label": v.title()}
                for v in OPENAI_VOICES]

    def options(self, lang: str) -> list[dict]:
        return self.piper_voices(lang) + self.edge_voices(lang) + self.openai_voices()

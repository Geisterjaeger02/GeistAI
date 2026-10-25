"""The voice turn: audio -> text -> answer -> speech. Framework-free so it is
easy to test with fake engines."""
from __future__ import annotations

import logging
import secrets
import threading
import time
from collections import OrderedDict
from dataclasses import dataclass, field

from . import audio as wav
from .textutil import for_display, for_speech, guess_language

log = logging.getLogger("geist.pipeline")

SILENCE_RMS = 0.002  # below this the recording is considered empty


@dataclass
class TurnResult:
    ok: bool
    transcript: str = ""
    reply: str = ""
    language: str = ""
    audio_id: str | None = None
    error: str = ""
    timings: dict = field(default_factory=dict)

    def to_json(self) -> dict:
        return {
            "ok": self.ok,
            "transcript": self.transcript,
            "reply": self.reply,
            "language": self.language,
            "audio": f"/api/audio/{self.audio_id}.wav" if self.audio_id else None,
            "error": self.error,
            "timings": self.timings,
        }


class AudioCache:
    """Keeps synthesized replies around just long enough for the device to fetch them."""

    def __init__(self, max_items: int = 24, ttl_s: int = 600):
        self.max_items, self.ttl_s = max_items, ttl_s
        self._items: OrderedDict[str, tuple[float, bytes]] = OrderedDict()
        self._lock = threading.Lock()

    def put(self, data: bytes) -> str:
        key = secrets.token_urlsafe(9)
        with self._lock:
            self._items[key] = (time.time(), data)
            while len(self._items) > self.max_items:
                self._items.popitem(last=False)
        return key

    def get(self, key: str) -> bytes | None:
        with self._lock:
            item = self._items.get(key)
            if not item or time.time() - item[0] > self.ttl_s:
                self._items.pop(key, None)
                return None
            return item[1]


class Assistant:
    def __init__(self, stt, llm, tts, cache: AudioCache | None = None):
        self.stt, self.llm, self.tts = stt, llm, tts
        self.cache = cache or AudioCache()

    @staticmethod
    def _lang_arg(lang: str | None) -> str | None:
        lang = (lang or "").strip().lower()
        return None if lang in ("", "auto") else lang

    def handle_audio(self, data: bytes, device: str, lang: str | None, want_voice: bool) -> TurnResult:
        t0 = time.perf_counter()
        try:
            pcm, _ = wav.wav_to_float32(data)
        except Exception:
            return TurnResult(ok=False, error="bad audio")
        level = wav.rms(pcm)
        log.info("[%s] %.1fs audio, rms %.4f", device, len(pcm) / 16000, level)
        if len(pcm) < 16000 * 0.3 or level < SILENCE_RMS:
            return TurnResult(ok=False, error="no_speech")

        tr = self.stt.transcribe(wav.normalize(pcm), self._lang_arg(lang))
        t_stt = time.perf_counter()
        if not tr.text.strip():
            return TurnResult(ok=False, error="no_speech", language=tr.language)
        log.info("[%s] heard (%s %.2f): %s", device, tr.language, tr.probability, tr.text)

        result = self.handle_text(tr.text, device, tr.language, want_voice)
        result.timings["stt"] = round((t_stt - t0) * 1000)
        return result

    def handle_text(self, text: str, device: str, lang: str, want_voice: bool) -> TurnResult:
        lang = self._lang_arg(lang) or guess_language(text)
        t0 = time.perf_counter()
        raw = self.llm.reply(device, text, lang)
        t_llm = time.perf_counter()
        reply = for_display(raw) or "…"
        log.info("[%s] reply: %s", device, reply)

        audio_id = None
        if want_voice:
            speech = self.tts.synthesize(for_speech(raw), lang)
            if speech:
                audio_id = self.cache.put(speech)
        t_tts = time.perf_counter()
        return TurnResult(
            ok=True,
            transcript=for_display(text),
            reply=reply,
            language=lang,
            audio_id=audio_id,
            timings={"llm": round((t_llm - t0) * 1000), "tts": round((t_tts - t_llm) * 1000)},
        )

    def reset(self, device: str) -> None:
        self.llm.memory.reset(device)

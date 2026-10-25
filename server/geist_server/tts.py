"""Text-to-speech with Piper (fast, offline, many languages).

Falls back to espeak-ng (if installed) for languages without a Piper voice.
"""
from __future__ import annotations

import io
import logging
import shutil
import subprocess
import threading
import wave
from pathlib import Path

from .audio import fix_wav_sizes
from .config import Config

log = logging.getLogger("geist.tts")


class PiperTTS:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._voices: dict[str, object] = {}
        self._lock = threading.Lock()
        self._espeak = shutil.which("espeak-ng")

    # ------------------------------------------------------------------ helpers
    def voice_path(self, lang: str) -> Path | None:
        name = self.cfg.voices.get(lang)
        if not name:
            return None
        path = self.cfg.voices_dir / f"{name}.onnx"
        return path if path.is_file() else None

    def available_languages(self) -> list[str]:
        langs = [lang for lang in self.cfg.voices if self.voice_path(lang)]
        return sorted(langs)

    def _voice(self, lang: str):
        if lang in self._voices:
            return self._voices[lang]
        path = self.voice_path(lang)
        if path is None:
            return None
        from piper import PiperVoice  # lazy import

        log.info("loading Piper voice %s", path.name)
        voice = PiperVoice.load(str(path))
        self._voices[lang] = voice
        return voice

    # ------------------------------------------------------------------ public
    def synthesize(self, text: str, lang: str) -> bytes | None:
        """Returns a mono 16-bit WAV, or None if no voice exists for `lang`."""
        if not text:
            return None
        with self._lock:
            try:
                voice = self._voice(lang)
            except Exception:  # piper not installed / broken model
                log.exception("could not load Piper voice for %s", lang)
                voice = None
            if voice is not None:
                return self._piper(voice, text)
        if self._espeak:
            return self._espeak_ng(text, lang)
        log.warning("no voice for language '%s' - replying with text only", lang)
        return None

    def _piper(self, voice, text: str) -> bytes:
        buf = io.BytesIO()
        with wave.open(buf, "wb") as wf:
            if hasattr(voice, "synthesize_wav"):  # piper-tts >= 1.3
                try:
                    from piper import SynthesisConfig

                    syn = SynthesisConfig(length_scale=self.cfg.tts_length_scale)
                    voice.synthesize_wav(text, wf, syn_config=syn)
                except ImportError:
                    voice.synthesize_wav(text, wf)
            else:  # piper-tts 1.2
                voice.synthesize(text, wf, length_scale=self.cfg.tts_length_scale)
        return buf.getvalue()

    def _espeak_ng(self, text: str, lang: str) -> bytes | None:
        try:
            out = subprocess.run([self._espeak, "-v", lang, "--stdout", text],
                                 capture_output=True, timeout=30, check=True)
            return fix_wav_sizes(out.stdout) if out.stdout else None
        except Exception:
            log.exception("espeak-ng failed")
            return None

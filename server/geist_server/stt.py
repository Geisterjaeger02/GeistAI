"""Speech-to-text with faster-whisper (multilingual, runs locally)."""
from __future__ import annotations

import logging
import threading
from dataclasses import dataclass

import numpy as np

from .config import Config

log = logging.getLogger("geist.stt")


@dataclass
class Transcript:
    text: str
    language: str
    probability: float


class WhisperSTT:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._model = None
        self._lock = threading.Lock()

    def _load(self):
        if self._model is None:
            from faster_whisper import WhisperModel  # heavy import, do it lazily

            log.info("loading Whisper '%s' (%s, %s)", self.cfg.whisper_model,
                     self.cfg.whisper_device, self.cfg.whisper_compute)
            self._model = WhisperModel(self.cfg.whisper_model, device=self.cfg.whisper_device,
                                       compute_type=self.cfg.whisper_compute)
        return self._model

    def warmup(self) -> None:
        self._load()

    def _run(self, model, audio: np.ndarray, language: str | None):
        segments, info = model.transcribe(audio, language=language, beam_size=5,
                                          vad_filter=True, condition_on_previous_text=False)
        text = "".join(s.text for s in segments).strip()
        return text, info

    def transcribe(self, audio: np.ndarray, language: str | None = None) -> Transcript:
        """language=None means auto-detect."""
        with self._lock:  # one model, one job at a time
            model = self._load()
            text, info = self._run(model, audio, language)
            detected = info.language
            prob = float(getattr(info, "language_probability", 0.0) or 0.0)

            allowed = self.cfg.allowed_languages
            if language is None and allowed and detected not in allowed:
                # Pick the most likely *allowed* language and transcribe again.
                probs = getattr(info, "all_language_probs", None) or []
                best = next((lang for lang, _ in probs if lang in allowed), None)
                if best:
                    log.info("detected '%s' not allowed, retrying as '%s'", detected, best)
                    text, info = self._run(model, audio, best)
                    detected, prob = best, float(dict(probs).get(best, 0.0))
        return Transcript(text=text, language=detected, probability=prob)

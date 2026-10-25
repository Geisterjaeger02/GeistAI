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
            from .gpu import cuda_devices, register_cuda_libraries

            register_cuda_libraries()  # before faster-whisper loads CTranslate2
            from faster_whisper import WhisperModel  # heavy import, do it lazily

            if self.cfg.whisper_compute in ("", "auto"):
                gpu = self.cfg.whisper_device in ("auto", "cuda") and cuda_devices() > 0
                self.cfg.whisper_compute = "float16" if gpu else "int8"
            log.info("loading Whisper '%s' (%s, %s)", self.cfg.whisper_model,
                     self.cfg.whisper_device, self.cfg.whisper_compute)
            self._model = WhisperModel(self.cfg.whisper_model, device=self.cfg.whisper_device,
                                       compute_type=self.cfg.whisper_compute)
        return self._model

    @staticmethod
    def _is_gpu_problem(e: Exception) -> bool:
        msg = str(e).lower()
        return any(k in msg for k in ("cublas", "cudnn", "cuda", "cudart", "nvrtc"))

    def _fall_back_to_cpu(self, e: Exception) -> None:
        """An NVIDIA GPU was detected but its CUDA libraries are missing - use the CPU."""
        from faster_whisper import WhisperModel

        log.warning("GPU speech recognition not available (%s). Using the CPU instead. "
                    "To use the GPU: pip install -r requirements-gpu.txt (then restart), "
                    "or set WHISPER_DEVICE=cpu in .env to skip this check.", e)
        self.cfg.whisper_device, self.cfg.whisper_compute = "cpu", "int8"
        self._model = WhisperModel(self.cfg.whisper_model, device="cpu", compute_type="int8")

    def _run_once(self, audio: np.ndarray, language: str | None, vad: bool = True):
        segments, info = self._load().transcribe(audio, language=language, beam_size=5,
                                                 vad_filter=vad,
                                                 condition_on_previous_text=False)
        text = "".join(s.text for s in segments).strip()
        return text, info

    def _run(self, audio: np.ndarray, language: str | None, vad: bool = True):
        try:
            return self._run_once(audio, language, vad)
        except (RuntimeError, OSError) as e:
            if self.cfg.whisper_device == "cpu" or not self._is_gpu_problem(e):
                raise
            self._fall_back_to_cpu(e)
            return self._run_once(audio, language, vad)

    def warmup(self) -> None:
        """Load the model and run one tiny transcription, which also checks the GPU."""
        with self._lock:
            noise = (np.random.default_rng(0).standard_normal(16000) * 0.01).astype(np.float32)
            self._run(noise, "en", vad=False)

    def transcribe(self, audio: np.ndarray, language: str | None = None) -> Transcript:
        """language=None means auto-detect."""
        with self._lock:  # one model, one job at a time
            text, info = self._run(audio, language)
            detected = info.language
            prob = float(getattr(info, "language_probability", 0.0) or 0.0)

            allowed = self.cfg.allowed_languages
            if language is None and allowed and detected not in allowed:
                # Pick the most likely *allowed* language and transcribe again.
                probs = getattr(info, "all_language_probs", None) or []
                best = next((lang for lang, _ in probs if lang in allowed), None)
                if best:
                    log.info("detected '%s' not allowed, retrying as '%s'", detected, best)
                    text, info = self._run(audio, best)
                    detected, prob = best, float(dict(probs).get(best, 0.0))
        return Transcript(text=text, language=detected, probability=prob)

"""Text-to-speech. The voice per language is chosen in the voice page
(http://<server>:8765/voices) or with VOICE_XX in .env:

* Piper (local, free)          VOICE_DE=de_DE-thorsten-high   (or piper:...)
* Microsoft (online, free)     VOICE_DE=edge:de-DE-KatjaNeural
* OpenAI (online, paid)        VOICE_DE=openai:nova

If an online voice fails (no internet, service changed), the local Piper
voice of that language speaks instead; espeak-ng is the last resort.
"""
from __future__ import annotations

import io
import logging
import shutil
import subprocess
import threading
import wave

from .audio import fix_wav_sizes, pcm16_to_wav
from .config import DEFAULT_VOICES, Config
from .voices import parse

log = logging.getLogger("geist.tts")


class TTS:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self._piper_cache: dict[str, object] = {}  # piper voice name -> loaded voice
        self._lock = threading.Lock()
        self._espeak = shutil.which("espeak-ng")
        self._downloads: dict[str, threading.Thread] = {}
        self._openai_quirks: set[str] = set()

    # ------------------------------------------------------------------ voices
    def voice_id(self, lang: str) -> str:
        return self.cfg.voices.get(lang) or DEFAULT_VOICES.get(lang, "")

    def piper_path(self, name: str):
        path = self.cfg.voices_dir / f"{name}.onnx"
        return path if path.is_file() else None

    def voice_path(self, lang: str):  # kept for older callers
        engine, name = parse(self.voice_id(lang))
        return self.piper_path(name) if engine == "piper" and name else None

    def available_languages(self) -> list[str]:
        langs = []
        for lang in self.cfg.voices:
            engine, name = parse(self.voice_id(lang))
            if engine != "piper" or self.piper_path(name):
                langs.append(lang)
        return sorted(langs)

    def warmup(self) -> None:
        """Load every installed Piper voice once (online voices need no warm-up)."""
        for lang in self.available_languages():
            if parse(self.voice_id(lang))[0] == "piper":
                self.synthesize("ok", lang)

    def ensure_piper(self, name: str, wait_s: float) -> bool:
        """Downloads a Piper voice (about 60 MB) in the background; waits up to wait_s."""
        if self.piper_path(name):
            return True
        if not self.cfg.auto_download_voices:
            return False
        thread = self._downloads.get(name)
        if thread is None or (not thread.is_alive() and not self.piper_path(name)):
            from .download_voices import download

            log.info("downloading the voice %s ...", name)
            thread = threading.Thread(target=download, args=(name, self.cfg.voices_dir),
                                      daemon=True)
            self._downloads[name] = thread
            thread.start()
        thread.join(timeout=wait_s)
        return self.piper_path(name) is not None

    # ------------------------------------------------------------------ public
    def synthesize(self, text: str, lang: str, voice: str | None = None,
                   wait_s: float = 20) -> bytes | None:
        """Returns a mono 16-bit WAV, or None if no voice could speak."""
        if not text:
            return None
        engine, name = parse(voice or self.voice_id(lang))
        try:
            if engine == "edge":
                return self._edge(text, name)
            if engine == "openai":
                return self._openai(text, name)
        except Exception as e:  # online voice failed: fall back to the local one
            log.warning("%s voice failed (%s) - using the local voice", engine, e)
            engine, name = "piper", DEFAULT_VOICES.get(lang, "")
        if engine == "piper" and name:
            if self.ensure_piper(name, wait_s):
                with self._lock:
                    try:
                        return self._piper(self._load_piper(name), text)
                    except Exception:
                        log.exception("Piper voice %s failed", name)
        if self._espeak:
            return self._espeak_ng(text, lang)
        log.warning("no voice for language '%s' - replying with text only", lang)
        return None

    # ------------------------------------------------------------------ engines
    def _load_piper(self, name: str):
        if name not in self._piper_cache:
            from piper import PiperVoice  # lazy import

            log.info("loading Piper voice %s", name)
            self._piper_cache[name] = PiperVoice.load(str(self.piper_path(name)))
        return self._piper_cache[name]

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

    def _edge(self, text: str, name: str) -> bytes:
        import asyncio

        import edge_tts

        rate = int(round((1.0 / max(self.cfg.tts_length_scale, 0.3) - 1.0) * 100))

        async def run() -> bytes:
            comm = edge_tts.Communicate(text, name, rate=f"{rate:+d}%")
            data = bytearray()
            async for chunk in comm.stream():
                if chunk.get("type") == "audio":
                    data += chunk["data"]
            return bytes(data)

        mp3 = asyncio.run(run())
        if not mp3:
            raise RuntimeError("no audio")
        return mp3_to_wav(mp3)

    def _openai(self, text: str, name: str) -> bytes:
        import httpx

        cfg = self.cfg
        if not cfg.openai_api_key:
            raise RuntimeError("OPENAI_API_KEY is not set")
        body = {"model": cfg.openai_tts_model, "voice": name, "input": text,
                "response_format": "wav"}
        speed = 1.0 / max(cfg.tts_length_scale, 0.3)
        if abs(speed - 1.0) > 0.01 and "no_speed" not in self._openai_quirks:
            body["speed"] = round(speed, 2)
        for _ in range(2):
            r = httpx.post(f"{cfg.openai_url}/v1/audio/speech", timeout=60, json=body,
                           headers={"Authorization": f"Bearer {cfg.openai_api_key}"})
            if r.status_code == 400 and "speed" in body and "speed" in r.text.lower():
                self._openai_quirks.add("no_speed")
                body.pop("speed")
                continue
            break
        if r.status_code >= 400:
            raise RuntimeError(f"OpenAI {r.status_code}: {r.text[:120]}")
        return fix_wav_sizes(r.content)

    def _espeak_ng(self, text: str, lang: str) -> bytes | None:
        try:
            out = subprocess.run([self._espeak, "-v", lang, "--stdout", text],
                                 capture_output=True, timeout=30, check=True)
            return fix_wav_sizes(out.stdout) if out.stdout else None
        except Exception:
            log.exception("espeak-ng failed")
            return None


PiperTTS = TTS  # the old name


def mp3_to_wav(mp3: bytes, rate: int = 24000) -> bytes:
    """Decodes MP3 (Microsoft voices) with PyAV, which faster-whisper already uses."""
    import av
    import numpy as np

    pcm = []
    with av.open(io.BytesIO(mp3)) as container:
        resampler = av.AudioResampler(format="s16", layout="mono", rate=rate)
        for frame in container.decode(audio=0):
            for f in resampler.resample(frame):
                pcm.append(f.to_ndarray().reshape(-1))
        for f in resampler.resample(None):
            pcm.append(f.to_ndarray().reshape(-1))
    data = np.concatenate(pcm).astype("<i2").tobytes() if pcm else b""
    return pcm16_to_wav(data, rate)

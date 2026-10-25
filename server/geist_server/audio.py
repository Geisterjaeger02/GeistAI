"""WAV helpers (stdlib + numpy only)."""
from __future__ import annotations

import io
import wave

import numpy as np

TARGET_RATE = 16000


def wav_to_float32(data: bytes) -> tuple[np.ndarray, int]:
    """Decode a PCM WAV into mono float32 at 16 kHz (what Whisper expects)."""
    with wave.open(io.BytesIO(data), "rb") as wf:
        channels = wf.getnchannels()
        width = wf.getsampwidth()
        rate = wf.getframerate()
        frames = wf.readframes(wf.getnframes())

    if width == 2:
        pcm = np.frombuffer(frames, dtype="<i2").astype(np.float32) / 32768.0
    elif width == 4:
        pcm = np.frombuffer(frames, dtype="<i4").astype(np.float32) / 2147483648.0
    elif width == 1:
        pcm = (np.frombuffer(frames, dtype=np.uint8).astype(np.float32) - 128.0) / 128.0
    else:
        raise ValueError(f"unsupported sample width: {width}")

    if channels > 1:
        pcm = pcm.reshape(-1, channels).mean(axis=1)

    if rate != TARGET_RATE and len(pcm):
        duration = len(pcm) / rate
        n_out = int(round(duration * TARGET_RATE))
        x_old = np.linspace(0.0, duration, num=len(pcm), endpoint=False)
        x_new = np.linspace(0.0, duration, num=n_out, endpoint=False)
        pcm = np.interp(x_new, x_old, pcm).astype(np.float32)
    return pcm, TARGET_RATE


def pcm16_to_wav(data: bytes, rate: int = TARGET_RATE, channels: int = 1) -> bytes:
    """Wraps raw little-endian 16-bit PCM (audio/L16 uploads) into a WAV."""
    data = data[: len(data) - (len(data) % (2 * channels))]
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(data)
    return buf.getvalue()


def parse_l16_params(content_type: str) -> tuple[int, int]:
    """'audio/L16; rate=16000; channels=1' -> (16000, 1)"""
    rate, channels = TARGET_RATE, 1
    for part in content_type.split(";")[1:]:
        key, _, value = part.strip().partition("=")
        try:
            if key.lower() == "rate":
                rate = int(value)
            elif key.lower() == "channels":
                channels = int(value)
        except ValueError:
            pass
    return rate, channels


def normalize(pcm: np.ndarray, peak: float = 0.9) -> np.ndarray:
    """Remove DC offset and bring quiet recordings up to a sensible level."""
    if not len(pcm):
        return pcm
    pcm = pcm - float(np.mean(pcm))
    m = float(np.max(np.abs(pcm)))
    if m > 1e-4:
        pcm = pcm * min(peak / m, 30.0)
    return pcm.astype(np.float32)


def rms(pcm: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(pcm)))) if len(pcm) else 0.0


def float32_to_wav(pcm: np.ndarray, rate: int) -> bytes:
    pcm16 = np.clip(pcm * 32767.0, -32768, 32767).astype("<i2")
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(pcm16.tobytes())
    return buf.getvalue()


def fix_wav_sizes(data: bytes) -> bytes:
    """Patch RIFF/data sizes of streamed WAVs (e.g. espeak-ng --stdout writes placeholders)."""
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return data
    buf = bytearray(data)
    buf[4:8] = (len(buf) - 8).to_bytes(4, "little")
    pos = 12
    while pos + 8 <= len(buf):
        cid = bytes(buf[pos:pos + 4])
        size = int.from_bytes(buf[pos + 4:pos + 8], "little")
        if cid == b"data":
            buf[pos + 4:pos + 8] = (len(buf) - pos - 8).to_bytes(4, "little")
            break
        pos += 8 + size + (size & 1)
    return bytes(buf)

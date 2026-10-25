"""Server configuration, read from environment variables (or a .env file)."""
from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path


def _load_dotenv(path: Path) -> None:
    """Tiny .env loader (KEY=VALUE per line) so python-dotenv isn't required."""
    if not path.is_file():
        return
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        value = value.strip().strip('"').strip("'")
        os.environ.setdefault(key.strip(), value)


def _env(name: str, default: str) -> str:
    return os.environ.get(name, default)


def _env_int(name: str, default: int) -> int:
    try:
        return int(os.environ.get(name, default))
    except ValueError:
        return default


def _env_float(name: str, default: float) -> float:
    try:
        return float(os.environ.get(name, default))
    except ValueError:
        return default


# Default Piper voice per language (https://huggingface.co/rhasspy/piper-voices).
DEFAULT_VOICES = {
    "de": "de_DE-thorsten-medium",
    "en": "en_US-lessac-medium",
    "es": "es_ES-davefx-medium",
    "fr": "fr_FR-siwis-medium",
    "it": "it_IT-paola-medium",
    "pt": "pt_BR-faber-medium",
    "nl": "nl_NL-mls-medium",
    "pl": "pl_PL-gosia-medium",
    "tr": "tr_TR-dfki-medium",
}


@dataclass
class Config:
    host: str = "0.0.0.0"
    port: int = 8765
    announce_mdns: bool = True

    # Speech-to-text (faster-whisper)
    whisper_model: str = "small"
    whisper_device: str = "auto"       # auto | cpu | cuda
    whisper_compute: str = "auto"      # auto (float16 on GPU, int8 on CPU) | int8 | float16
    # Languages auto-detection may pick. Restricting this avoids e.g. German
    # being mis-detected as Dutch on short phrases. Empty = any language.
    allowed_languages: list[str] = field(default_factory=list)

    # Language model
    llm_backend: str = "ollama"        # ollama | openai (LM Studio, llama.cpp, vLLM ...)
    llm_url: str = "http://localhost:11434"
    llm_model: str = "gemma3:4b"
    llm_api_key: str = ""
    llm_temperature: float = 0.6
    llm_max_tokens: int = 220
    llm_keep_alive: str = "2h"         # how long Ollama keeps the model in memory
    history_turns: int = 6
    assistant_name: str = "GeistAI"
    timezone: str = "Europe/Berlin"

    # Text-to-speech (Piper)
    voices_dir: Path = Path("voices")
    voices: dict[str, str] = field(default_factory=lambda: dict(DEFAULT_VOICES))
    tts_length_scale: float = 1.0      # >1 = slower speech
    speak_on_pc: bool = False          # also play replies on this computer's speakers

    @classmethod
    def from_env(cls) -> "Config":
        _load_dotenv(Path.cwd() / ".env")
        cfg = cls()
        cfg.host = _env("GEIST_HOST", cfg.host)
        cfg.port = _env_int("GEIST_PORT", cfg.port)
        cfg.announce_mdns = _env("GEIST_MDNS", "1") not in ("0", "false", "no")

        cfg.whisper_model = _env("WHISPER_MODEL", cfg.whisper_model)
        cfg.whisper_device = _env("WHISPER_DEVICE", cfg.whisper_device)
        cfg.whisper_compute = _env("WHISPER_COMPUTE", cfg.whisper_compute)
        langs = _env("ALLOWED_LANGUAGES", "de,en,es,fr,it,pt,nl,pl,tr")
        cfg.allowed_languages = [x.strip() for x in langs.split(",") if x.strip()]

        cfg.llm_backend = _env("LLM_BACKEND", cfg.llm_backend).lower()
        default_url = "http://localhost:11434" if cfg.llm_backend == "ollama" else "http://localhost:1234"
        cfg.llm_url = _env("LLM_URL", default_url).rstrip("/")
        cfg.llm_model = _env("LLM_MODEL", cfg.llm_model)
        cfg.llm_api_key = _env("LLM_API_KEY", cfg.llm_api_key)
        cfg.llm_temperature = _env_float("LLM_TEMPERATURE", cfg.llm_temperature)
        cfg.llm_max_tokens = _env_int("LLM_MAX_TOKENS", cfg.llm_max_tokens)
        cfg.llm_keep_alive = _env("LLM_KEEP_ALIVE", cfg.llm_keep_alive)
        cfg.history_turns = _env_int("HISTORY_TURNS", cfg.history_turns)
        cfg.assistant_name = _env("ASSISTANT_NAME", cfg.assistant_name)
        cfg.timezone = _env("GEIST_TIMEZONE", cfg.timezone)

        cfg.voices_dir = Path(_env("VOICES_DIR", str(cfg.voices_dir)))
        cfg.tts_length_scale = _env_float("TTS_LENGTH_SCALE", cfg.tts_length_scale)
        cfg.speak_on_pc = _env("SPEAK_ON_PC", "0").lower() in ("1", "true", "yes", "on")
        # Override single voices: VOICE_DE=de_DE-kerstin-low
        for key, value in os.environ.items():
            if key.startswith("VOICE_") and len(key) == 8 and value:
                cfg.voices[key[6:].lower()] = value
        return cfg

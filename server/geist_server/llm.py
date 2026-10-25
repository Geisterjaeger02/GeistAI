"""Chat with a local language model: Ollama or any OpenAI-compatible server
(LM Studio, llama.cpp server, vLLM, LocalAI, ...)."""
from __future__ import annotations

import logging
import threading
import time
from collections import deque
from datetime import datetime

from .config import Config
from .textutil import language_name

log = logging.getLogger("geist.llm")

SYSTEM_PROMPT = """You are {name}, a friendly and witty voice assistant that lives in a small \
handheld device with a tiny 1.9-inch screen and a speaker.
Rules:
- Always answer in {language}. The user is speaking {language} right now; if they switch \
language, you switch too.
- Keep it short: one to three sentences (at most about 60 words) unless the user explicitly \
asks for details.
- Plain text only: no markdown, no lists, no emojis, no code blocks. Your answer is shown on a \
tiny screen and read aloud.
- Be honest: you have no live internet access, so say so for news, weather or prices.
Current date and time: {now}."""


class ChatError(RuntimeError):
    pass


class Conversations:
    """Short per-device memory so follow-up questions work."""

    def __init__(self, max_turns: int, idle_timeout_s: int = 30 * 60):
        self.max_messages = max(0, max_turns) * 2
        self.idle_timeout_s = idle_timeout_s
        self._data: dict[str, tuple[float, deque]] = {}
        self._lock = threading.Lock()

    def history(self, device: str) -> list[dict]:
        with self._lock:
            entry = self._data.get(device)
            if not entry or time.time() - entry[0] > self.idle_timeout_s:
                return []
            return list(entry[1])

    def add(self, device: str, user: str, assistant: str) -> None:
        with self._lock:
            last, msgs = self._data.get(device, (0.0, deque(maxlen=self.max_messages or 1)))
            if time.time() - last > self.idle_timeout_s:
                msgs.clear()
            if self.max_messages:
                msgs.append({"role": "user", "content": user})
                msgs.append({"role": "assistant", "content": assistant})
            self._data[device] = (time.time(), msgs)

    def reset(self, device: str) -> None:
        with self._lock:
            self._data.pop(device, None)


class LLMClient:
    def __init__(self, cfg: Config):
        self.cfg = cfg
        self.memory = Conversations(cfg.history_turns)

    def system_prompt(self, language: str) -> str:
        try:
            from zoneinfo import ZoneInfo

            now = datetime.now(ZoneInfo(self.cfg.timezone))
        except Exception:
            now = datetime.now()
        return SYSTEM_PROMPT.format(name=self.cfg.assistant_name, language=language_name(language),
                                    now=now.strftime("%A, %Y-%m-%d %H:%M"))

    def reply(self, device: str, text: str, language: str) -> str:
        messages = [{"role": "system", "content": self.system_prompt(language)}]
        messages += self.memory.history(device)
        messages.append({"role": "user", "content": text})
        answer = self._complete(messages).strip()
        self.memory.add(device, text, answer)
        return answer

    # ------------------------------------------------------------------ backends
    def _complete(self, messages: list[dict]) -> str:
        import httpx

        cfg = self.cfg
        try:
            if cfg.llm_backend == "ollama":
                r = httpx.post(f"{cfg.llm_url}/api/chat", timeout=120, json={
                    "model": cfg.llm_model,
                    "messages": messages,
                    "stream": False,
                    "keep_alive": "30m",
                    "options": {"temperature": cfg.llm_temperature,
                                "num_predict": cfg.llm_max_tokens},
                })
                r.raise_for_status()
                return r.json()["message"]["content"]

            headers = {"Authorization": f"Bearer {cfg.llm_api_key}"} if cfg.llm_api_key else {}
            r = httpx.post(f"{cfg.llm_url}/v1/chat/completions", timeout=120, headers=headers, json={
                "model": cfg.llm_model,
                "messages": messages,
                "temperature": cfg.llm_temperature,
                "max_tokens": cfg.llm_max_tokens,
            })
            r.raise_for_status()
            return r.json()["choices"][0]["message"]["content"]
        except httpx.ConnectError as e:
            raise ChatError(f"LLM not reachable at {cfg.llm_url}") from e
        except httpx.HTTPStatusError as e:
            body = e.response.text[:200]
            raise ChatError(f"LLM error {e.response.status_code}: {body}") from e
        except (KeyError, IndexError, ValueError) as e:
            raise ChatError("LLM returned an unexpected response") from e

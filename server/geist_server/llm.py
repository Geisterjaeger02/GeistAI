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
from .tools import ROUTER_PROMPT, ROUTER_SCHEMA, ToolCall, Tools, parse_tool_call, today_string

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
{internet}
Current date and time: {now}."""

INTERNET_ON = """- You can look things up online. When the user's message contains a "Live data" block, \
it was fetched from the internet just now for this question (search results and the text of \
the best pages). Answer from it: pick the parts that answer the question, round numbers \
sensibly (whole degrees), and never invent details that are not in it. You may name the \
website you got it from. If the block says the lookup failed, say so briefly."""
INTERNET_OFF = "- Be honest: you have no live internet access, so say so for news, weather or prices."



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
    def __init__(self, cfg: Config, tools: Tools | None = None):
        self.cfg = cfg
        self.memory = Conversations(cfg.history_turns)
        if tools is None and cfg.tools_enabled:
            tools = Tools(cfg.home_location, cfg.tools_timeout_s)
        self.tools = tools

    def system_prompt(self, language: str) -> str:
        try:
            from zoneinfo import ZoneInfo

            now = datetime.now(ZoneInfo(self.cfg.timezone))
        except Exception:
            now = datetime.now()
        return SYSTEM_PROMPT.format(name=self.cfg.assistant_name, language=language_name(language),
                                    internet=INTERNET_ON if self.tools else INTERNET_OFF,
                                    now=now.strftime("%A, %Y-%m-%d %H:%M"))

    def reply(self, device: str, text: str, language: str) -> str:
        history = self.memory.history(device)
        content = text
        if self.tools:
            t0 = time.perf_counter()
            call = self.route(history, text)
            result = self.tools.run(call, language, question=text)
            if result:
                log.info("[%s] %s %s -> %d chars in %.1fs", device, call.tool,
                         call.query or call.url or call.location or "(home)", len(result.text),
                         time.perf_counter() - t0)
                content = (f"{text}\n\n[Live data, fetched just now for this question]\n"
                           f"{result.text}")
        messages = [{"role": "system", "content": self.system_prompt(language)}]
        messages += history
        messages.append({"role": "user", "content": content})
        answer = self._complete(messages).strip()
        self.memory.add(device, text, answer)  # remember the question, not the fetched data
        return answer

    def route(self, history: list[dict], text: str) -> ToolCall:
        """Asks the model (in JSON mode) whether this message needs a live lookup."""
        recent = history[-4:]
        convo = "".join(f"{m['role'].capitalize()}: {m['content'][:300]}\n" for m in recent)
        user = (f"Conversation so far:\n{convo}\n" if convo else "") + f"Last message: {text}"
        prompt = ROUTER_PROMPT.format(today=today_string(self.cfg.timezone),
                                      home=self.cfg.home_location or "unknown")
        try:
            raw = self._complete([{"role": "system", "content": prompt},
                                  {"role": "user", "content": user}],
                                 max_tokens=80, json_schema=ROUTER_SCHEMA, temperature=0.0)
        except ChatError as e:
            log.warning("tool routing failed: %s", e)
            return ToolCall()
        return parse_tool_call(raw)

    def warmup(self) -> None:
        """Load the model into memory now, so the first real question is fast."""
        self._complete([{"role": "user", "content": "Hi"}], max_tokens=1)

    # ------------------------------------------------------------------ backends
    def _complete(self, messages: list[dict], max_tokens: int | None = None,
                  json_schema: dict | None = None, temperature: float | None = None) -> str:
        import httpx

        cfg = self.cfg
        try:
            if cfg.llm_backend == "ollama":
                body = {
                    "model": cfg.llm_model,
                    "messages": messages,
                    "stream": False,
                    "keep_alive": cfg.llm_keep_alive,
                    "options": {"temperature": cfg.llm_temperature if temperature is None
                                else temperature,
                                "num_predict": max_tokens or cfg.llm_max_tokens,
                                # room for web pages; must stay the same for every call,
                                # otherwise Ollama reloads the model
                                "num_ctx": cfg.llm_context},
                }
                if json_schema:
                    body["format"] = json_schema
                    body["think"] = False  # reasoning models: just the JSON, quickly
                r = httpx.post(f"{cfg.llm_url}/api/chat", timeout=120, json=body)
                if json_schema and r.status_code == 400 and "think" in r.text:
                    body.pop("think")  # model without a thinking switch
                    r = httpx.post(f"{cfg.llm_url}/api/chat", timeout=120, json=body)
                r.raise_for_status()
                return r.json()["message"]["content"]

            headers = {"Authorization": f"Bearer {cfg.llm_api_key}"} if cfg.llm_api_key else {}
            body = {
                "model": cfg.llm_model,
                "messages": messages,
                "temperature": cfg.llm_temperature if temperature is None else temperature,
                "max_tokens": max_tokens or cfg.llm_max_tokens,
            }
            if json_schema:
                body["response_format"] = {"type": "json_schema", "json_schema": {
                    "name": "decision", "schema": json_schema}}
            r = httpx.post(f"{cfg.llm_url}/v1/chat/completions", timeout=120, headers=headers,
                           json=body)
            if json_schema and r.status_code in (400, 422):
                body.pop("response_format")  # server without structured output: plain prompt
                r = httpx.post(f"{cfg.llm_url}/v1/chat/completions", timeout=120,
                               headers=headers, json=body)
            r.raise_for_status()
            return r.json()["choices"][0]["message"]["content"]
        except httpx.ConnectError as e:
            raise ChatError(f"LLM not reachable at {cfg.llm_url}") from e
        except httpx.HTTPStatusError as e:
            body = e.response.text[:200]
            raise ChatError(f"LLM error {e.response.status_code}: {body}") from e
        except (KeyError, IndexError, ValueError) as e:
            raise ChatError("LLM returned an unexpected response") from e

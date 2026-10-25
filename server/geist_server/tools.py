"""Live information from the internet for the assistant: weather (Open-Meteo),
web and news search (DuckDuckGo via the `ddgs` package) and Wikipedia.

None of these need an API key. Each tool returns a short plain-text block of
facts which is handed to the language model, so the model can phrase the
answer in the user's language.
"""
from __future__ import annotations

import json
import logging
import re
from dataclasses import dataclass, field
from datetime import date, datetime

log = logging.getLogger("geist.tools")

USER_AGENT = "GeistAI/0.2 (+https://github.com/Geisterjaeger02/GeistAI)"
TOOL_NAMES = ("none", "weather", "web_search", "news", "wikipedia")

# --------------------------------------------------------------------------- routing
ROUTER_PROMPT = """You decide whether a voice assistant needs live data from the internet \
to answer the user's LAST message. Today is {today}. The user's home is {home}.
Tools:
- weather: current weather or forecast. "location" = the place asked about; empty if the user \
did not name one. "when" = now | today | tomorrow | week.
- news: recent news or headlines on a topic. "query" = short search words.
- web_search: anything current or factual you might not know: prices, sports results, \
opening hours, events, people in office, product releases, "who/what is ...". "query" = short \
search words in the user's language.
- wikipedia: background knowledge about a well-known person, place, thing or event. "query" = \
the article title.
- none: chit-chat, jokes, advice, maths, translations, opinions, the current date or time, \
or anything you can answer well yourself.
Earlier messages are only context for follow-ups like "and tomorrow?".
Answer with JSON only, for example {{"tool": "weather", "location": "Madrid", "when": "tomorrow", \
"query": ""}}."""

ROUTER_SCHEMA = {
    "type": "object",
    "properties": {
        "tool": {"type": "string", "enum": list(TOOL_NAMES)},
        "location": {"type": "string"},
        "when": {"type": "string", "enum": ["now", "today", "tomorrow", "week", ""]},
        "query": {"type": "string"},
    },
    "required": ["tool"],
}


@dataclass
class ToolCall:
    tool: str = "none"
    location: str = ""
    when: str = ""
    query: str = ""

    @property
    def needed(self) -> bool:
        return self.tool != "none"


def parse_tool_call(raw: str) -> ToolCall:
    """Reads the router's JSON answer. Anything unexpected means "no tool"."""
    m = re.search(r"\{.*\}", raw or "", re.DOTALL)
    if not m:
        return ToolCall()
    try:
        data = json.loads(m.group(0))
    except ValueError:
        return ToolCall()
    if not isinstance(data, dict):
        return ToolCall()
    tool = str(data.get("tool", "none")).strip().lower()
    if tool not in TOOL_NAMES:
        return ToolCall()
    call = ToolCall(
        tool=tool,
        location=str(data.get("location") or "").strip(),
        when=str(data.get("when") or "").strip().lower(),
        query=str(data.get("query") or "").strip(),
    )
    if call.tool in ("web_search", "news", "wikipedia") and not call.query:
        return ToolCall()
    return call


# --------------------------------------------------------------------------- weather
WMO_CODES = {
    0: "clear sky", 1: "mainly clear", 2: "partly cloudy", 3: "overcast",
    45: "fog", 48: "freezing fog",
    51: "light drizzle", 53: "drizzle", 55: "heavy drizzle",
    56: "light freezing drizzle", 57: "freezing drizzle",
    61: "light rain", 63: "rain", 65: "heavy rain",
    66: "light freezing rain", 67: "freezing rain",
    71: "light snow", 73: "snow", 75: "heavy snow", 77: "snow grains",
    80: "light rain showers", 81: "rain showers", 82: "violent rain showers",
    85: "light snow showers", 86: "snow showers",
    95: "thunderstorm", 96: "thunderstorm with hail", 99: "thunderstorm with heavy hail",
}


def describe_code(code) -> str:
    try:
        return WMO_CODES.get(int(code), "unknown conditions")
    except (TypeError, ValueError):
        return "unknown conditions"


def _num(v, fmt="{:.0f}") -> str:
    return "?" if v is None else fmt.format(v)


def format_weather(place: str, data: dict, when: str) -> str:
    """Turns an Open-Meteo forecast response into a few lines of facts."""
    lines = [f"Weather for {place} (source: Open-Meteo):"]
    cur = data.get("current") or {}
    if cur:
        lines.append(
            f"Now: {_num(cur.get('temperature_2m'), '{:.1f}')} °C "
            f"(feels like {_num(cur.get('apparent_temperature'))} °C), "
            f"{describe_code(cur.get('weather_code'))}, "
            f"wind {_num(cur.get('wind_speed_10m'))} km/h, "
            f"humidity {_num(cur.get('relative_humidity_2m'))} %."
        )
    daily = data.get("daily") or {}
    days = daily.get("time") or []
    count = {"now": 2, "today": 2, "tomorrow": 3, "week": 7}.get(when, 3)
    for i, day in enumerate(days[:count]):
        def at(key):
            values = daily.get(key) or []
            return values[i] if i < len(values) else None

        def d(key, fmt="{:.0f}"):
            return _num(at(key), fmt)

        try:
            label = {0: "Today", 1: "Tomorrow"}.get(i) or date.fromisoformat(day).strftime("%A")
        except ValueError:
            label = day
        line = (
            f"{label} ({day}): {d('temperature_2m_min')} to {d('temperature_2m_max')} °C, "
            f"{describe_code(at('weather_code'))}, "
            f"rain chance {d('precipitation_probability_max')} %, "
            f"precipitation {d('precipitation_sum', '{:.1f}')} mm, "
            f"wind up to {d('wind_speed_10m_max')} km/h"
        )
        if i == 0:
            rise = (daily.get("sunrise") or [""])[0][-5:]
            sset = (daily.get("sunset") or [""])[0][-5:]
            if rise and sset:
                line += f", sunrise {rise}, sunset {sset}"
        lines.append(line + ".")
    return "\n".join(lines)


# --------------------------------------------------------------------------- search
SEARCH_REGIONS = {
    "de": "de-de", "en": "us-en", "es": "es-es", "fr": "fr-fr", "it": "it-it",
    "pt": "pt-pt", "nl": "nl-nl", "pl": "pl-pl", "tr": "tr-tr",
}


def _clip(text: str, n: int) -> str:
    text = re.sub(r"\s+", " ", text or "").strip()
    return text if len(text) <= n else text[: n - 1].rstrip() + "…"


def _domain(url: str) -> str:
    m = re.match(r"https?://(?:www\.)?([^/]+)", url or "")
    return m.group(1) if m else ""


def format_search(query: str, results: list[dict], kind: str = "Web search") -> str:
    if not results:
        return f"{kind} for '{query}' found nothing."
    lines = [f"{kind} results for '{query}':"]
    for i, r in enumerate(results[:5], 1):
        title = _clip(r.get("title", ""), 120)
        body = _clip(r.get("body", ""), 280)
        src = r.get("source") or _domain(r.get("href") or r.get("url") or "")
        when = (r.get("date") or "")[:10]
        meta = ", ".join(x for x in (src, when) if x)
        lines.append(f"{i}. {title}: {body}" + (f" ({meta})" if meta else ""))
    return "\n".join(lines)


# --------------------------------------------------------------------------- runner
@dataclass
class ToolResult:
    tool: str
    text: str
    ok: bool = True
    extra: dict = field(default_factory=dict)


class Tools:
    def __init__(self, home_location: str = "", timeout_s: float = 8.0, http=None):
        self.home_location = home_location
        self.timeout_s = timeout_s
        self._http = http  # injectable for tests

    # -- http
    def _client(self):
        if self._http is None:
            import httpx

            self._http = httpx.Client(timeout=self.timeout_s, headers={"User-Agent": USER_AGENT},
                                      follow_redirects=True)
        return self._http

    def _get_json(self, url: str, params: dict) -> dict:
        r = self._client().get(url, params=params)
        r.raise_for_status()
        return r.json()

    # -- dispatch
    def run(self, call: ToolCall, language: str) -> ToolResult | None:
        if not call.needed:
            return None
        try:
            if call.tool == "weather":
                return self.weather(call.location, call.when, language)
            if call.tool == "news":
                return self.search(call.query, language, news=True)
            if call.tool == "web_search":
                return self.search(call.query, language)
            if call.tool == "wikipedia":
                return self.wikipedia(call.query, language)
        except Exception as e:  # network down, API changed ... never break the conversation
            log.warning("tool %s failed: %s", call.tool, e)
            return ToolResult(call.tool, f"The {call.tool.replace('_', ' ')} lookup failed "
                                         f"({type(e).__name__}). Tell the user you couldn't "
                                         "look it up right now.", ok=False)
        return None

    # -- weather
    def geocode(self, name: str, language: str) -> dict | None:
        candidates = [name]
        if "," in name:  # "Bordesholm, Germany" -> the API wants just the place
            candidates.append(name.split(",")[0])
        for cand in candidates:
            data = self._get_json("https://geocoding-api.open-meteo.com/v1/search",
                                  {"name": cand.strip(), "count": 1, "language": language or "en",
                                   "format": "json"})
            if data.get("results"):
                return data["results"][0]
        return None

    def weather(self, location: str, when: str, language: str) -> ToolResult:
        place_name = location or self.home_location
        if not place_name:
            return ToolResult("weather", "No location is known. Ask the user which place they "
                                         "mean (they can also set HOME_LOCATION on the server).",
                              ok=False)
        place = self.geocode(place_name, language)
        if not place:
            return ToolResult("weather", f"The place '{place_name}' was not found. Ask the user "
                                         "to say it differently.", ok=False)
        label = ", ".join(x for x in (place.get("name"), place.get("admin1"), place.get("country"))
                          if x)
        data = self._get_json("https://api.open-meteo.com/v1/forecast", {
            "latitude": place["latitude"],
            "longitude": place["longitude"],
            "current": "temperature_2m,apparent_temperature,relative_humidity_2m,"
                       "weather_code,wind_speed_10m",
            "daily": "weather_code,temperature_2m_max,temperature_2m_min,"
                     "precipitation_probability_max,precipitation_sum,wind_speed_10m_max,"
                     "sunrise,sunset",
            "timezone": "auto",
            "forecast_days": 7,
        })
        return ToolResult("weather", format_weather(label, data, when), extra={"place": label})

    # -- search
    def search(self, query: str, language: str, news: bool = False) -> ToolResult:
        kind = "News search" if news else "Web search"
        try:
            from ddgs import DDGS
        except ImportError:
            DDGS = None
        if DDGS is not None:
            region = SEARCH_REGIONS.get(language, "wt-wt")
            ddg = DDGS()
            if news:
                results = ddg.news(query, region=region, max_results=5, timelimit="w")
            else:
                results = ddg.text(query, region=region, max_results=5)
            if results or news:
                return ToolResult("news" if news else "web_search",
                                  format_search(query, results or [], kind))
        else:
            log.info("ddgs not installed (pip install ddgs) - using Wikipedia instead")
        return self.wikipedia(query, language)

    # -- wikipedia
    def wikipedia(self, query: str, language: str) -> ToolResult:
        lang = language if re.fullmatch(r"[a-z]{2,3}", language or "") else "en"
        base = f"https://{lang}.wikipedia.org"
        found = self._get_json(f"{base}/w/rest.php/v1/search/page", {"q": query, "limit": 1})
        pages = found.get("pages") or []
        if not pages:
            return ToolResult("wikipedia", f"Wikipedia has no article about '{query}'.", ok=False)
        key = pages[0].get("key") or pages[0].get("title", "")
        from urllib.parse import quote

        summary = self._get_json(f"{base}/api/rest_v1/page/summary/{quote(key, safe='')}", {})
        text = _clip(summary.get("extract", ""), 1200)
        title = summary.get("title") or pages[0].get("title", query)
        return ToolResult("wikipedia", f"Wikipedia ({lang}) article '{title}': {text}")


def today_string(tz: str) -> str:
    try:
        from zoneinfo import ZoneInfo

        now = datetime.now(ZoneInfo(tz))
    except Exception:
        now = datetime.now()
    return now.strftime("%A, %Y-%m-%d")

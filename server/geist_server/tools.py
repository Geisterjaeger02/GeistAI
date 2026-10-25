"""Live information from the internet for the assistant.

* weather   - Open-Meteo forecast
* web       - searches the web (DuckDuckGo, Brave, Startpage, Yahoo, Mojeek via
              the `ddgs` package), then opens the best pages and reads them
* news      - latest news on a topic, plus the top articles
* open_url  - reads a page the user names ("what's on tagesschau.de?")

None of these need an API key. Each tool returns a plain-text block of facts
which is handed to the language model, so the model can phrase the answer in
the user's language.
"""
from __future__ import annotations

import json
import logging
import re
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import date, datetime

log = logging.getLogger("geist.tools")

USER_AGENT = "GeistAI/0.2 (+https://github.com/Geisterjaeger02/GeistAI)"
TOOL_NAMES = ("none", "weather", "web", "news", "open_url")
# older / alternative names the model might use
TOOL_ALIASES = {"web_search": "web", "search": "web", "wikipedia": "web", "url": "open_url",
                "browse": "open_url", "website": "open_url"}
# pages are fetched like a normal browser would, many sites block unknown clients
BROWSER_UA = ("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
              "Chrome/140.0.0.0 Safari/537.36")

# --------------------------------------------------------------------------- routing
ROUTER_PROMPT = """You decide whether a voice assistant must use the internet to answer the \
user's LAST message. Today is {today}. The user's home is {home}.
Tools:
- weather: weather now or a forecast. "location" = the place asked about, empty if the user \
named none. "when" = now | today | tomorrow | week.
- news: the latest news or headlines about a topic. "query" = search words.
- web: search the internet and read the best pages. Use it for anything factual that may be \
recent or that you are not completely sure about: prices, results, tables and rankings, \
schedules, opening hours, events, products, people, companies, places, recipes, how-tos, \
"who / what / when / where is ...". "query" = good search words in the user's language.
- open_url: the user names a specific website or link. "url" = that address.
- none: chit-chat, jokes, opinions, advice, maths, translations, the current date or time.
When in doubt, choose web.
Earlier messages are only context for follow-ups like "and tomorrow?" or "tell me more".
Answer with JSON only, for example {{"tool": "web", "query": "Bundesliga Tabelle aktuell", \
"location": "", "when": "", "url": ""}}."""

ROUTER_SCHEMA = {
    "type": "object",
    "properties": {
        "tool": {"type": "string", "enum": list(TOOL_NAMES)},
        "location": {"type": "string"},
        "when": {"type": "string", "enum": ["now", "today", "tomorrow", "week", ""]},
        "query": {"type": "string"},
        "url": {"type": "string"},
    },
    "required": ["tool"],
}


@dataclass
class ToolCall:
    tool: str = "none"
    location: str = ""
    when: str = ""
    query: str = ""
    url: str = ""

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
    tool = TOOL_ALIASES.get(tool, tool)
    if tool not in TOOL_NAMES:
        return ToolCall()
    call = ToolCall(
        tool=tool,
        location=str(data.get("location") or "").strip(),
        when=str(data.get("when") or "").strip().lower(),
        query=str(data.get("query") or "").strip(),
        url=str(data.get("url") or "").strip(),
    )
    if call.tool == "open_url" and not call.url:
        if re.search(r"\w\.\w{2,}", call.query):  # the model put the address into "query"
            call.url = call.query
        else:
            call.tool = "web"
    if call.tool in ("web", "news") and not call.query:
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


# --------------------------------------------------------------------------- reading pages
_BLOCK_TAGS = ("h1", "h2", "h3", "h4", "p", "li", "tr", "dt", "dd", "pre", "blockquote",
               "figcaption", "td")
_DROP_XPATH = ("//script|//style|//noscript|//nav|//header|//footer|//aside|//form|//svg|"
               "//iframe|//button|//select|//template|//*[@aria-hidden='true']|"
               "//*[contains(@class,'cookie')]|//*[contains(@id,'cookie')]|"
               "//*[contains(@class,'consent')]")


def _norm(text: str) -> str:
    return re.sub(r"\s+", " ", text or "").strip()


def html_to_blocks(html: str) -> tuple[str, list[str]]:
    """Readable text blocks (headings, paragraphs, list items, table rows) of a page."""
    try:
        import lxml.html
    except ImportError:  # crude fallback
        title = _norm(re.sub(r"<[^>]+>", " ", (re.search(r"<title[^>]*>(.*?)</title>", html,
                                                         re.S | re.I) or [None, ""])[1]))
        text = re.sub(r"(?is)<(script|style|nav|header|footer)[^>]*>.*?</\1>", " ", html)
        text = re.sub(r"(?i)<br\s*/?>|</p>|</li>|</tr>|</h\d>|</div>", "\n", text)
        text = re.sub(r"<[^>]+>", " ", text)
        return title, [b for b in (_norm(x) for x in text.split("\n")) if b]

    try:
        doc = lxml.html.fromstring(html)
    except Exception:
        return "", []
    title = _norm(" ".join(doc.xpath("//title//text()")))
    for el in doc.xpath(_DROP_XPATH):
        if el.getparent() is not None:
            el.drop_tree()
    roots = doc.xpath("//article") or doc.xpath("//main") or doc.xpath("//body") or [doc]
    root = max(roots, key=lambda r: len(r.text_content()))
    blocks, taken = [], set()
    for el in root.iter(*_BLOCK_TAGS):
        if any(a in taken for a in el.iterancestors()):
            continue  # already part of a bigger block
        if el.tag == "tr":
            cells = [_norm(c.text_content()) for c in el.xpath("./th|./td")]
            text = " | ".join(c for c in cells if c)
        else:
            text = _norm(el.text_content())
        if text:
            taken.add(el)
            blocks.append(("# " + text) if el.tag in ("h1", "h2", "h3") else text)
    if sum(len(b) for b in blocks) < 300:  # pages built from plain <div>s
        blocks = [b for b in (_norm(x) for x in root.text_content().split("\n")) if b]
    return title, blocks


def _terms(text: str) -> set[str]:
    return {w for w in re.findall(r"\w{3,}", (text or "").lower())}


def extract_relevant(html: str, question: str, budget: int = 1800) -> str:
    """The parts of a page that matter for the question, in page order, within budget."""
    title, blocks = html_to_blocks(html)
    blocks = [b for b in blocks if len(b) >= 25 or " | " in b or b.startswith("# ")]
    if not blocks:
        return ""
    terms = _terms(question)
    scored = []
    for i, b in enumerate(blocks):
        overlap = len(terms & _terms(b))
        bonus = 1.0 if i < 3 else 0.0                      # the lead usually summarises
        bonus += 0.6 if " | " in b and overlap else 0.0    # table rows with a match
        bonus += 0.3 if re.search(r"\d", b) else 0.0       # numbers, dates, prices
        scored.append((overlap + bonus, i))
    chosen, used = set(), 0
    for score, i in sorted(scored, key=lambda x: (-x[0], x[1])):
        text = blocks[i][:600]
        if used + len(text) > budget:
            continue
        chosen.add(i)
        used += len(text) + 1
        if used > budget * 0.95:
            break
    # neighbouring table rows give context (header, positions around a match)
    for i in sorted(chosen):
        for j in (i - 1, i + 1):
            if 0 <= j < len(blocks) and j not in chosen and " | " in blocks[j] and \
                    used + len(blocks[j]) < budget * 1.2:
                chosen.add(j)
                used += len(blocks[j]) + 1
    body = "\n".join(blocks[i][:600] for i in sorted(chosen))
    return (f"{title}\n{body}" if title else body).strip()


# --------------------------------------------------------------------------- runner
@dataclass
class ToolResult:
    tool: str
    text: str
    ok: bool = True
    extra: dict = field(default_factory=dict)


class Tools:
    # Search engines used for "web". The ddgs default ("auto") puts Wikipedia and
    # Grokipedia first, which makes nearly every answer an encyclopedia answer.
    WEB_BACKENDS = "duckduckgo,brave,startpage,yahoo,mojeek"

    def __init__(self, home_location: str = "", timeout_s: float = 8.0, http=None,
                 search=None, read_pages: int = 3):
        self.home_location = home_location
        self.timeout_s = timeout_s
        self.read_pages = read_pages
        self._http = http  # injectable for tests
        self._search = search  # injectable: fn(kind, query, region) -> list[dict]

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
    def run(self, call: ToolCall, language: str, question: str = "") -> ToolResult | None:
        if not call.needed:
            return None
        try:
            if call.tool == "weather":
                return self.weather(call.location, call.when, language)
            if call.tool == "news":
                return self.web(call.query, language, question, news=True)
            if call.tool == "web":
                return self.web(call.query, language, question)
            if call.tool == "open_url":
                return self.open_url(call.url, language, question)
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

    # -- web
    def _ddgs(self, kind: str, query: str, region: str) -> list[dict]:
        if self._search:
            return self._search(kind, query, region)
        try:
            from ddgs import DDGS
        except ImportError:
            log.warning("web search needs the ddgs package: pip install -r requirements.txt")
            return []
        ddg = DDGS(timeout=int(self.timeout_s))
        if kind == "news":
            return ddg.news(query, region=region, max_results=6, timelimit="w") or []
        try:
            return ddg.text(query, region=region, max_results=8, backend=self.WEB_BACKENDS) or []
        except Exception as e:  # engines change often - fall back to whatever works
            log.info("search engines failed (%s), trying ddgs auto", e)
            return ddg.text(query, region=region, max_results=8) or []

    def fetch_page(self, url: str, language: str) -> str:
        r = self._client().get(url, headers={
            "User-Agent": BROWSER_UA,
            "Accept": "text/html,application/xhtml+xml",
            "Accept-Language": f"{language or 'en'},en;q=0.7",
        })
        r.raise_for_status()
        ctype = r.headers.get("content-type", "text/html")
        if "html" not in ctype and "text" not in ctype:
            return ""
        return r.text[:2_000_000]

    def _read(self, urls: list[str], language: str, question: str, budget: int) -> list[tuple]:
        def one(url):
            try:
                text = extract_relevant(self.fetch_page(url, language), question, budget)
                return url, text
            except Exception as e:
                log.info("could not read %s: %s", url, e)
                return url, ""

        with ThreadPoolExecutor(max_workers=max(1, len(urls))) as pool:
            return [(u, t) for u, t in pool.map(one, urls) if len(t) > 80]

    def web(self, query: str, language: str, question: str = "", news: bool = False) -> ToolResult:
        tool = "news" if news else "web"
        region = SEARCH_REGIONS.get(language, "wt-wt")
        results = self._ddgs("news" if news else "text", query, region)
        if not results:
            if news:
                return ToolResult(tool, f"The news search for '{query}' found nothing.", ok=False)
            return self.wikipedia(query, language)
        urls = []
        for r in results:
            url = r.get("href") or r.get("url") or ""
            if url.startswith("http") and not re.search(r"\.(pdf|jpe?g|png|mp4)(\?|$)|youtube\.com|"
                                                        r"youtu\.be", url, re.I):
                if _domain(url) not in {_domain(u) for u in urls}:  # different sites
                    urls.append(url)
            if len(urls) >= self.read_pages:
                break
        pages = self._read(urls, language, f"{question} {query}", budget=1800)
        parts = [format_search(query, results, "News search" if news else "Web search")]
        for url, text in pages:
            parts.append(f"--- Page {_domain(url)} ({url}):\n{text}")
        log.info("web: %d results, read %d of %d pages", len(results), len(pages), len(urls))
        return ToolResult(tool, "\n\n".join(parts), extra={"pages": [u for u, _ in pages]})

    def open_url(self, url: str, language: str, question: str = "") -> ToolResult:
        url = url.strip().strip("<>\"'")
        if not re.match(r"https?://", url):
            url = "https://" + url
        text = extract_relevant(self.fetch_page(url, language), question, budget=3500)
        if not text:
            return ToolResult("open_url", f"The page {url} could not be read.", ok=False)
        return ToolResult("open_url", f"--- Page {_domain(url)} ({url}):\n{text}")

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

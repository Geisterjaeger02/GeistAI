"""Internet tools and the routing step, with fake HTTP and a fake model.

    cd server && python -m unittest discover -s tests -v
"""
import json
import unittest

from geist_server.config import Config
from geist_server.llm import LLMClient
from geist_server.tools import (ToolCall, Tools, describe_code, extract_relevant, format_search,
                                format_weather, html_to_blocks, parse_tool_call)

FORECAST = {
    "current": {"temperature_2m": 9.34, "apparent_temperature": 6.8, "relative_humidity_2m": 87,
                "weather_code": 3, "wind_speed_10m": 14.2},
    "daily": {
        "time": ["2026-10-25", "2026-10-26", "2026-10-27"],
        "weather_code": [61, 2, 95],
        "temperature_2m_max": [11.2, 12.8, 10.1],
        "temperature_2m_min": [6.1, 5.4, 7.0],
        "precipitation_probability_max": [60, 10, 80],
        "precipitation_sum": [1.2, 0.0, 6.5],
        "wind_speed_10m_max": [22, 15, 40],
        "sunrise": ["2026-10-25T07:12", "2026-10-26T07:14", "2026-10-27T07:16"],
        "sunset": ["2026-10-25T16:58", "2026-10-26T16:56", "2026-10-27T16:54"],
    },
}


class FakeResponse:
    def __init__(self, data):
        self.data = data

    def raise_for_status(self):
        pass

    def json(self):
        return self.data


class FakeHttp:
    def __init__(self, routes):
        self.routes, self.calls = routes, []

    def get(self, url, params=None):
        self.calls.append((url, params))
        for prefix, data in self.routes.items():
            if url.startswith(prefix):
                return FakeResponse(data(params) if callable(data) else data)
        raise AssertionError("unexpected url " + url)


def weather_http(found=True):
    return FakeHttp({
        "https://geocoding-api.open-meteo.com": {"results": [
            {"name": "Bordesholm", "admin1": "Schleswig-Holstein", "country": "Deutschland",
             "latitude": 54.18, "longitude": 10.03}]} if found else {},
        "https://api.open-meteo.com": FORECAST,
    })


class ParseTest(unittest.TestCase):
    def test_valid(self):
        c = parse_tool_call('{"tool": "weather", "location": "Madrid", "when": "tomorrow"}')
        self.assertEqual((c.tool, c.location, c.when), ("weather", "Madrid", "tomorrow"))

    def test_json_inside_text(self):
        c = parse_tool_call('Sure! {"tool":"news","query":"Bundesliga"} hope that helps')
        self.assertEqual((c.tool, c.query), ("news", "Bundesliga"))

    def test_old_names_and_url_in_query(self):
        self.assertEqual(parse_tool_call('{"tool": "wikipedia", "query": "Kiel"}').tool, "web")
        c = parse_tool_call('{"tool": "open_url", "query": "tagesschau.de"}')
        self.assertEqual((c.tool, c.url), ("open_url", "tagesschau.de"))
        c = parse_tool_call('{"tool": "open_url", "query": "die Tagesschau"}')
        self.assertEqual(c.tool, "web")

    def test_garbage_means_no_tool(self):
        for raw in ("", "no", "{broken", '{"tool": "rm -rf"}', "[1, 2]",
                    '{"tool": "web_search", "query": ""}'):
            self.assertFalse(parse_tool_call(raw).needed, raw)


class WeatherTest(unittest.TestCase):
    def test_format(self):
        text = format_weather("Bordesholm, Deutschland", FORECAST, "tomorrow")
        self.assertIn("Now: 9.3 °C", text)
        self.assertIn("overcast", text)
        self.assertIn("Today (2026-10-25): 6 to 11 °C, light rain, rain chance 60 %", text)
        self.assertIn("Tomorrow (2026-10-26)", text)
        self.assertIn("Tuesday (2026-10-27)", text)
        self.assertIn("sunset 16:58", text)

    def test_short_lists_dont_crash(self):
        data = {"daily": {"time": ["2026-10-25", "2026-10-26"], "weather_code": [1]}}
        text = format_weather("X", data, "week")
        self.assertIn("Tomorrow (2026-10-26): ? to ? °C, unknown conditions", text)

    def test_codes(self):
        self.assertEqual(describe_code(95), "thunderstorm")
        self.assertEqual(describe_code(None), "unknown conditions")

    def test_home_location_used(self):
        http = weather_http()
        tools = Tools("Bordesholm", http=http)
        res = tools.run(ToolCall("weather", "", "now"), "de")
        self.assertTrue(res.ok)
        self.assertEqual(http.calls[0][1]["name"], "Bordesholm")
        self.assertEqual(http.calls[0][1]["language"], "de")
        self.assertIn("Bordesholm, Schleswig-Holstein, Deutschland", res.text)

    def test_no_location_known(self):
        res = Tools("", http=weather_http()).run(ToolCall("weather"), "en")
        self.assertFalse(res.ok)
        self.assertIn("Ask the user", res.text)

    def test_unknown_place(self):
        res = Tools("", http=weather_http(found=False)).run(ToolCall("weather", "Xyzzy"), "en")
        self.assertFalse(res.ok)
        self.assertIn("not found", res.text)

    def test_network_error_is_reported_not_raised(self):
        class Broken:
            def get(self, *a, **k):
                raise OSError("offline")

        res = Tools("Kiel", http=Broken()).run(ToolCall("weather"), "de")
        self.assertFalse(res.ok)
        self.assertIn("couldn't look it up", res.text)


class SearchTest(unittest.TestCase):
    def test_format_search(self):
        text = format_search("Bitcoin Kurs", [
            {"title": "Bitcoin  Kurs aktuell", "body": "BTC steht bei 61.000 Euro " * 30,
             "href": "https://www.finanzen.net/x"},
            {"title": "News", "body": "Bitcoin steigt", "url": "https://n.de", "source": "NDR",
             "date": "2026-10-25T08:00:00"},
        ])
        self.assertIn("1. Bitcoin Kurs aktuell: BTC steht", text)
        self.assertIn("(finanzen.net)", text)
        self.assertIn("(NDR, 2026-10-25)", text)
        self.assertLess(len(text), 900)

    def test_wikipedia(self):
        http = FakeHttp({
            "https://de.wikipedia.org/w/rest.php": {"pages": [{"key": "Kiel", "title": "Kiel"}]},
            "https://de.wikipedia.org/api/rest_v1/page/summary/Kiel":
                {"title": "Kiel", "extract": "Kiel ist die Landeshauptstadt von Schleswig-Holstein."},
        })
        res = Tools(http=http).wikipedia("Kiel", "de")
        self.assertIn("Landeshauptstadt", res.text)


TABLE_PAGE = """<html><head><title>Bundesliga Tabelle - 7. Spieltag</title>
<script>var x = 'Bayern 99 Punkte';</script></head><body>
<nav><a>Startseite</a><a>Fußball</a><a>Tabelle</a></nav>
<div class="cookie-banner">Wir verwenden Cookies, um Ihnen das beste Erlebnis zu bieten.</div>
<main><h1>Bundesliga Tabelle</h1>
<p>Die aktuelle Tabelle der Bundesliga nach dem 7. Spieltag der Saison 2026/27.</p>
<table><tr><th>Platz</th><th>Verein</th><th>Spiele</th><th>Punkte</th></tr>
<tr><td>1</td><td>Bayern München</td><td>7</td><td>19</td></tr>
<tr><td>2</td><td>Borussia Dortmund</td><td>7</td><td>16</td></tr>
<tr><td>3</td><td>Holstein Kiel</td><td>7</td><td>15</td></tr>
<tr><td>4</td><td>RB Leipzig</td><td>7</td><td>13</td></tr></table>
<ul><li><p>Nächster Spieltag: Freitag, 31. Oktober, 20:30 Uhr</p></li></ul>
""" + "<p>" + "Werbung und weitere Artikel ohne Bezug. " * 80 + "</p>" + """
</main><footer>Impressum Datenschutz</footer></body></html>"""


class PageReadingTest(unittest.TestCase):
    def test_blocks_drop_clutter_and_keep_tables(self):
        title, blocks = html_to_blocks(TABLE_PAGE)
        self.assertEqual(title, "Bundesliga Tabelle - 7. Spieltag")
        joined = "\n".join(blocks)
        self.assertIn("3 | Holstein Kiel | 7 | 15", joined)
        self.assertNotIn("Cookies", joined)
        self.assertNotIn("Startseite", joined)
        self.assertNotIn("99 Punkte", joined)
        self.assertNotIn("Impressum", joined)
        self.assertEqual(joined.count("Nächster Spieltag"), 1)  # <li><p> not twice

    def test_relevant_parts_within_budget(self):
        text = extract_relevant(TABLE_PAGE, "Auf welchem Platz steht Holstein Kiel?", budget=500)
        self.assertIn("Holstein Kiel | 7 | 15", text)
        self.assertIn("Borussia Dortmund", text)  # neighbouring row for context
        self.assertLess(len(text), 800)

    def test_web_searches_then_reads_pages(self):
        class PageHttp:
            def __init__(self):
                self.urls = []

            def get(self, url, params=None, headers=None):
                self.urls.append(url)
                resp = FakeResponse(None)
                resp.text = TABLE_PAGE if "kicker" in url else "<html><body><p>x</p></body></html>"
                resp.headers = {"content-type": "text/html; charset=utf-8"}
                return resp

        pages = PageHttp()
        searched = []

        def fake_search(kind, query, region):
            searched.append((kind, query, region))
            return [
                {"title": "Tabelle", "href": "https://www.kicker.de/bundesliga/tabelle",
                 "body": "Die Tabelle der Bundesliga"},
                {"title": "Noch mal kicker", "href": "https://www.kicker.de/other", "body": "..."},
                {"title": "Video", "href": "https://www.youtube.com/watch?v=1", "body": "..."},
                {"title": "Sportschau", "href": "https://www.sportschau.de/tabelle", "body": "..."},
            ]

        tools = Tools(http=pages, search=fake_search)
        res = tools.run(ToolCall("web", query="Bundesliga Tabelle"), "de",
                        question="Wo steht Holstein Kiel?")
        self.assertEqual(searched, [("text", "Bundesliga Tabelle", "de-de")])
        # one page per site, no videos
        self.assertEqual(pages.urls, ["https://www.kicker.de/bundesliga/tabelle",
                                      "https://www.sportschau.de/tabelle"])
        self.assertIn("1. Tabelle: Die Tabelle der Bundesliga (kicker.de)", res.text)
        self.assertIn("--- Page kicker.de", res.text)
        self.assertIn("Holstein Kiel | 7 | 15", res.text)
        self.assertNotIn("sportschau.de (https", res.text)  # unreadable page left out

    def test_open_url_adds_https(self):
        class PageHttp:
            url = None

            def get(self, url, params=None, headers=None):
                PageHttp.url = url
                resp = FakeResponse(None)
                resp.text, resp.headers = TABLE_PAGE, {"content-type": "text/html"}
                return resp

        res = Tools(http=PageHttp()).run(ToolCall("open_url", url="kicker.de"), "de", "Tabelle?")
        self.assertEqual(PageHttp.url, "https://kicker.de")
        self.assertIn("Bayern München", res.text)


class FakeModelLLM(LLMClient):
    """LLMClient with the HTTP call to the model replaced."""

    def __init__(self, router_answer, tools):
        cfg = Config()
        cfg.home_location = "Bordesholm"
        super().__init__(cfg, tools=tools)
        self.router_answer = router_answer
        self.calls = []

    def _complete(self, messages, max_tokens=None, json_schema=None, temperature=None):
        self.calls.append((messages, json_schema))
        if json_schema:
            return self.router_answer
        return "Morgen wird es bis zu 13 Grad warm."


class RoutingTest(unittest.TestCase):
    def test_weather_question_gets_live_data(self):
        llm = FakeModelLLM(json.dumps({"tool": "weather", "location": "", "when": "tomorrow"}),
                           Tools("Bordesholm", http=weather_http()))
        answer = llm.reply("dev1", "Wie wird das Wetter morgen?", "de")
        self.assertEqual(answer, "Morgen wird es bis zu 13 Grad warm.")
        final_messages = llm.calls[-1][0]
        self.assertIn("Live data", final_messages[-1]["content"])
        self.assertIn("5 to 13 °C", final_messages[-1]["content"])
        self.assertIn("look things up online", final_messages[0]["content"])
        # memory keeps the plain question, not the fetched data
        self.assertEqual(llm.memory.history("dev1")[0]["content"], "Wie wird das Wetter morgen?")

    def test_chit_chat_skips_tools(self):
        http = weather_http()
        llm = FakeModelLLM('{"tool": "none"}', Tools("Bordesholm", http=http))
        llm.reply("dev1", "Erzähl mir einen Witz", "de")
        self.assertEqual(http.calls, [])
        self.assertNotIn("Live data", llm.calls[-1][0][-1]["content"])

    def test_follow_up_sees_history(self):
        llm = FakeModelLLM('{"tool": "none"}', Tools(http=weather_http()))
        llm.memory.add("dev1", "Wetter in Madrid?", "Sonnig, 24 Grad.")
        llm.reply("dev1", "Und morgen?", "de")
        router_user = llm.calls[0][0][1]["content"]
        self.assertIn("Wetter in Madrid?", router_user)
        self.assertIn("Last message: Und morgen?", router_user)

    def test_tools_disabled(self):
        cfg = Config()
        cfg.tools_enabled = False
        llm = LLMClient(cfg)
        self.assertIsNone(llm.tools)
        self.assertIn("no live internet", llm.system_prompt("de"))


class DotenvTest(unittest.TestCase):
    def test_inline_comments(self):
        import os
        import tempfile
        from pathlib import Path

        from geist_server.config import _load_dotenv

        path = Path(tempfile.mkdtemp()) / ".env"
        path.write_text('GT_A=small     # tiny | base\nGT_B=   # nothing\nGT_C="x # y"\n'
                        'GT_D=Bad Segeberg\nGT_E=pa#ss\n', encoding="utf-8")
        for k in ("GT_A", "GT_B", "GT_C", "GT_D", "GT_E"):
            os.environ.pop(k, None)
        _load_dotenv(path)
        self.assertEqual([os.environ[k] for k in ("GT_A", "GT_B", "GT_C", "GT_D", "GT_E")],
                         ["small", "", "x # y", "Bad Segeberg", "pa#ss"])


if __name__ == "__main__":
    unittest.main()

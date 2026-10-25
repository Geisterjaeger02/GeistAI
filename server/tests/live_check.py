"""Calls the real internet services once (run in CI; needs network).

    cd server && python -m tests.live_check
"""
import os

from geist_server.tools import ToolCall, Tools


def main() -> None:
    tools = Tools("Bordesholm")
    for call, lang in [
        (ToolCall("weather", "", "tomorrow"), "de"),
        (ToolCall("weather", "Madrid", "now"), "es"),
        (ToolCall("wikipedia", query="Kiel"), "de"),
        (ToolCall("web_search", query="Bundesliga Tabelle"), "de"),
        (ToolCall("news", query="space"), "en"),
    ]:
        res = tools.run(call, lang)
        print(f"--- {call.tool} ({'ok' if res.ok else 'FAILED'})\n{res.text}\n")
        if os.environ.get("GITHUB_ACTIONS"):  # readable as an annotation on the run page
            body = res.text[:900].replace("%", "%25").replace("\n", "%0A")
            print(f"::notice title={call.tool} {lang} {'ok' if res.ok else 'FAILED'}::{body}")


if __name__ == "__main__":
    main()

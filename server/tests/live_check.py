"""Calls the real internet services once (run in CI; needs network).

    cd server && python -m tests.live_check
"""
import os

from geist_server.tools import ToolCall, Tools


def main() -> None:
    tools = Tools("Bordesholm")
    for call, lang, question in [
        (ToolCall("weather", "", "tomorrow"), "de", "Wie wird das Wetter morgen?"),
        (ToolCall("web", query="Bundesliga Tabelle aktuell"), "de",
         "Auf welchem Platz steht Holstein Kiel?"),
        (ToolCall("web", query="Bitcoin Kurs Euro"), "de", "Was kostet ein Bitcoin?"),
        (ToolCall("news", query="Schleswig-Holstein"), "de", "Was gibt es Neues in Schleswig-Holstein?"),
        (ToolCall("open_url", url="tagesschau.de"), "de", "Was sind die Schlagzeilen?"),
    ]:
        res = tools.run(call, lang, question)
        print(f"--- {call.tool} ({'ok' if res.ok else 'FAILED'})\n{res.text}\n")
        if os.environ.get("GITHUB_ACTIONS"):  # readable as an annotation on the run page
            body = res.text[:3000].replace("%", "%25").replace("\n", "%0A")
            print(f"::notice title={call.tool} {lang} {'ok' if res.ok else 'FAILED'}::{body}")


if __name__ == "__main__":
    main()

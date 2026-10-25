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
        if tools.last_search_errors:
            res.text += "\nsearch errors: " + " / ".join(tools.last_search_errors)
            tools.last_search_errors = []
        print(f"--- {call.tool} ({'ok' if res.ok else 'FAILED'})\n{res.text}\n")
        if os.environ.get("GITHUB_ACTIONS"):  # readable as an annotation on the run page
            body = res.text[:3000].replace("%", "%25").replace("\n", "%0A")
            print(f"::notice title={call.tool} {lang} {'ok' if res.ok else 'FAILED'}::{body}")


def check_voices() -> None:
    """Every default voice must exist on Hugging Face; list alternatives if not."""
    import httpx

    from geist_server.config import DEFAULT_VOICES
    from geist_server.download_voices import voice_url

    lines, http = [], httpx.Client(timeout=20, follow_redirects=True)
    for lang, name in DEFAULT_VOICES.items():
        r = http.head(voice_url(name, ".onnx.json"))
        if r.status_code == 200:
            lines.append(f"{lang}: {name} ok")
            continue
        locale = name.split("-")[0]
        tree = http.get(f"https://huggingface.co/api/models/rhasspy/piper-voices/tree/main/"
                        f"{locale.split('_')[0]}/{locale}")
        options = []
        if tree.status_code == 200:
            for speaker in tree.json():
                sub = http.get("https://huggingface.co/api/models/rhasspy/piper-voices/tree/main/"
                               + speaker["path"])
                options += [x["path"].split("/")[-1] for x in sub.json()] if sub.ok else []
        lines.append(f"{lang}: {name} MISSING (HTTP {r.status_code}); available: "
                     f"{', '.join(options) or 'none for ' + locale}")
    text = "\n".join(lines)
    print(text)
    if os.environ.get("GITHUB_ACTIONS"):
        print("::notice title=voices::" + text.replace("%", "%25").replace("\n", "%0A"))


if __name__ == "__main__":
    check_voices()
    main()

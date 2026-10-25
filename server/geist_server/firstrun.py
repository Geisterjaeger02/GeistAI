"""First-time setup questions (run by setup.bat / setup.sh, safe to run again).

    python -m geist_server.firstrun                 # asks
    python -m geist_server.firstrun --yes --home Bordesholm --voices de,en

Writes server/.env, downloads the voices and checks Ollama.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent  # the server folder


# --------------------------------------------------------------------------- .env
def read_env(path: Path) -> dict[str, str]:
    values = {}
    if path.is_file():
        for line in path.read_text(encoding="utf-8").splitlines():
            m = re.match(r"\s*([A-Z0-9_]+)\s*=(.*)", line)
            if m:
                v = m.group(2).strip()
                v = "" if v.startswith("#") else re.split(r"\s+#", v, maxsplit=1)[0].strip()
                values[m.group(1)] = v.strip('"').strip("'")
    return values


def set_env(path: Path, key: str, value: str) -> None:
    """Sets KEY=value in a .env file: replaces the existing (or commented-out
    example) line instead of adding a duplicate."""
    lines = path.read_text(encoding="utf-8").splitlines() if path.is_file() else []
    new = f"{key}={value}"
    for pattern in (rf"\s*{key}\s*=", rf"\s*#\s*{key}\s*="):
        for i, line in enumerate(lines):
            if re.match(pattern, line):
                lines[i] = new
                path.write_text("\n".join(lines) + "\n", encoding="utf-8")
                return
    lines.append(new)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def ensure_env(server_dir: Path) -> Path:
    env = server_dir / ".env"
    if not env.exists():
        shutil.copy(server_dir / ".env.example", env)
        print("Created .env")
    return env


# --------------------------------------------------------------------------- questions
def ask(question: str, default: str, assume_yes: bool) -> str:
    if assume_yes or not sys.stdin.isatty():
        return default
    hint = f" [{default}]" if default else ""
    try:
        answer = input(f"{question}{hint}: ").strip()
    except EOFError:
        return default
    return answer or default


def ask_yes_no(question: str, default: bool, assume_yes: bool) -> bool:
    answer = ask(question + (" (Y/n)" if default else " (y/N)"), "", assume_yes).lower()
    if not answer:
        return default
    return answer[0] in ("y", "j", "s", "o")  # yes / ja / sí / oui


# --------------------------------------------------------------------------- ollama
def check_ollama(url: str, model: str, assume_yes: bool) -> bool:
    import httpx

    try:
        tags = httpx.get(f"{url}/api/tags", timeout=5).json()
    except Exception:
        print("\n! Ollama is not running (or not installed).")
        print("  Install it from https://ollama.com/download, start it, then run setup again.")
        return False
    names = {m.get("name", "") for m in tags.get("models", [])}
    if model in names or f"{model}:latest" in names:
        print(f"Ollama: {model} is installed.")
        return True
    exe = shutil.which("ollama")
    if not exe:
        print(f"\n! The model {model} is missing. Run:  ollama pull {model}")
        return False
    if not ask_yes_no(f"Download the language model {model} now (a few GB)?", True, assume_yes):
        print(f"  Later:  ollama pull {model}")
        return False
    return subprocess.call([exe, "pull", model]) == 0


# --------------------------------------------------------------------------- main
def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="GeistAI first-time setup")
    ap.add_argument("--yes", action="store_true", help="don't ask, use defaults")
    ap.add_argument("--home", help="your town for weather questions")
    ap.add_argument("--voices", help="languages to download voices for, e.g. de,en,es")
    ap.add_argument("--speak-on-pc", choices=("0", "1"))
    ap.add_argument("--skip-downloads", action="store_true")
    args = ap.parse_args(argv)

    env_path = ensure_env(HERE)
    env = read_env(env_path)

    print("\nA few questions (press Enter to keep the value in brackets).\n")
    home = args.home if args.home is not None else ask(
        "Your town, for weather questions (e.g. Bordesholm)", env.get("HOME_LOCATION", ""),
        args.yes)
    set_env(env_path, "HOME_LOCATION", home)

    # an .env from an older version limits speech recognition to 9 languages
    from .config import DEFAULT_LANGUAGES, OLD_DEFAULT_LANGUAGES

    if env.get("ALLOWED_LANGUAGES", "").replace(" ", "") == OLD_DEFAULT_LANGUAGES:
        set_env(env_path, "ALLOWED_LANGUAGES", DEFAULT_LANGUAGES)
        print("Speech recognition now listens for all supported languages.")

    speak = args.speak_on_pc
    if speak is None:
        speak = "1" if ask_yes_no(
            "Also play the answers on this PC's speakers? (useful for devices without a speaker)",
            env.get("SPEAK_ON_PC", "0") == "1", args.yes) else "0"
    set_env(env_path, "SPEAK_ON_PC", speak)

    voices = args.voices or ask("Voices to download (languages)", "de,en,es", args.yes)
    langs = [x.strip().lower() for x in re.split(r"[,\s]+", voices) if x.strip()]

    if not args.skip_downloads:
        # load the .env we just wrote, so the voice folder etc. are right
        import os

        os.chdir(HERE)
        from . import download_voices
        from .config import Config

        print("\nDownloading voices ...")
        download_voices.main(langs)
        cfg = Config.from_env()
        if cfg.llm_backend == "ollama":
            check_ollama(cfg.llm_url, cfg.llm_model, args.yes)

    print("\nSetup finished. Start the server with start.bat (Windows) or ./start.sh.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

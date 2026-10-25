"""Text clean-up for the tiny screen and for the speech synthesiser."""
from __future__ import annotations

import re
import unicodedata

LANGUAGE_NAMES = {
    "de": "German (Deutsch)",
    "en": "English",
    "es": "Spanish (Español)",
    "fr": "French (Français)",
    "it": "Italian (Italiano)",
    "pt": "Portuguese (Português)",
    "nl": "Dutch (Nederlands)",
    "pl": "Polish (Polski)",
    "tr": "Turkish (Türkçe)",
    "cs": "Czech (Čeština)",
    "sv": "Swedish (Svenska)",
    "da": "Danish (Dansk)",
    "no": "Norwegian (Norsk)",
    "fi": "Finnish (Suomi)",
    "ro": "Romanian (Română)",
    "hu": "Hungarian (Magyar)",
}


def language_name(code: str) -> str:
    return LANGUAGE_NAMES.get(code, code or "the user's language")


# Code points the device font can draw (see tools/gen_assets.py).
_EXTRA = {0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
          0x2022, 0x2026, 0x20AC, 0x2039, 0x203A, 0x00B7}


def _displayable(ch: str) -> bool:
    cp = ord(ch)
    return (0x20 <= cp < 0x7F) or (0xA0 <= cp < 0x180) or cp in _EXTRA or ch == "\n"


_THINK_RE = re.compile(r"<think>.*?</think>", re.DOTALL | re.IGNORECASE)
_CODE_FENCE_RE = re.compile(r"```.*?```", re.DOTALL)
_LINK_RE = re.compile(r"\[([^\]]+)\]\((?:[^)]+)\)")
_HEADING_RE = re.compile(r"^\s{0,3}#{1,6}\s*", re.MULTILINE)
_BULLET_RE = re.compile(r"^\s*(?:[-*+]|\d+[.)])\s+", re.MULTILINE)
_EMPH_RE = re.compile(r"(\*\*|__|\*|_|~~|`)(?=\S)(.+?)(?<=\S)\1")


def strip_markdown(text: str) -> str:
    text = _THINK_RE.sub("", text)
    text = _CODE_FENCE_RE.sub("", text)
    text = _LINK_RE.sub(r"\1", text)
    text = _HEADING_RE.sub("", text)
    text = _BULLET_RE.sub("• ", text)
    for _ in range(2):  # nested emphasis
        text = _EMPH_RE.sub(r"\2", text)
    text = text.replace("`", "")
    return text


def _fold(ch: str) -> str:
    """Map an unsupported character to something drawable, or drop it."""
    if _displayable(ch):
        return ch
    if unicodedata.category(ch).startswith("Z"):
        return " "
    decomposed = unicodedata.normalize("NFKD", ch)
    base = "".join(c for c in decomposed if not unicodedata.combining(c))
    if base and all(_displayable(c) for c in base):
        return base
    return ""  # emoji, symbols, CJK ...


def for_display(text: str) -> str:
    """Plain text the T-Embed font can render."""
    text = unicodedata.normalize("NFC", strip_markdown(text))
    text = "".join(_fold(c) for c in text)
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r" *\n *", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def for_speech(text: str) -> str:
    """Text for TTS: no markdown, no bullets, no emoji."""
    text = for_display(text)
    text = text.replace("•", "").replace("\n", " ")
    return re.sub(r"\s+", " ", text).strip()


_STOPWORDS = {
    "de": "der die das und ist nicht ich du wie was wer ein eine mit auf für ist es zu den von bitte hallo danke",
    "en": "the and is are not you what who how a an with on for it to of please hello thanks i",
    "es": "el la los las y es no yo tú qué quién cómo un una con en por para de hola gracias",
    "fr": "le la les et est pas je tu que qui comment un une avec sur pour de bonjour merci",
    "it": "il lo la gli e è non io tu che chi come un una con su per di ciao grazie",
    "pt": "o a os as e é não eu você que quem como um uma com em para de olá obrigado",
    "nl": "de het en is niet ik jij wat wie hoe een met op voor van hallo dank",
}
_STOPSETS = {lang: set(words.split()) for lang, words in _STOPWORDS.items()}


def guess_language(text: str, default: str = "en") -> str:
    """Very small stop-word language guesser, used for typed text only
    (spoken input gets its language from Whisper)."""
    words = re.findall(r"[^\W\d_]+", text.lower())
    if not words:
        return default
    scores = {lang: sum(w in stop for w in words) for lang, stop in _STOPSETS.items()}
    best = max(scores, key=scores.get)
    return best if scores[best] > 0 else default

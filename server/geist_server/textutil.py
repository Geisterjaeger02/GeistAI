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
    "ru": "Russian (Русский)",
    "uk": "Ukrainian (Українська)",
    "el": "Greek (Ελληνικά)",
    "ar": "Arabic (العربية)",
    "fa": "Persian (فارسی)",
    "hi": "Hindi (हिन्दी)",
    "zh": "Chinese (中文, Simplified)",
    "vi": "Vietnamese (Tiếng Việt)",
    "id": "Indonesian (Bahasa Indonesia)",
    "ca": "Catalan (Català)",
    "sk": "Slovak (Slovenčina)",
    "sr": "Serbian (Srpski)",
}


def language_name(code: str) -> str:
    return LANGUAGE_NAMES.get(code, code or "the user's language")


# Code points the device font can draw (see tools/gen_assets.py).
_EXTRA = {0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
          0x2022, 0x2026, 0x20AC, 0x2039, 0x203A, 0x00B7}


# Must match CODEPOINTS in tools/gen_assets.py.
_RANGES = (
    (0x20, 0x7E), (0xA0, 0x17F),        # Latin-1, Latin Extended-A
    (0x1A0, 0x1A1), (0x1AF, 0x1B0),     # Vietnamese ơ ư
    (0x218, 0x21B),                     # Romanian ș ț
    (0x384, 0x3CE),                     # Greek
    (0x400, 0x45F), (0x490, 0x491),     # Cyrillic (Russian, Ukrainian, Serbian ...)
    (0x1EA0, 0x1EF9),                   # Vietnamese
)


def _displayable(ch: str) -> bool:
    cp = ord(ch)
    return ch == "\n" or cp in _EXTRA or any(a <= cp <= b for a, b in _RANGES)


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
    if unicodedata.category(ch)[0] in "LNP":  # other scripts: Chinese, Arabic, Hindi ...
        latin = _transliterate(ch)
        if latin and all(_displayable(c) for c in latin):
            return latin
    return ""  # emoji, symbols ...


def _transliterate(ch: str) -> str:
    """中 -> zhong, ب -> b ... so the screen shows something readable."""
    try:
        from anyascii import anyascii
    except ImportError:
        return ""
    return anyascii(ch)


def _space_words(text: str) -> str:
    # transliterated Chinese has no spaces: "nihao" -> "ni hao" reads better
    return re.sub(r"(?<=[a-z])(?=[A-Z])", " ", text)


def for_display(text: str) -> str:
    """Plain text the device font can render (other scripts are transliterated)."""
    text = unicodedata.normalize("NFC", strip_markdown(text))
    parts, prev_cjk = [], False
    for c in text:
        folded = _fold(c)
        cjk = 0x3400 <= ord(c) <= 0x9FFF and folded.isalpha()
        if cjk:  # Chinese has no spaces: 你好 -> "ni hao"
            folded = folded.lower()
            if parts and parts[-1][-1:].isalnum():
                folded = " " + folded
        elif prev_cjk and folded[:1].isalnum():
            folded = " " + folded
        elif folded in (".", ",", "!", "?", ":", ";") and not _displayable(c):
            folded += " "  # 。，！ are full-width and carry their own space
        parts.append(folded)
        prev_cjk = cjk
    text = "".join(parts)
    text = re.sub(r"[ \t]+", " ", text)
    text = re.sub(r" *\n *", "\n", text)
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()


def for_speech(text: str, lang: str = "") -> str:
    """Text for TTS: no markdown, no bullets, no emoji, years spoken as years.
    Keeps every script (the voices speak Russian, Chinese ... natively)."""
    text = unicodedata.normalize("NFC", strip_markdown(text))
    text = "".join(c for c in text if unicodedata.category(c) not in ("So", "Sk", "Cs", "Co")
                   and c not in "\u200d\ufe0f")
    text = text.replace("•", " ").replace("\n", " ")
    text = spell_years(text, lang)
    return re.sub(r"\s+", " ", text).strip()


# --------------------------------------------------------------------------- years
# Languages where a year is NOT read like the plain number:
#   de 1936 -> neunzehnhundertsechsunddreißig (not eintausendneunhundert...)
#   en 1936 -> nineteen thirty-six
# Elsewhere (es, fr, it, ru ...) the normal number is the correct year form.
_YEAR_LANGS = {"de", "en", "nl", "da", "no", "sv"}
_YEAR_RE = re.compile(r"(?<![\d.,:/€$£-])(1[1-9]\d\d|20\d\d)(?![\d€$£%])(?![.,:/]\d)")
# a number followed by one of these is an amount, not a year
_NOT_YEAR_AFTER = re.compile(
    r"\s*(?:€|\$|£|%|‰|°|euro|eur\b|dollar|usd|pfund|cent|prozent|percent|km|m\b|cm|mm|kg|g\b|"
    r"l\b|liter|meter|kilometer|gramm|stück|mal\b|times|x\b|kcal|kwh|mb|gb|ps\b|hp\b|"
    r"einwohner|menschen|people|leute|euros|dollars|points|punkte|tore|goals|seiten|pages|"
    r"stunden|hours|minuten|minutes|sekunden|seconds|tage|days|jahre\b|years\b)", re.I)
_NOT_YEAR_BEFORE = re.compile(r"(?:€|\$|£|nr\.?|nummer|number|no\.|#|tel\.?|plz)\s*$", re.I)


def _year_words(year: int, lang: str) -> str | None:
    try:
        from num2words import num2words
    except ImportError:
        return None
    try:
        if lang == "sv":  # num2words has no Swedish year mode: nitton + hundra + trettiosex
            if year % 100 == 0 or year >= 2000:
                return num2words(year, lang="sv")
            return num2words(year // 100, lang="sv") + "hundra" + num2words(year % 100, lang="sv")
        words = num2words(year, lang=lang, to="year")
        if lang == "nl":
            words = words.replace(" honderd ", "honderd")  # negentienhonderdzesendertig
        return words
    except Exception:  # language without a year mode
        return None


def spell_years(text: str, lang: str) -> str:
    """Writes four-digit years as words where the language reads them differently."""
    lang = (lang or "").split("_")[0].lower()
    if lang not in _YEAR_LANGS:
        return text

    def sub(m: re.Match) -> str:
        if _NOT_YEAR_AFTER.match(text, m.end()) or _NOT_YEAR_BEFORE.search(text[:m.start()]):
            return m.group(0)
        words = _year_words(int(m.group(1)), lang)
        return words or m.group(0)

    return _YEAR_RE.sub(sub, text)


_STOPWORDS = {
    "de": "der die das und ist nicht ich du wie was wer ein eine mit auf für ist es zu den von bitte hallo danke",
    "en": "the and is are not you what who how a an with on for it to of please hello thanks i",
    "es": "el la los las y es no yo tú qué quién cómo un una con en por para de hola gracias",
    "fr": "le la les et est pas je tu que qui comment un une avec sur pour de bonjour merci",
    "it": "il lo la gli e è non io tu che chi come un una con su per di ciao grazie",
    "pt": "o a os as e é não eu você que quem como um uma com em para de olá obrigado",
    "nl": "de het en is niet ik jij wat wie hoe een met op voor van hallo dank",
    "pl": "i jest nie ja ty co kto jak z na dla w się cześć dziękuję proszę",
    "tr": "ve bir bu ne kim nasıl ben sen için ile merhaba teşekkürler değil",
    "sv": "och är inte jag du vad vem hur en ett med på för av hej tack",
    "da": "og er ikke jeg du hvad hvem hvordan en et med på for af hej tak",
    "no": "og er ikke jeg du hva hvem hvordan en et med på for av hei takk",
    "cs": "a je není já ty co kdo jak s na pro z ahoj děkuji prosím",
    "ro": "și este nu eu tu ce cine cum un o cu pe pentru de bună mulțumesc",
    "hu": "és van nem én te mi ki hogyan egy a az val szia köszönöm",
    "fi": "ja on ei minä sinä mitä kuka miten kanssa hei kiitos",
    "id": "dan adalah tidak saya kamu apa siapa bagaimana dengan di untuk halo terima kasih",
}
_STOPSETS = {lang: set(words.split()) for lang, words in _STOPWORDS.items()}


def guess_language(text: str, default: str = "en") -> str:
    """Very small stop-word language guesser, used for typed text only
    (spoken input gets its language from Whisper)."""
    script = _script_language(text)
    if script:
        return script
    words = re.findall(r"[^\W\d_]+", text.lower())
    if not words:
        return default
    scores = {lang: sum(w in stop for w in words) for lang, stop in _STOPSETS.items()}
    best = max(scores, key=scores.get)
    return best if scores[best] > 0 else default


def _script_language(text: str) -> str | None:
    """Languages that can be told apart by their alphabet."""
    counts: dict[str, int] = {}
    for ch in text:
        cp = ord(ch)
        if 0x400 <= cp <= 0x4FF:
            key = "uk" if ch in "іїєґІЇЄҐ" else "cyr"
        elif 0x370 <= cp <= 0x3FF:
            key = "el"
        elif 0x600 <= cp <= 0x6FF:
            key = "fa" if ch in "پچژگی" else "ar"
        elif 0x900 <= cp <= 0x97F:
            key = "hi"
        elif 0x4E00 <= cp <= 0x9FFF or 0x3400 <= cp <= 0x4DBF:
            key = "zh"
        elif cp in (0x1A0, 0x1A1, 0x1AF, 0x1B0, 0x111) or 0x1EA0 <= cp <= 0x1EF9:
            key = "vi"
        else:
            continue
        counts[key] = counts.get(key, 0) + 1
    if not counts:
        return None
    if "uk" in counts:
        return "uk"
    if "fa" in counts and "ar" in counts:
        return "fa"
    best = max(counts, key=counts.get)
    return "ru" if best == "cyr" else best

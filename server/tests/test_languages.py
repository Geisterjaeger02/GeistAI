"""More languages: year words, other scripts on the screen, typed-language guess."""
import importlib.util
import unittest

from geist_server.config import DEFAULT_VOICES, Config
from geist_server.textutil import LANGUAGE_NAMES, for_display, for_speech, guess_language

HAS_NUM2WORDS = importlib.util.find_spec("num2words") is not None
HAS_ANYASCII = importlib.util.find_spec("anyascii") is not None


@unittest.skipUnless(HAS_NUM2WORDS, "pip install num2words")
class YearTest(unittest.TestCase):
    def test_german_years(self):
        self.assertEqual(for_speech("Er wurde 1936 geboren.", "de"),
                         "Er wurde neunzehnhundertsechsunddreißig geboren.")
        self.assertEqual(for_speech("Von 1914 bis 1918, heute 2026.", "de"),
                         "Von neunzehnhundertvierzehn bis neunzehnhundertachtzehn, heute "
                         "zweitausendsechsundzwanzig.")
        self.assertEqual(for_speech("Die 1990er Jahre", "de"), "Die neunzehnhundertneunziger Jahre")

    def test_english_years(self):
        self.assertEqual(for_speech("Born in 1936, retired in 2005.", "en"),
                         "Born in nineteen thirty-six, retired in two thousand and five.")

    def test_amounts_times_and_numbers_stay(self):
        for text in ("Das kostet 1500 Euro.", "Es sind 1936 Einwohner.", "Um 19:36 Uhr.",
                     "1.936 Punkte", "3,1936", "Nr. 1936", "€ 1999", "1200 km", "50 %"):
            self.assertEqual(for_speech(text, "de"), text)

    def test_languages_that_read_years_as_numbers(self):
        self.assertEqual(for_speech("En 1936 empezó.", "es"), "En 1936 empezó.")
        self.assertEqual(for_speech("В 1936 году.", "ru"), "В 1936 году.")

    def test_dutch_and_swedish(self):
        self.assertIn("negentienhonderdzesendertig", for_speech("In 1936.", "nl"))
        self.assertIn("nittonhundratrettiosex", for_speech("År 1936.", "sv"))


class ScriptTest(unittest.TestCase):
    def test_cyrillic_greek_vietnamese_romanian_shown_as_is(self):
        for text in ("Привет, как дела?", "Добрий день, ґанок", "Γεια σου, τι κάνεις;",
                     "Xin chào, bạn khỏe không?", "Bună ziua, mulțumesc"):
            self.assertEqual(for_display(text), text)

    @unittest.skipUnless(HAS_ANYASCII, "pip install anyascii")
    def test_other_scripts_transliterated(self):
        self.assertEqual(for_display("你好，我是GeistAI。"), "ni hao, wo shi GeistAI.")
        self.assertTrue(for_display("مرحبا").isascii())
        self.assertTrue(for_display("नमस्ते").isascii())

    def test_speech_keeps_the_original_script(self):
        self.assertEqual(for_speech("你好！👋 **今天**", "zh"), "你好！ 今天")
        self.assertEqual(for_speech("Привет 🙂", "ru"), "Привет")

    def test_guess_typed_language_by_alphabet(self):
        cases = {"Привет, как дела?": "ru", "Привіт, як справи? Їжа": "uk", "Γεια σου": "el",
                 "مرحبا كيف حالك": "ar", "سلام، چطوری؟": "fa", "नमस्ते": "hi", "你好吗": "zh",
                 "Xin chào, bạn khỏe không?": "vi", "Hej, hur mår du? Tack": "sv"}
        for text, lang in cases.items():
            self.assertEqual(guess_language(text), lang, text)


class ConfigTest(unittest.TestCase):
    def test_every_allowed_language_has_a_voice_and_a_name(self):
        cfg = Config.from_env()
        for lang in Config().allowed_languages or cfg.allowed_languages:
            self.assertIn(lang, DEFAULT_VOICES, lang)
            self.assertIn(lang, LANGUAGE_NAMES, lang)


if __name__ == "__main__":
    unittest.main()

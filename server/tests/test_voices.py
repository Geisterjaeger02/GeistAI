"""Voice choice: ids, the voice catalogs, online voices falling back to Piper."""
import json
import tempfile
import unittest
from pathlib import Path

from geist_server.config import Config
from geist_server.tts import TTS
from geist_server.voices import Catalog, parse, sample_text


def cfg_with_catalog() -> Config:
    cfg = Config()
    cfg.voices_dir = Path(tempfile.mkdtemp())
    catalog = {
        "de_DE-thorsten-high": {"name": "thorsten", "quality": "high", "num_speakers": 1,
                                "language": {"family": "de", "country_english": "Germany"}},
        "de_DE-kerstin-low": {"name": "kerstin", "quality": "low", "num_speakers": 1,
                              "language": {"family": "de", "country_english": "Germany"}},
        "de_DE-eva_k-x_low": {"name": "eva_k", "quality": "x_low", "num_speakers": 1,
                              "language": {"family": "de"}},
        "de_DE-thorsten_emotional-medium": {"name": "thorsten_emotional", "quality": "medium",
                                            "num_speakers": 8, "language": {"family": "de"}},
        "en_US-amy-medium": {"name": "amy", "quality": "medium", "num_speakers": 1,
                             "language": {"family": "en"}},
    }
    (cfg.voices_dir / "voices.json").write_text(json.dumps(catalog), encoding="utf-8")
    (cfg.voices_dir / "de_DE-kerstin-low.onnx").write_bytes(b"x")
    return cfg


class ParseTest(unittest.TestCase):
    def test_ids(self):
        self.assertEqual(parse("edge:de-DE-KatjaNeural"), ("edge", "de-DE-KatjaNeural"))
        self.assertEqual(parse("openai:nova"), ("openai", "nova"))
        self.assertEqual(parse("de_DE-thorsten-high"), ("piper", "de_DE-thorsten-high"))
        self.assertEqual(parse("piper:de_DE-thorsten-high"), ("piper", "de_DE-thorsten-high"))

    def test_samples(self):
        self.assertIn("Luna", sample_text("de", "Luna"))
        self.assertIn("GeistAI", sample_text("xx"))


class CatalogTest(unittest.TestCase):
    def test_piper_voices_for_a_language(self):
        cat = Catalog(cfg_with_catalog())
        ids = [v["id"] for v in cat.piper_voices("de")]
        self.assertIn("piper:de_DE-thorsten-high", ids)
        self.assertIn("piper:de_DE-kerstin-low", ids)
        self.assertNotIn("piper:de_DE-eva_k-x_low", ids)       # too rough
        self.assertNotIn("piper:de_DE-thorsten_emotional-medium", ids)  # multi-speaker
        self.assertNotIn("piper:en_US-amy-medium", ids)
        kerstin = [v for v in cat.piper_voices("de") if "kerstin" in v["id"]][0]
        self.assertTrue(kerstin["installed"])
        self.assertIn("Germany", kerstin["label"])

    def test_edge_voices_by_locale(self):
        cat = Catalog(cfg_with_catalog())
        cat._edge = [{"ShortName": "de-DE-KatjaNeural", "Locale": "de-DE", "Gender": "Female"},
                     {"ShortName": "de-AT-JonasNeural", "Locale": "de-AT", "Gender": "Male"},
                     {"ShortName": "en-US-AriaNeural", "Locale": "en-US", "Gender": "Female"},
                     {"ShortName": "nb-NO-PernilleNeural", "Locale": "nb-NO", "Gender": "Female"}]
        cat._edge_time = 1e12
        ids = [v["id"] for v in cat.edge_voices("de")]
        self.assertEqual(ids, ["edge:de-DE-KatjaNeural", "edge:de-AT-JonasNeural"])
        self.assertEqual(cat.edge_voices("no")[0]["id"], "edge:nb-NO-PernilleNeural")
        self.assertIn("Katja", cat.edge_voices("de")[0]["label"])

    def test_openai_only_with_key(self):
        cfg = cfg_with_catalog()
        self.assertEqual(Catalog(cfg).openai_voices(), [])
        cfg.openai_api_key = "sk-test"
        self.assertIn("openai:nova", [v["id"] for v in Catalog(cfg).openai_voices()])


class FallbackTest(unittest.TestCase):
    def test_online_voice_failure_uses_local_voice(self):
        cfg = cfg_with_catalog()
        cfg.voices["de"] = "edge:de-DE-KatjaNeural"
        tts = TTS(cfg)
        spoken = []

        def broken_edge(text, name):
            raise ConnectionError("offline")

        tts._edge = broken_edge
        tts.ensure_piper = lambda name, wait_s: True
        tts._load_piper = lambda name: name
        tts._piper = lambda voice, text: spoken.append(voice) or b"RIFF-wav"
        self.assertEqual(tts.synthesize("Hallo", "de"), b"RIFF-wav")
        self.assertEqual(spoken, ["de_DE-thorsten-medium"])  # the default German voice

    def test_explicit_voice_for_preview(self):
        cfg = cfg_with_catalog()
        tts = TTS(cfg)
        used = []
        tts._openai = lambda text, name: used.append(name) or b"RIFF-openai"
        self.assertEqual(tts.synthesize("Hi", "en", voice="openai:nova"), b"RIFF-openai")
        self.assertEqual(used, ["nova"])

    def test_available_languages(self):
        cfg = cfg_with_catalog()
        cfg.voices = {"de": "de_DE-kerstin-low", "en": "edge:en-US-AriaNeural",
                      "fr": "fr_FR-siwis-medium"}
        self.assertEqual(TTS(cfg).available_languages(), ["de", "en"])  # fr not downloaded


if __name__ == "__main__":
    unittest.main()

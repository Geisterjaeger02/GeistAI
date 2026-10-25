"""Tests that run without any AI models (fake engines).

    cd server && python -m unittest discover -s tests -v
"""
import io
import math
import unittest
import wave

import numpy as np

from geist_server import audio
from geist_server.llm import Conversations
from geist_server.pipeline import Assistant, AudioCache
from geist_server.stt import Transcript
from geist_server.textutil import for_display, for_speech, guess_language


def make_wav(seconds=1.0, rate=16000, freq=220.0, amp=0.3, channels=1):
    n = int(seconds * rate)
    t = np.arange(n) / rate
    pcm = (np.sin(2 * math.pi * freq * t) * amp * 32767).astype("<i2")
    if channels == 2:
        pcm = np.repeat(pcm, 2)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(pcm.tobytes())
    return buf.getvalue()


class FakeSTT:
    def __init__(self, text="Hallo, wer bist du?", lang="de"):
        self.text, self.lang, self.calls = text, lang, []

    def transcribe(self, pcm, language=None):
        self.calls.append(language)
        return Transcript(self.text, language or self.lang, 0.99)


class FakeLLM:
    def __init__(self):
        self.memory = Conversations(4)
        self.seen = []

    def reply(self, device, text, language, options=None):
        self.seen.append((device, text, language))
        self.options = options
        if options is not None:
            options.used = "cloud" if options.cloud else "local"
        return "**Ich bin GeistAI** 👻 – dein *lokaler* Assistent."


class FakeTTS:
    def __init__(self):
        self.calls = []

    def synthesize(self, text, lang):
        self.calls.append((text, lang))
        return make_wav(0.2, rate=22050)


class TextTests(unittest.TestCase):
    def test_display_strips_markdown_and_emoji(self):
        self.assertEqual(for_display("**Hallo** 👋 _Welt_!"), "Hallo Welt!")

    def test_display_keeps_accents(self):
        self.assertEqual(for_display("¿Qué tal? Ça va, Grüße, Łódź"), "¿Qué tal? Ça va, Grüße, Łódź")

    def test_display_folds_unsupported_letters(self):
        self.assertEqual(for_display("Ştefan Bucureşti"), "Ştefan Bucureşti")  # Ş is in Latin Ext-A
        self.assertEqual(for_display("Đ ǅ ș ȁ"), "Đ Dz ș a")

    def test_display_bullets_and_think(self):
        self.assertEqual(for_display("<think>hmm</think>- eins\n- zwei"), "• eins\n• zwei")

    def test_speech(self):
        self.assertEqual(for_speech("# Titel\n- a\n- b 🚀"), "Titel a b")

    def test_guess_language(self):
        self.assertEqual(guess_language("Hallo, wie geht es dir?"), "de")
        self.assertEqual(guess_language("¿Qué hora es en Madrid?"), "es")
        self.assertEqual(guess_language("What is the capital of France?"), "en")
        self.assertEqual(guess_language("12345"), "en")


class AudioTests(unittest.TestCase):
    def test_decode_and_resample(self):
        pcm, rate = audio.wav_to_float32(make_wav(1.0, rate=22050, channels=2))
        self.assertEqual(rate, 16000)
        self.assertAlmostEqual(len(pcm), 16000, delta=2)
        self.assertGreater(audio.rms(pcm), 0.1)

    def test_normalize(self):
        quiet = np.full(1000, 0.01, dtype=np.float32) + np.sin(np.arange(1000)) * 0.01
        out = audio.normalize(quiet.astype(np.float32))
        self.assertAlmostEqual(float(np.mean(out)), 0.0, places=3)
        self.assertLessEqual(float(np.max(np.abs(out))), 0.9001)

    def test_fix_wav_sizes(self):
        good = make_wav(0.1)
        broken = good[:4] + b"\xff\xff\xff\xff" + good[8:40] + b"\xff\xff\xff\xff" + good[44:]
        fixed = audio.fix_wav_sizes(broken)
        self.assertEqual(fixed, good)


class PipelineTests(unittest.TestCase):
    def setUp(self):
        self.stt, self.llm, self.tts = FakeSTT(), FakeLLM(), FakeTTS()
        self.a = Assistant(self.stt, self.llm, self.tts, AudioCache())

    def test_full_turn(self):
        r = self.a.handle_audio(make_wav(1.0), "dev1", "auto", True)
        self.assertTrue(r.ok)
        self.assertEqual(r.language, "de")
        self.assertEqual(r.transcript, "Hallo, wer bist du?")
        self.assertEqual(r.reply, "Ich bin GeistAI – dein lokaler Assistent.")
        self.assertEqual(self.stt.calls, [None])  # auto-detect
        self.assertEqual(self.tts.calls[0][1], "de")
        j = r.to_json()
        self.assertTrue(j["audio"].startswith("/api/audio/"))
        audio_id = j["audio"].rsplit("/", 1)[1][:-4]
        self.assertIsNotNone(self.a.cache.get(audio_id))

    def test_forced_language(self):
        self.a.handle_audio(make_wav(1.0), "dev1", "es", False)
        self.assertEqual(self.stt.calls, ["es"])
        self.assertEqual(self.llm.seen[0][2], "es")
        self.assertEqual(self.tts.calls, [])  # tts off

    def test_silence(self):
        r = self.a.handle_audio(make_wav(1.0, amp=0.0), "dev1", "auto", True)
        self.assertFalse(r.ok)
        self.assertEqual(r.error, "no_speech")

    def test_bad_audio(self):
        r = self.a.handle_audio(b"not a wav", "dev1", "auto", True)
        self.assertEqual(r.error, "bad audio")

    def test_text_turn_guesses_language(self):
        r = self.a.handle_text("¿Qué tal estás hoy?", "web", "auto", True)
        self.assertEqual(r.language, "es")


class SpeakOnPcTests(unittest.TestCase):
    def test_plays_locally_even_without_device_voice(self):
        played = []
        a = Assistant(FakeSTT(), FakeLLM(), FakeTTS(), AudioCache(), speak_locally=played.append)
        r = a.handle_audio(make_wav(1.0), "dev1", "auto", False)
        self.assertEqual(len(played), 1)
        self.assertIsNone(r.to_json()["audio"])  # device didn't ask for audio


class MemoryTests(unittest.TestCase):
    def test_history_limit_and_reset(self):
        m = Conversations(max_turns=2)
        for i in range(5):
            m.add("d", f"q{i}", f"a{i}")
        h = m.history("d")
        self.assertEqual([x["content"] for x in h], ["q3", "a3", "q4", "a4"])
        m.reset("d")
        self.assertEqual(m.history("d"), [])


if __name__ == "__main__":
    unittest.main()

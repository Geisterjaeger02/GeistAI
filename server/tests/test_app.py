"""HTTP API tests with fake engines (needs fastapi + httpx; skipped otherwise)."""
import unittest

import numpy as np

try:
    from fastapi.testclient import TestClient
except Exception:  # pragma: no cover - fastapi not installed
    TestClient = None

from geist_server.config import Config
from geist_server.pipeline import Assistant, AudioCache
from tests.test_pipeline import FakeLLM, FakeSTT, FakeTTS, make_wav


@unittest.skipIf(TestClient is None, "fastapi not installed")
class AppTests(unittest.TestCase):
    def setUp(self):
        from geist_server.app import create_app

        cfg = Config()
        cfg.announce_mdns = False
        self.stt = FakeSTT()
        self.assistant = Assistant(self.stt, FakeLLM(), FakeTTS(), AudioCache())
        self.client = TestClient(create_app(cfg, self.assistant))

    def test_health(self):
        self.assertTrue(self.client.get("/health").json()["ok"])

    def test_wav_upload(self):
        r = self.client.post("/api/chat?device=t&lang=auto&tts=1", content=make_wav(1.0),
                             headers={"Content-Type": "audio/wav"})
        j = r.json()
        self.assertTrue(j["ok"], j)
        audio = self.client.get(j["audio"])
        self.assertEqual(audio.status_code, 200)
        self.assertEqual(audio.content[:4], b"RIFF")

    def test_streamed_l16_upload(self):
        pcm = (np.sin(np.arange(16000) / 5.0) * 8000).astype("<i2").tobytes()

        def chunks():  # a generator makes the client use chunked transfer encoding
            for i in range(0, len(pcm), 1024):
                yield pcm[i:i + 1024]

        r = self.client.post("/api/chat?device=t&lang=de&tts=0", content=chunks(),
                             headers={"Content-Type": "audio/L16; rate=16000; channels=1"})
        j = r.json()
        self.assertTrue(j["ok"], j)
        self.assertEqual(self.stt.calls, ["de"])

    def test_text(self):
        r = self.client.post("/api/text?lang=auto", json={"text": "Hola, ¿qué tal?"})
        self.assertEqual(r.json()["language"], "es")

    def test_assistant_options(self):
        r = self.client.post("/api/text?lang=de&ai=cloud&name=Luna", json={"text": "Hallo"})
        self.assertEqual(r.json()["ai"], "cloud")
        opts = self.assistant.llm.options
        self.assertEqual((opts.cloud, opts.name), (True, "Luna"))
        self.assertIn("cloud", self.client.get("/health").json())


if __name__ == "__main__":
    unittest.main()

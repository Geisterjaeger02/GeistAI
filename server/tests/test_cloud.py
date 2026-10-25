"""ChatGPT option and the assistant's nickname."""
import unittest

from geist_server.config import Config
from geist_server.llm import ChatError, ChatOptions, LLMClient, clean_name


class FakeBackends(LLMClient):
    def __init__(self, key="", cloud_fails=False):
        cfg = Config()
        cfg.openai_api_key = key
        cfg.tools_enabled = False
        super().__init__(cfg)
        self.cloud_fails = cloud_fails
        self.calls = []

    def _complete(self, messages, max_tokens=None, json_schema=None, temperature=None,
                  cloud=False):
        self.calls.append(("cloud" if cloud else "local", messages))
        if cloud and self.cloud_fails:
            raise ChatError("OpenAI limit reached or no credit left")
        return "cloud answer" if cloud else "local answer"


class CloudTest(unittest.TestCase):
    def test_local_by_default(self):
        llm = FakeBackends(key="sk-test")
        opts = ChatOptions()
        self.assertEqual(llm.reply("d", "Hallo", "de", options=opts), "local answer")
        self.assertEqual(opts.used, "local")

    def test_chatgpt_when_chosen(self):
        llm = FakeBackends(key="sk-test")
        opts = ChatOptions(cloud=True)
        self.assertEqual(llm.reply("d", "Hallo", "de", options=opts), "cloud answer")
        self.assertEqual(opts.used, "cloud")

    def test_chatgpt_without_key_uses_local(self):
        llm = FakeBackends(key="")
        opts = ChatOptions(cloud=True)
        self.assertEqual(llm.reply("d", "Hallo", "de", options=opts), "local answer")
        self.assertEqual([c[0] for c in llm.calls], ["local"])

    def test_chatgpt_failure_falls_back(self):
        llm = FakeBackends(key="sk-test", cloud_fails=True)
        opts = ChatOptions(cloud=True)
        self.assertEqual(llm.reply("d", "Hallo", "de", options=opts), "local answer")
        self.assertEqual([c[0] for c in llm.calls], ["cloud", "local"])
        self.assertEqual(opts.used, "local")

    def test_nickname_in_system_prompt(self):
        llm = FakeBackends()
        llm.reply("d", "Wie heißt du?", "de", options=ChatOptions(name="Luna"))
        self.assertIn("You are Luna,", llm.calls[-1][1][0]["content"])
        llm.reply("d", "Wie heißt du?", "de")
        self.assertIn("You are GeistAI,", llm.calls[-1][1][0]["content"])

    def test_clean_name(self):
        self.assertEqual(clean_name("  Luna\n{x} "), "Lunax")
        self.assertEqual(len(clean_name("A" * 80)), 30)


class OpenAIBodyTest(unittest.TestCase):
    def body(self, model, quirks=()):
        cfg = Config()
        cfg.openai_model = model
        llm = LLMClient(cfg)
        llm._openai_quirks |= set(quirks)
        return llm._openai_body([{"role": "user", "content": "hi"}], 220, None, 0.6)

    def test_classic_model(self):
        b = self.body("gpt-4.1-mini")
        self.assertEqual((b["max_tokens"], b["temperature"]), (220, 0.6))
        self.assertNotIn("reasoning_effort", b)

    def test_reasoning_model(self):
        b = self.body("gpt-5-mini")
        self.assertNotIn("temperature", b)
        self.assertNotIn("max_tokens", b)
        self.assertGreaterEqual(b["max_completion_tokens"], 1200)
        self.assertEqual(b["reasoning_effort"], "minimal")

    def test_learned_quirks(self):
        b = self.body("some-new-model", {"max_completion_tokens", "no_temperature"})
        self.assertEqual(b["max_completion_tokens"], 220)
        self.assertNotIn("temperature", b)


if __name__ == "__main__":
    unittest.main()

import tempfile
import unittest
from pathlib import Path

from geist_server.firstrun import read_env, set_env


class EnvFileTest(unittest.TestCase):
    def test_set_replaces_instead_of_duplicating(self):
        path = Path(tempfile.mkdtemp()) / ".env"
        path.write_text("A=1\nHOME_LOCATION=                # your town\n# VOICE_DE=x\n",
                        encoding="utf-8")
        set_env(path, "HOME_LOCATION", "Bordesholm")
        set_env(path, "HOME_LOCATION", "Kiel")
        set_env(path, "VOICE_DE", "de_DE-kerstin-low")
        set_env(path, "NEW", "yes")
        text = path.read_text(encoding="utf-8")
        self.assertEqual(text.count("HOME_LOCATION"), 1)
        self.assertEqual(read_env(path), {"A": "1", "HOME_LOCATION": "Kiel",
                                          "VOICE_DE": "de_DE-kerstin-low", "NEW": "yes"})


if __name__ == "__main__":
    unittest.main()

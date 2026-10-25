#!/usr/bin/env python3
"""Builds the web flasher site (GitHub Pages) from the merged firmware images.

    python3 tools/make_flasher.py --bins <dir with geistai-<env>.bin> --out _site

CI merges bootloader + partition table + app into one image per board
(esptool merge_bin), so every manifest has a single part at offset 0.
"""
from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BOARDS = {
    "tembed_cc1101": ("LilyGO T-Embed CC1101", "ESP32-S3"),
    "tembed": ("LilyGO T-Embed", "ESP32-S3"),
    "cardputer": ("M5Stack Cardputer", "ESP32-S3"),
    "stickcplus2": ("M5Stack StickC Plus2", "ESP32"),
}


def firmware_version() -> str:
    text = (ROOT / "firmware" / "include" / "config.h").read_text(encoding="utf-8")
    m = re.search(r'#define\s+GEIST_VERSION\s+"([^"]+)"', text)
    return m.group(1) if m else "dev"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--bins", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    out: Path = args.out
    if out.exists():
        shutil.rmtree(out)
    (out / "firmware").mkdir(parents=True)
    (out / "manifests").mkdir()

    version = firmware_version()
    missing = []
    for env, (name, chip) in BOARDS.items():
        src = args.bins / f"geistai-{env}.bin"
        if not src.exists():
            missing.append(src.name)
            continue
        shutil.copy2(src, out / "firmware" / src.name)
        manifest = {
            "name": f"GeistAI for {name}",
            "version": version,
            "new_install_prompt_erase": True,
            # give the fresh firmware time to boot before asking for Wi-Fi
            "new_install_improv_wait_time": 20,
            "builds": [
                {"chipFamily": chip, "parts": [{"path": f"../firmware/{src.name}", "offset": 0}]}
            ],
        }
        (out / "manifests" / f"{env}.json").write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"{env}: {src.stat().st_size // 1024} KB")

    if missing:
        print("missing images:", ", ".join(missing))
        return 1

    shutil.copy2(ROOT / "site" / "index.html", out / "index.html")
    for img in ("screens.png", "screens_m5.png"):
        if (ROOT / "docs" / img).exists():
            shutil.copy2(ROOT / "docs" / img, out / img)
    (out / ".nojekyll").write_text("")
    print(f"site written to {out} (firmware {version})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

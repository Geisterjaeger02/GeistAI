#!/usr/bin/env sh
# GeistAI server setup for Linux / macOS. Safe to run again.
set -e
cd "$(dirname "$0")"
PY=""
for v in 3.13 3.12 3.11 3.10; do
  if command -v "python$v" >/dev/null 2>&1; then PY="python$v"; break; fi
done
if [ -z "$PY" ]; then
  echo "Python 3.10-3.13 not found. Install one (e.g. sudo apt install python3.12-venv, or brew install python@3.13)."
  exit 1
fi
echo "[1/4] Python: $PY"
[ -x .venv/bin/python ] || "$PY" -m venv .venv
echo "[2/4] Installing packages ..."
.venv/bin/python -m pip install --upgrade pip --quiet
.venv/bin/python -m pip install -r requirements.txt
if command -v nvidia-smi >/dev/null 2>&1; then
  echo "[3/4] NVIDIA GPU found - installing GPU support"
  .venv/bin/python -m pip install -r requirements-gpu.txt || echo "GPU support failed, using the CPU."
else
  echo "[3/4] No NVIDIA GPU - speech recognition runs on the CPU."
fi
echo "[4/4] Settings, voices and language model"
.venv/bin/python -m geist_server.firstrun
echo "All done! Start GeistAI with ./start.sh"

#!/usr/bin/env sh
# Start the GeistAI server (Linux / macOS).
cd "$(dirname "$0")"
[ -x .venv/bin/python ] || { echo "Run ./setup.sh first."; exit 1; }
exec .venv/bin/python -m geist_server "$@"

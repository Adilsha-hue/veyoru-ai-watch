#!/bin/sh
# VEYORU lab launcher (macOS/Linux) - ./start_server.sh
# Env: LLM_PROVIDER=auto|groq|gemini|ollama|openai|local, PORT, HOST
# Free cloud: GROQ_API_KEY="gsk_..." LLM_PROVIDER=groq ./start_server.sh
set -e
cd "$(dirname "$0")"
PORT="${PORT:-8000}"
HOST="${HOST:-0.0.0.0}"
exec python3 server.py --host "$HOST" --port "$PORT"

#!/bin/sh
# VEYORU lab launcher (macOS/Linux) - ./start_server.sh
cd "$(dirname "$0")"
python3 server.py --port 8000

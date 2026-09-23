# Interactive watch workflow

1. Start `start_server.bat` and open `http://localhost:8000` in desktop Chrome or Edge.
2. Home shows the live time and charge percentage in the center. Tap **OPEN ASSISTANT** or swipe left/right to change pages.
3. On Assistant, tap **TAP TO LISTEN** or **Listen / Ask**. Chrome speech input is used when available; otherwise the typed question is used.
4. For a local demo, leave the gateway field blank or pick OFFLINE mode. For a real model, run the gateway with a free key (`GROQ_API_KEY` + `LLM_PROVIDER=groq`) or local Ollama (`LLM_PROVIDER=ollama`) and enter `http://localhost:8000/api/assistant`. `GET /api/health` shows the active provider.
5. Connect the ESP32-S3 and press **Send current screen**. The same render state is sent over USB CDC.

The ESP32-S3 is the right place for wake-word and short offline commands, not for a large hosted LLM. Espressif's ESP-SR stack provides WakeNet and MultiNet for this class of device; the gateway then handles the larger assistant model. Keep the API key on the gateway computer, never in firmware or browser JavaScript.

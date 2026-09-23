# WINDOWS + ESP32-S3 prototype workflow (watch + AI on wrist)

Goal: blank website -> plug board into laptop -> watch face appears ->
tap to listen -> free AI replies on browser + board. Laptop hotspot for Phase 2.

## 0. What you need
- Windows 10/11 laptop, Chrome or Edge (Web Serial needs it), data USB-C cable
- Any ESP32-S3 board (generic works serial-only; Waveshare 1.43" AMOLED for round face)
- Python 3.10+ (`py -3 --version`), Arduino IDE 2.x
- Free key (pick one): Groq `gsk_...` at https://console.groq.com (recommended)

## 1. Board driver (Windows)
1. Plug board in. Open Device Manager > Ports (COM & LPT).
2. `USB Serial Device (COMx)` = native CDC, done. `CP210x` = install Silicon Labs CP210x driver. `CH343 / USB-SERIAL` = install WCH CH34x driver.
3. Remember COMx. Unplug for now.

## 2. Flash the Waveshare 1.43" firmware
The complete AMOLED driver and hybrid assistant live in `firmware/src`. Use PlatformIO:
```powershell
cd firmware
pio run
pio run --target upload --upload-port COM15
pio device monitor --baud 115200 --port COM15
```
Replace `COM15` with the board port shown by Device Manager. If upload mode is not detected, hold BOOT, tap RESET, release BOOT, then upload again. Serial should show `VEYORU:BOOT` and `VEYORU:READY`.

## 3. Run website (blank -> connected)
```powershell
cd C:\path\to\veyoru-ai-watch
py -3 server.py --port 8000
# or double-click start_server.bat
```
1. Chrome opens `http://localhost:8000` (must be localhost, not file://, not server-IP).
2. You see a black round preview + `Connect board to wake display`. This is intentional.
3. Click `Connect ESP32-S3` > pick COMx > `Send current screen`.
4. Board Serial prints `VEYORU:SHOW ...` + `VEYORU:OK render`. Site dot turns green.
5. `Run browser demo` cycles home/assistant/result/health/focus/charge on both.

## 4. Free AI listen flow
```powershell
$env:GROQ_API_KEY="gsk_paste_here"; $env:LLM_PROVIDER="groq"; py -3 server.py --port 8000
```
1. `curl http://localhost:8000/api/health` shows `provider: groq`.
2. On page, LLM gateway field = `http://localhost:8000/api/assistant`.
3. Click `Listen / Ask` or tap orb > speak into laptop mic (typed box used if no mic) > `AI reply` screen shows Groq answer, also sent over USB to board.
4. No key or no internet? AUTO falls back to the on-board/offline command engine for time, battery, focus, greeting, and help.

## 5. Phone hotspot → online AI
1. Turn on the phone hotspot and connect the laptop to it.
2. Start `server.py`; it listens on `0.0.0.0` so devices on the hotspot can reach it. Allow Python on Private networks if Windows asks.
3. Run `ipconfig` and find the laptop Wi-Fi IPv4 address, for example `192.168.43.120`.
4. Connect the watch by USB. In the website's **Phone hotspot → ESP32-S3** box enter the hotspot name, password, and `http://192.168.43.120:8000/api/assistant`.
5. Click **Connect watch Wi-Fi**. The firmware first tries that server for a cloud answer; if Wi-Fi or the server fails it immediately returns to offline mode.

The API key stays on the laptop. Do not put a Groq/OpenAI key in ESP firmware.

## 6. If stuck
- `Web Serial is unavailable` = use desktop Chrome/Edge + localhost URL.
- `Connection cancelled` = wrong COM or charge-only cable. Try data cable + BOOT/RESET.
- `gateway 502` = key missing/invalid or offline. Check `/api/health`, try `LLM_PROVIDER=local`.
- Website stays black = connect the board and accept the Web Serial port picker.
- Board says `VEYORU:WIFI failed` = verify hotspot SSID/password and use a 2.4 GHz-compatible hotspot.
- Cloud fails but offline works = verify the laptop and board are on the same hotspot and use the laptop IPv4 address, never `localhost`, in the board server field.

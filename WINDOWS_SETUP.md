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

## 2. Flash firmware
1. Arduino IDE > Boards Manager > install Espressif `esp32` 3.3.0+.
2. Library Manager > install `lvgl` 9.2.2 (only needed if round AMOLED enabled).
3. Open `firmware/VEYORU_Display_Bridge/VEYORU_Display_Bridge.ino`.
4. Generic S3: board `ESP32S3 Dev Module`, USB CDC On Boot Enabled, USB Mode Hardware CDC and JTAG, PSRAM Enabled, Flash 16MB (or 4MB match), Partition Default.
5. Waveshare 1.43": board `Waveshare ESP32-S3-Touch-AMOLED-1.43`, same CDC settings, set `DISPLAY_WAVESHARE_AMOLED_1_43 1` in `config.h`.
6. Upload. No port? Hold BOOT, tap RESET, release BOOT, pick new COM, upload, tap RESET.
7. Tools > Serial Monitor 115200: expect `VEYORU:BOOT` then `VEYORU:READY controller=SH8601 protocol=1`.

## 3. Run website (blank -> connected)
```powershell
cd C:\path\to\veyoru-ai-watch
py -3 server.py --port 8000
# or double-click start_server.bat
```
1. Chrome opens `http://localhost:8000` (must be localhost, not file://, not server-IP).
2. You see blank round preview + `Preview mode · board not connected`. Normal.
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
4. No key? Same flow returns `LOCAL REPLY` demo so interaction still works.

## 5. Laptop hotspot prototype (no router)
1. Windows Settings > Mobile hotspot ON, name e.g. `VEYORU-LAB`, note laptop WiFi IP (`ipconfig`).
2. For LAN share: `py -3 server.py --port 8000 --host 0.0.0.0`, allow firewall.
3. Other device on hotspot uses `http://<laptop-ip>:8000/api/assistant`.
4. Board standalone (Phase 2): set `ENABLE_WIFI_GROQ 1` + SSID/PASS + key in `config.h`, re-upload. Board asks Groq over hotspot directly.

## 6. If stuck
- `Web Serial is unavailable` = use desktop Chrome/Edge + localhost URL.
- `Connection cancelled` = wrong COM or charge-only cable. Try data cable + BOOT/RESET.
- `gateway 502` = key missing/invalid or offline. Check `/api/health`, try `LLM_PROVIDER=local`.
- Faces show on site but not board = generic S3 serial-only until AMOLED flag + driver wired. Serial `VEYORU:SHOW` proves link is fine.

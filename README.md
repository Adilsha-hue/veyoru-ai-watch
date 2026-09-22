# VEYORU ESP32 Display Lab

This package gives you a fast edit-preview-send loop for the Waveshare ESP32-S3 Touch AMOLED 1.43 (466 x 466).

## What it does

- Previews the same round VEYORU interface in the browser.
- Connects to the ESP32-S3 over USB CDC with Web Serial.
- Runs a small safe display language in the built-in editor.
- Sends one-line JSON commands to the firmware without reflashing every UI edit.
- Includes six screen presets: home, assistant, result, health, focus, and charge.
- Supports live time and charge in the center of the home screen, tap-to-listen, browser speech input when available, and left/right swipe navigation.
- Includes an optional local `/api/assistant` gateway. The browser never stores an API key; the gateway can call a hosted LLM only when `OPENAI_API_KEY` is set on the computer.

## First-time setup

1. Install Arduino IDE 2.x.
2. Install the Espressif `esp32` board package version 3.3.0 or newer.
3. Install `lvgl` version 9.2.2 from Library Manager.
4. Open `firmware/VEYORU_Display_Bridge/VEYORU_Display_Bridge.ino`.
5. Select **Waveshare ESP32-S3-Touch-AMOLED-1.43**.
6. Set **USB CDC On Boot = Enabled**, **USB Mode = Hardware CDC and JTAG**, PSRAM enabled, 16 MB flash, and the 3 MB app / 9 MB FAT partition option.
7. Connect the board with a data-capable USB-C cable and upload.
8. If no port appears the first time: hold BOOT, tap RESET, release BOOT, select the new port, and upload again.

### Prebuilt image (fastest)

The `prebuilt` folder contains:

- `VEYORU_firmware.factory.bin` — combined bootloader, partition table and application image; flash at address `0x0`.
- `VEYORU_firmware.bin` — application-only image; flash at address `0x10000` when the matching bootloader and partition table already exist.
- `VEYORU_firmware.elf` — symbols for debugging and simulators.

The combined image was compiled with Arduino-ESP32 3.3.12 and LVGL 9.2.2 for 16 MB flash and 8 MB OPI PSRAM. The build uses about 656 KB flash and 155 KB internal RAM.

To enter download mode, connect the board's own USB-C socket, hold BOOT, tap RESET, then release BOOT. Flash the factory image, then tap RESET once without holding BOOT.

### PlatformIO option

Open the `firmware` directory as a PlatformIO project and run **Build** or **Upload**. The included `platformio.ini` uses the stable pioarduino ESP32 platform and the same LVGL version as the Arduino sketch.

## Run the web app

Double-click `start_server.bat`, then open `http://localhost:8000` in desktop Chrome or Edge. Click **Connect ESP32-S3**, choose the board port, and press **Send current screen**.

For a full LLM reply, start the server from PowerShell with ` $env:OPENAI_API_KEY="your-key"; $env:OPENAI_MODEL="gpt-5-mini"; .\start_server.bat `, then enter `http://localhost:8000/api/assistant` in the **LLM gateway** field. If no key is set, the same endpoint returns a local demo reply so the interaction still works offline.

Do not open `index.html` directly for hardware use. Web Serial requires a secure context; localhost is treated as secure.

## VEYORU Script example

```text
screen("assistant");
title("Ask VEYORU");
body("What should we build today?");
status("LISTENING");
heartRate(74);
battery(82);
accent("#71E7FF");
send();
```

This editor is for display states, not arbitrary C++ compilation. Hardware drivers and application logic still live in the Arduino firmware.

## USB protocol

The app sends newline-delimited JSON:

```json
{"cmd":"render","screen":"assistant","title":"Ask VEYORU","body":"What should we build today?","status":"LISTENING","hr":74,"battery":82,"accent":"#71E7FF"}
```

The board responds with newline-delimited status messages beginning with `VEYORU:`.

Expected verification sequence:

```text
VEYORU:BOOT
VEYORU:READY controller=SH8601 protocol=1
VEYORU:HELLO esp32-s3 amoled-466x466 protocol=1
VEYORU:OK render
```

## Important

This is prototype firmware. The heart-rate value shown by the app is simulated. It is not medical software and does not read a physical sensor yet.

The firmware binary was compiled successfully and its flash write was hash-verified on the connected ESP32-S3. The final display/serial acknowledgement check requires the board to be reset into normal run mode after flashing.

The hardware driver files are adapted from the MIT-licensed `thelastoutpostworkshop/waveshare_esp32s3_1.43_amoled_lvgl9` project. See `firmware/VEYORU_Display_Bridge/THIRD_PARTY_LICENSE.txt`.

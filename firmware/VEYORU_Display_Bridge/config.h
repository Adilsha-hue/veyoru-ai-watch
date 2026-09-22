// VEYORU display bridge config - edit for your board, then upload.
// Generic ESP32-S3 default: USB serial render works with NO display libs.
// For Waveshare ESP32-S3-Touch-AMOLED-1.43 set DISPLAY_WAVESHARE_AMOLED_1_43 1.
#pragma once

// 1 = Waveshare 1.43" 466x466 AMOLED (SH8601 + LVGL). 0 = serial-only (any ESP32-S3).
#define DISPLAY_WAVESHARE_AMOLED_1_43 0

// USB serial baud used by index.html Web Serial panel.
#define VEYORU_BAUD 115200

// --- Optional Phase-2: standalone WiFi -> Groq free cloud (no laptop USB) ---
// 1) Set to 1, 2) fill WIFI_SSID/WIFI_PASS, 3) set GROQ_API_KEY via
//    Arduino Secrets or build flag (never commit the key).
//    Board then answers on-watch over your laptop hotspot. USB still works.
#define ENABLE_WIFI_GROQ 0
#define WIFI_SSID "VEYORU-LAB"
#define WIFI_PASS "12345678"
// Groq free model, fits watch (short replies). Get key: https://console.groq.com
#define GROQ_MODEL "llama-3.1-8b-instant"
// Keep replies tiny so they fit the 466px face.
#define GROQ_MAX_TOKENS 60

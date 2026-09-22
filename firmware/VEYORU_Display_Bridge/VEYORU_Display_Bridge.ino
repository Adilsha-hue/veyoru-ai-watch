// VEYORU_Display_Bridge - ESP32-S3 USB display bridge for the VEYORU web lab.
// Protocol (newline JSON, 115200 baud):
//   PC -> board: {"cmd":"render","screen":"assistant","title":"...","body":"...",
//                  "status":"LISTENING","hr":74,"battery":82,"accent":"#71E7FF"}
//   PC -> board: {"cmd":"hello","client":"..."}
//   board -> PC: VEYORU:BOOT / VEYORU:READY controller=SH8601 protocol=1
//                VEYORU:HELLO esp32-s3 amoled-466x466 protocol=1 / VEYORU:OK render
//
// Arduino IDE 2.x setup (Windows):
//   1. Boards Manager -> Espressif "esp32" 3.3.0+
//   2. Library Manager -> "lvgl" 9.2.2 (only if DISPLAY_WAVESHARE_AMOLED_1_43=1)
//   3. Board: "ESP32S3 Dev Module" (generic) or "Waveshare ESP32-S3-Touch-AMOLED-1.43"
//   4. USB CDC On Boot=Enabled, USB Mode=Hardware CDC and JTAG, PSRAM enabled,
//      Flash 16MB, Partition 3MB APP / 9MB FAT (or Default 4MB for generic 4MB boards)
//   5. Upload with a DATA USB-C cable. No port? Hold BOOT, tap RESET, release BOOT.
// Touch/display wiring for Waveshare board is on-board; generic S3 runs serial-only
// until you wire your own panel and flip the flag in config.h.
#include <Arduino.h>
#include "config.h"

#if DISPLAY_WAVESHARE_AMOLED_1_43
#include <lvgl.h>
// TODO: plug your Waveshare SH8601 driver here (see THIRD_PARTY_LICENSE.txt for
// the MIT reference project). The bridge works serial-only until then; the web
// preview in index.html is the reference renderer.
#endif

#if ENABLE_WIFI_GROQ
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#ifndef GROQ_API_KEY
#define GROQ_API_KEY ""
#endif
#endif

String gScreen = "home", gTitle = "VEYORU", gBody = "Your AI, one gesture away.";
String gStatus = "READY", gAccent = "#C9FF4B";
int gHr = 72, gBattery = 82;

// Tiny JSON string/number extractor (no extra lib needed for USB prototype).
static String jstr(const String &line, const char *key) {
  String k = String("\"") + key + String("\"");
  int i = line.indexOf(k);
  if (i < 0) return "";
  i = line.indexOf(':', i + k.length());
  if (i < 0) return "";
  i++;
  while (i < (int)line.length() && (line[i] == ' ' || line[i] == '\t')) i++;
  if (i < (int)line.length() && line[i] == '"') {
    i++;
    String out;
    while (i < (int)line.length() && line[i] != '"') {
      if (line[i] == '\\' && i + 1 < (int)line.length()) { out += line[i + 1]; i += 2; }
      else { out += line[i]; i++; }
    }
    return out;
  }
  int j = i;
  while (j < (int)line.length() && line[j] != ',' && line[j] != '}') j++;
  return line.substring(i, j);
}

static void renderState() {
#if DISPLAY_WAVESHARE_AMOLED_1_43
  // TODO: lv_label_set_text(titleLabel, gTitle.c_str()); etc.
  // Keep the same 6 presets as index.html: home/assistant/result/health/focus/charge.
#endif
  Serial.print("VEYORU:SHOW screen=");
  Serial.print(gScreen);
  Serial.print(" title=");
  Serial.print(gTitle);
  Serial.print(" status=");
  Serial.println(gStatus);
}

static void handleLine(String line) {
  line.trim();
  if (!line.length()) return;
  String cmd = jstr(line, "cmd");
  if (cmd == "hello") {
    Serial.println("VEYORU:HELLO esp32-s3 amoled-466x466 protocol=1");
    return;
  }
  if (cmd == "render") {
    String v;
    v = jstr(line, "screen");  if (v.length()) gScreen = v;
    v = jstr(line, "title");   if (v.length()) gTitle = v;
    v = jstr(line, "body");    if (v.length()) gBody = v;
    v = jstr(line, "status");  if (v.length()) gStatus = v;
    v = jstr(line, "accent");  if (v.length()) gAccent = v;
    v = jstr(line, "hr");      if (v.length()) gHr = constrain(v.toInt(), 30, 220);
    v = jstr(line, "battery"); if (v.length()) gBattery = constrain(v.toInt(), 0, 100);
    renderState();
    Serial.println("VEYORU:OK render");
    return;
  }
  Serial.println("VEYORU:ERR unknown cmd");
}

#if ENABLE_WIFI_GROQ
// Standalone WiFi prototype: watch asks Groq directly over laptop hotspot.
// Keeps prompt tiny so the reply fits the round face.
static String askGroqDirect(const String &question) {
  if (String(GROQ_API_KEY).length() == 0) return "Set GROQ_API_KEY to enable AI.";
  WiFiClientSecure client;
  client.setInsecure();  // prototype only; use proper CA in production
  HTTPClient http;
  http.begin(client, "https://api.groq.com/openai/v1/chat/completions");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);
  String body = String("{\"model\":\"") + GROQ_MODEL +
    "\",\"max_tokens\":" + GROQ_MAX_TOKENS +
    ",\"temperature\":0.6,\"messages\":["
    "{\"role\":\"system\",\"content\":\"You are VEYORU, a concise smartwatch assistant. "
    "Reply in 1-2 short lines, max 160 characters. Plain text only.\"},"
    "{\"role\":\"user\",\"content\":\"";
  String q = question; q.replace("\\", "\\\\"); q.replace("\"", "\\\"");
  q.replace("\n", " "); body += q + "\"}]}";
  int code = http.POST(body);
  String out = "AI unavailable.";
  if (code == 200) {
    String resp = http.getString();
    int i = resp.indexOf("\"content\"");
    if (i >= 0) {
      i = resp.indexOf('"', resp.indexOf(':', i) + 1) + 1;
      int j = i;
      String txt;
      while (j < (int)resp.length() && resp[j] != '"') {
        if (resp[j] == '\\' && j + 1 < (int)resp.length()) { txt += resp[j + 1]; j += 2; }
        else { txt += resp[j]; j++; }
      }
      if (txt.length()) out = txt;
    }
  }
  http.end();
  out.replace("\\n", " ");
  if (out.length() > 160) out = out.substring(0, 160);
  return out;
}
#endif

void setup() {
  Serial.begin(VEYORU_BAUD);
  delay(400);
  Serial.println("VEYORU:BOOT");
#if DISPLAY_WAVESHARE_AMOLED_1_43
  // TODO: init SH8601 + touch + lv_init() here.
#endif
#if ENABLE_WIFI_GROQ
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(250);
#endif
  Serial.println("VEYORU:READY controller=SH8601 protocol=1");
  renderState();
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') { handleLine(line); line = ""; }
    else if (c != '\r') { line += c; if (line.length() > 1024) line = ""; }
  }
#if DISPLAY_WAVESHARE_AMOLED_1_43
  // TODO: lv_timer_handler(); delay(5);
#else
  delay(2);
#endif
}

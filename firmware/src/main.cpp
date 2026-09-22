#define LV_CONF_INCLUDE_SIMPLE
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "amoled.h"

Amoled amoled;

#define LVGL_DRAW_BUF_SIZE (DISPLAY_WIDTH * 90 * sizeof(lv_color_t))
lv_display_t *displayHandle = nullptr;
lv_color_t *drawBuffer1 = nullptr;
lv_color_t *drawBuffer2 = nullptr;

lv_obj_t *timeLabel;
lv_obj_t *batteryLabel;
lv_obj_t *orb;
lv_obj_t *titleLabel;
lv_obj_t *bodyLabel;
lv_obj_t *statusLabel;
lv_obj_t *hrLabel;

String serialLine;
String cloudServerUrl;
uint32_t accent = 0xC9FF4B;

static uint32_t tickMillis() { return millis(); }

static void flushDisplay(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
  amoled.drawArea(area->x1, area->y1, area->x2, area->y2, (uint16_t *)pxMap);
  lv_display_flush_ready(disp);
}

static void roundArea(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_INVALIDATE_AREA) return;
  lv_area_t *area = (lv_area_t *)lv_event_get_param(event);
  if (!area) return;
  area->x1 &= ~1;
  area->x2 |= 1;
  area->y1 &= ~1;
  area->y2 |= 1;
}

static lv_obj_t *makeChip(lv_obj_t *parent, int x) {
  lv_obj_t *chip = lv_label_create(parent);
  lv_obj_set_style_text_font(chip, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(chip, lv_color_hex(0xD4DDD6), 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(0x111612), 0);
  lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(chip, lv_color_hex(0x344036), 0);
  lv_obj_set_style_border_width(chip, 1, 0);
  lv_obj_set_style_radius(chip, 18, 0);
  lv_obj_set_style_pad_hor(chip, 12, 0);
  lv_obj_set_style_pad_ver(chip, 7, 0);
  lv_obj_align(chip, LV_ALIGN_BOTTOM_MID, x, -46);
  return chip;
}

static void createUi() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x020302), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  timeLabel = lv_label_create(screen);
  lv_label_set_text(timeLabel, "09:41");
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xA8B0AA), 0);
  lv_obj_align(timeLabel, LV_ALIGN_TOP_LEFT, 92, 49);

  batteryLabel = lv_label_create(screen);
  lv_label_set_text(batteryLabel, "82%");
  lv_obj_set_style_text_color(batteryLabel, lv_color_hex(0xA8B0AA), 0);
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_RIGHT, -92, 49);

  orb = lv_obj_create(screen);
  lv_obj_set_size(orb, 112, 112);
  lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_bg_opa(orb, LV_OPA_30, 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_border_width(orb, 2, 0);
  lv_obj_set_style_shadow_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_shadow_width(orb, 38, 0);
  lv_obj_set_style_shadow_opa(orb, LV_OPA_30, 0);
  lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 94);
  lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);

  titleLabel = lv_label_create(screen);
  lv_label_set_text(titleLabel, "VEYORU");
  lv_obj_set_width(titleLabel, 360);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_36, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xF4F7F2), 0);
  lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, 44);

  bodyLabel = lv_label_create(screen);
  lv_label_set_text(bodyLabel, "Your AI, one gesture away.");
  lv_obj_set_width(bodyLabel, 320);
  lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(bodyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(bodyLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(bodyLabel, lv_color_hex(0xB1BBB3), 0);
  lv_obj_align(bodyLabel, LV_ALIGN_CENTER, 0, 91);

  statusLabel = makeChip(screen, -62);
  lv_label_set_text(statusLabel, "READY");
  hrLabel = makeChip(screen, 63);
  lv_label_set_text(hrLabel, "72 BPM");
}

static String jsonString(const String &line, const char *key, const String &fallback) {
  String token = String("\"") + key + "\"";
  int start = line.indexOf(token);
  if (start < 0) return fallback;
  start = line.indexOf(':', start + token.length());
  if (start < 0) return fallback;
  start = line.indexOf('"', start + 1);
  if (start < 0) return fallback;
  int end = start + 1;
  while (end < (int)line.length()) {
    if (line[end] == '"' && line[end - 1] != '\\') break;
    end++;
  }
  if (end >= (int)line.length()) return fallback;
  String value = line.substring(start + 1, end);
  value.replace("\\n", "\n");
  value.replace("\\\"", "\"");
  return value;
}

static int jsonInt(const String &line, const char *key, int fallback) {
  String token = String("\"") + key + "\"";
  int start = line.indexOf(token);
  if (start < 0) return fallback;
  start = line.indexOf(':', start + token.length());
  if (start < 0) return fallback;
  int end = start + 1;
  while (end < (int)line.length() && (line[end] == ' ' || line[end] == '\t')) end++;
  int valueStart = end;
  while (end < (int)line.length() && (isDigit(line[end]) || line[end] == '-')) end++;
  if (end == valueStart) return fallback;
  return line.substring(valueStart, end).toInt();
}

static uint32_t hexColor(const String &value, uint32_t fallback) {
  if (value.length() != 7 || value[0] != '#') return fallback;
  return strtoul(value.substring(1).c_str(), nullptr, 16);
}

static void applyRender(const String &line) {
  String title = jsonString(line, "title", "VEYORU");
  String body = jsonString(line, "body", "Your AI, one gesture away.");
  String status = jsonString(line, "status", "READY");
  int heartRate = constrain(jsonInt(line, "hr", 72), 30, 220);
  int battery = constrain(jsonInt(line, "battery", 82), 0, 100);
  accent = hexColor(jsonString(line, "accent", "#C9FF4B"), accent);

  lv_label_set_text(titleLabel, title.c_str());
  lv_label_set_text(bodyLabel, body.c_str());
  lv_label_set_text(statusLabel, status.c_str());
  lv_label_set_text_fmt(hrLabel, "%d BPM", heartRate);
  lv_label_set_text_fmt(batteryLabel, "%d%%", battery);
  lv_obj_set_style_bg_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_shadow_color(orb, lv_color_hex(accent), 0);
  Serial.println("VEYORU:OK render");
}

static String offlineReply(String prompt) {
  prompt.toLowerCase();
  if (prompt.indexOf("battery") >= 0 || prompt.indexOf("charge") >= 0) return "Battery status is shown at the top of the display.";
  if (prompt.indexOf("time") >= 0 || prompt.indexOf("date") >= 0) return "The live time and date are available on the home screen.";
  if (prompt.indexOf("focus") >= 0 || prompt.indexOf("timer") >= 0) return "Focus mode is ready. Start with one small task.";
  if (prompt.indexOf("hello") >= 0 || prompt.indexOf("hi ") >= 0) return "Hello. VEYORU offline assistant is ready.";
  if (prompt.indexOf("help") >= 0) return "Offline I can help with time, battery, focus and simple watch controls.";
  return "I am offline. Connect Wi-Fi for a full AI answer.";
}

static String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", " ");
  return value;
}

static String cloudReply(const String &prompt) {
  if (WiFi.status() != WL_CONNECTED || cloudServerUrl.length() == 0) return "";
  HTTPClient http;
  http.setTimeout(25000);
  if (!http.begin(cloudServerUrl)) return "";
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(String("{\"text\":\"") + jsonEscape(prompt) + "\"}");
  String response = code >= 200 && code < 300 ? http.getString() : "";
  http.end();
  return jsonString(response, "answer", "");
}

static void configureWifi(const String &line) {
  String ssid = jsonString(line, "ssid", "");
  String password = jsonString(line, "password", "");
  cloudServerUrl = jsonString(line, "server", "");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("VEYORU:WIFI connected ip=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("VEYORU:WIFI failed");
  }
}

static void runOfflineAssistant(const String &line) {
  String prompt = jsonString(line, "prompt", "");
  String answer = cloudReply(prompt);
  bool cloud = answer.length() > 0;
  if (!cloud) answer = offlineReply(prompt);
  accent = cloud ? 0xC9FF4B : 0xD7A7FF;
  lv_label_set_text(titleLabel, cloud ? "Cloud AI" : "Offline AI");
  lv_label_set_text(bodyLabel, answer.c_str());
  lv_label_set_text(statusLabel, cloud ? "CLOUD AI" : "OFFLINE AI");
  lv_obj_set_style_bg_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_shadow_color(orb, lv_color_hex(accent), 0);
  Serial.print("VEYORU:ANSWER ");
  Serial.println(answer);
}

static void handleCommand(const String &line) {
  String cmd = jsonString(line, "cmd", "");
  if (cmd == "hello") {
    Serial.println("VEYORU:HELLO esp32-s3 amoled-466x466 protocol=1");
  } else if (cmd == "render") {
    applyRender(line);
  } else if (cmd == "infer") {
    runOfflineAssistant(line);
  } else if (cmd == "wifi") {
    configureWifi(line);
  } else if (cmd == "ping") {
    Serial.println("VEYORU:PONG");
  } else {
    Serial.println("VEYORU:ERROR unknown-command");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("VEYORU:BOOT");
  if (!amoled.begin()) {
    Serial.println("VEYORU:ERROR display-init");
    while (true) delay(1000);
  }

  lv_init();
  lv_tick_set_cb(tickMillis);
  drawBuffer1 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  drawBuffer2 = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!drawBuffer1 || !drawBuffer2) {
    Serial.println("VEYORU:ERROR lvgl-buffer");
    while (true) delay(1000);
  }

  displayHandle = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_display_set_flush_cb(displayHandle, flushDisplay);
  lv_display_set_buffers(displayHandle, drawBuffer1, drawBuffer2, LVGL_DRAW_BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(displayHandle, roundArea, LV_EVENT_INVALIDATE_AREA, nullptr);
  createUi();
  Serial.printf("VEYORU:READY controller=%s protocol=1\n", amoled.name());
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialLine.trim();
      if (serialLine.length()) handleCommand(serialLine);
      serialLine = "";
    } else if (c != '\r' && serialLine.length() < 1024) {
      serialLine += c;
    }
  }
  lv_timer_handler();
  delay(5);
}




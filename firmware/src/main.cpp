#define LV_CONF_INCLUDE_SIMPLE
#include <Arduino.h>
#include <lvgl.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "amoled.h"
#include "offline_tools.h"

Amoled amoled;
OfflineTools offlineTools;

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
lv_obj_t *homeLayer;
lv_obj_t *assistantLayer;
lv_obj_t *activityLabel;
lv_obj_t *stepsLabel;
lv_obj_t *dateLabel;

String serialLine;
String cloudServerUrl;
uint32_t accent = 0xC9FF4B;
String uiScreen = "home";
String uiTitle = "VEYORU";
String uiBody = "Your AI, one gesture away.";
String uiStatus = "READY";
int uiHeartRate = 72;
int uiBattery = 82;
int uiSteps = 8560;
int uiMove = 68;
int uiVoltageMv = 0;
bool externalPower = false;
String powerLabel = "82%";
bool wifiConfigured = false;
bool wifiFailureReported = false;
wl_status_t lastWifiStatus = WL_NO_SHIELD;
uint32_t wifiStartedAt = 0;

struct AssistantReply {
  String answer;
  String mode;
  String provider;
  int httpCode = 0;

  bool isOnline() const {
    return answer.length() > 0 && mode != "offline" && mode != "local-demo" && mode != "error";
  }
};

struct ChatTurn {
  String user;
  String assistant;
};

static constexpr int CHAT_MEMORY_TURNS = 4;
ChatTurn chatMemory[CHAT_MEMORY_TURNS];
int chatMemoryCount = 0;

static void rememberTurn(String user, String assistant) {
  user = user.substring(0, 180);
  assistant = assistant.substring(0, 240);
  if (chatMemoryCount == CHAT_MEMORY_TURNS) {
    for (int i = 1; i < CHAT_MEMORY_TURNS; ++i) chatMemory[i - 1] = chatMemory[i];
    --chatMemoryCount;
  }
  chatMemory[chatMemoryCount++] = {user, assistant};
}

static String conversationContext() {
  String context;
  for (int i = 0; i < chatMemoryCount; ++i) {
    context += "User: " + chatMemory[i].user + "\nVEYORU: " + chatMemory[i].assistant + "\n";
  }
  return context;
}

static void clearConversation() {
  for (int i = 0; i < CHAT_MEMORY_TURNS; ++i) chatMemory[i] = {};
  chatMemoryCount = 0;
}

static int batteryPercentFromMv(int millivolts) {
  if (millivolts >= 4200) return 100;
  if (millivolts >= 4000) return 75 + (millivolts - 4000) * 25 / 200;
  if (millivolts >= 3800) return 40 + (millivolts - 3800) * 35 / 200;
  if (millivolts >= 3600) return 15 + (millivolts - 3600) * 25 / 200;
  if (millivolts >= 3300) return (millivolts - 3300) * 15 / 300;
  return 0;
}

static void updatePowerStatus() {
  uint32_t total = 0;
  for (int i = 0; i < 12; ++i) total += analogReadMilliVolts(PIN_NUM_BAT_ADC);
  uiVoltageMv = (int)(total / 12U) * 3;  // Waveshare board uses a 3:1 divider.
  if (uiVoltageMv < 2500 || uiVoltageMv > 6000) {
    // No usable battery-sense voltage. The development board is currently
    // running from its USB connection, so avoid showing a fake percentage.
    powerLabel = "USB";
    return;
  }
  externalPower = uiVoltageMv > 4400;
  if (externalPower) {
    powerLabel = "USB " + String(uiVoltageMv / 1000.0f, 1) + "V";
  } else {
    uiBattery = batteryPercentFromMv(uiVoltageMv);
    powerLabel = String(uiBattery) + "%  " + String(uiVoltageMv / 1000.0f, 1) + "V";
  }
}

static uint32_t tickMillis() { return millis(); }
static String jsonEscape(String value);

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

static lv_obj_t *makeLayer(lv_obj_t *parent) {
  lv_obj_t *layer = lv_obj_create(parent);
  lv_obj_set_size(layer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  lv_obj_center(layer);
  lv_obj_set_style_bg_opa(layer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(layer, 0, 0);
  lv_obj_set_style_pad_all(layer, 0, 0);
  lv_obj_clear_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  return layer;
}

lv_obj_t *hrArc = nullptr;
lv_obj_t *moveArc = nullptr;
lv_obj_t *stepsArc = nullptr;

static int hrToRing(int bpm) {
  if (bpm < 30) bpm = 30;
  if (bpm > 220) bpm = 220;
  return 8 + (bpm - 30) * 92 / 190;
}

static lv_obj_t *makeMetricRing(lv_obj_t *parent, int x, const char *caption, int value, lv_obj_t **arcOut) {
  lv_obj_t *ring = lv_arc_create(parent);
  lv_obj_set_size(ring, 104, 104);
  lv_obj_align(ring, LV_ALIGN_CENTER, x, 48);
  lv_arc_set_rotation(ring, 135);
  lv_arc_set_bg_angles(ring, 0, 270);
  lv_arc_set_range(ring, 0, 100);
  lv_arc_set_value(ring, value);
  lv_obj_remove_style(ring, nullptr, LV_PART_KNOB);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(ring, lv_color_hex(0x12352B), LV_PART_MAIN);
  lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(ring, lv_color_hex(0x35F2A1), LV_PART_INDICATOR);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *label = lv_label_create(ring);
  lv_label_set_text(label, caption);
  lv_obj_set_width(label, 84);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xE8FFF5), 0);
  lv_obj_center(label);
  if (arcOut) *arcOut = ring;
  return label;
}

static void createUi() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x020302), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  homeLayer = makeLayer(screen);
  assistantLayer = makeLayer(screen);

  lv_obj_t *onlineDot = lv_obj_create(homeLayer);
  lv_obj_set_size(onlineDot, 12, 12);
  lv_obj_set_style_radius(onlineDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(onlineDot, lv_color_hex(0x35F2A1), 0);
  lv_obj_set_style_bg_opa(onlineDot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(onlineDot, 0, 0);
  lv_obj_set_style_shadow_color(onlineDot, lv_color_hex(0x35F2A1), 0);
  lv_obj_set_style_shadow_width(onlineDot, 16, 0);
  lv_obj_align(onlineDot, LV_ALIGN_TOP_MID, 0, 25);

  dateLabel = lv_label_create(homeLayer);
  lv_label_set_text(dateLabel, "TUE 23 SEP");
  lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_letter_space(dateLabel, 3, 0);
  lv_obj_set_style_text_color(dateLabel, lv_color_hex(0x65B892), 0);
  lv_obj_align(dateLabel, LV_ALIGN_TOP_MID, 0, 52);

  timeLabel = lv_label_create(homeLayer);
  lv_label_set_text(timeLabel, "10:09");
  lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_36, 0);
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xF4FFF9), 0);
  lv_obj_align(timeLabel, LV_ALIGN_TOP_MID, 0, 71);

  batteryLabel = lv_label_create(homeLayer);
  lv_label_set_text(batteryLabel, "82%");
  lv_obj_set_style_text_font(batteryLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(batteryLabel, lv_color_hex(0xA9FFD5), 0);
  lv_obj_set_style_bg_color(batteryLabel, lv_color_hex(0x07150F), 0);
  lv_obj_set_style_bg_opa(batteryLabel, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(batteryLabel, lv_color_hex(0x28513E), 0);
  lv_obj_set_style_border_width(batteryLabel, 1, 0);
  lv_obj_set_style_radius(batteryLabel, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(batteryLabel, 10, 0);
  lv_obj_set_style_pad_ver(batteryLabel, 6, 0);
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_RIGHT, -76, 22);

  hrLabel = makeMetricRing(homeLayer, -120, "72\nHEART", hrToRing(72), &hrArc);
  activityLabel = makeMetricRing(homeLayer, 0, "68%\nMOVE", 68, &moveArc);
  stepsLabel = makeMetricRing(homeLayer, 120, "8.6K\nSTEPS", 86, &stepsArc);

  lv_obj_t *wave1 = lv_obj_create(homeLayer);
  lv_obj_set_size(wave1, 380, 84);
  lv_obj_align(wave1, LV_ALIGN_BOTTOM_MID, 0, -31);
  lv_obj_set_style_radius(wave1, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(wave1, lv_color_hex(0x0A3B2A), 0);
  lv_obj_set_style_bg_opa(wave1, LV_OPA_70, 0);
  lv_obj_set_style_border_color(wave1, lv_color_hex(0x26E89A), 0);
  lv_obj_set_style_border_width(wave1, 1, 0);
  lv_obj_clear_flag(wave1, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *homeAction = lv_label_create(homeLayer);
  lv_label_set_text(homeAction, "ASK VEYORU");
  lv_obj_set_style_text_font(homeAction, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_letter_space(homeAction, 2, 0);
  lv_obj_set_style_text_color(homeAction, lv_color_hex(0x7CFFD0), 0);
  lv_obj_align(homeAction, LV_ALIGN_BOTTOM_MID, 0, -61);

  lv_obj_t *assistantDot = lv_obj_create(assistantLayer);
  lv_obj_set_size(assistantDot, 10, 10);
  lv_obj_set_style_radius(assistantDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(assistantDot, lv_color_hex(0x35F2A1), 0);
  lv_obj_set_style_bg_opa(assistantDot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(assistantDot, 0, 0);
  lv_obj_align(assistantDot, LV_ALIGN_TOP_MID, 0, 27);

  titleLabel = lv_label_create(assistantLayer);
  lv_label_set_text(titleLabel, "VEYORU AI");
  lv_obj_set_width(titleLabel, 360);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_letter_space(titleLabel, 2, 0);
  lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xF4FFF9), 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 48);

  orb = lv_obj_create(assistantLayer);
  lv_obj_set_size(orb, 122, 122);
  lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_bg_opa(orb, LV_OPA_30, 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_border_width(orb, 2, 0);
  lv_obj_set_style_shadow_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_shadow_width(orb, 38, 0);
  lv_obj_set_style_shadow_opa(orb, LV_OPA_30, 0);
  lv_obj_align(orb, LV_ALIGN_TOP_MID, 0, 92);
  lv_obj_clear_flag(orb, LV_OBJ_FLAG_SCROLLABLE);

  statusLabel = makeChip(assistantLayer, 0);
  lv_label_set_text(statusLabel, "LISTENING");
  lv_obj_align(statusLabel, LV_ALIGN_CENTER, 0, 19);

  bodyLabel = lv_label_create(assistantLayer);
  lv_label_set_text(bodyLabel, "Tap or ask a question. This conversation stays open.");
  lv_obj_set_width(bodyLabel, 350);
  lv_label_set_long_mode(bodyLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(bodyLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(bodyLabel, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(bodyLabel, lv_color_hex(0xB1BBB3), 0);
  lv_obj_align(bodyLabel, LV_ALIGN_CENTER, 0, 105);

  lv_obj_t *chatHint = lv_label_create(assistantLayer);
  lv_label_set_text(chatHint, "CHAT STAYS OPEN  /  ASK AGAIN");
  lv_obj_set_style_text_font(chatHint, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_letter_space(chatHint, 1, 0);
  lv_obj_set_style_text_color(chatHint, lv_color_hex(0x5E8B76), 0);
  lv_obj_align(chatHint, LV_ALIGN_BOTTOM_MID, 0, -40);

  lv_obj_add_flag(assistantLayer, LV_OBJ_FLAG_HIDDEN);
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

static void reportUiState() {
  char accentHex[8];
  snprintf(accentHex, sizeof(accentHex), "#%06lX", (unsigned long)(accent & 0xFFFFFF));
  String screen = jsonEscape(uiScreen);
  String title = jsonEscape(uiTitle);
  String body = jsonEscape(uiBody);
  String status = jsonEscape(uiStatus);
  Serial.printf(
      "VEYORU:STATE {\"screen\":\"%s\",\"title\":\"%s\",\"body\":\"%s\","
      "\"status\":\"%s\",\"hr\":%d,\"battery\":%d,\"steps\":%d,\"move\":%d,\"power\":\"%s\",\"voltage_mv\":%d,\"accent\":\"%s\"}\n",
      screen.c_str(), title.c_str(), body.c_str(), status.c_str(),
      uiHeartRate, uiBattery, uiSteps, uiMove, jsonEscape(powerLabel).c_str(), uiVoltageMv, accentHex);
}

static void refreshUiFromState() {
  // Keep every metric bounded and display-safe even if a remote renderer sends
  // malformed or fractional-looking data.
  uiHeartRate = constrain(uiHeartRate, 30, 220);
  uiBattery = constrain(uiBattery, 0, 100);
  uiSteps = constrain(uiSteps, 0, 99999);
  uiMove = constrain(uiMove, 0, 100);
  // Home keeps the activity rings. Every other screen (assistant, result,
  // health, focus, charge) shows its title/body on the message layer so no
  // render state is ever silently dropped on the round face.
  bool assistant = uiScreen != "home";
  if (assistant) {
    lv_obj_add_flag(homeLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(assistantLayer, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(homeLayer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(assistantLayer, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(titleLabel, uiTitle.c_str());
  lv_label_set_text(bodyLabel, uiBody.c_str());
  lv_label_set_text(statusLabel, uiStatus.c_str());
  lv_label_set_text_fmt(hrLabel, "%d\nHEART", uiHeartRate);
  if (hrArc) lv_arc_set_value(hrArc, hrToRing(uiHeartRate));
  if (moveArc) lv_arc_set_value(moveArc, uiMove);
  if (stepsArc) lv_arc_set_value(stepsArc, constrain(uiSteps * 100 / 10000, 0, 100));
  lv_label_set_text_fmt(activityLabel, "%d%%\nMOVE", uiMove);
  char stepsText[12];
  if (uiSteps >= 1000) {
    snprintf(stepsText, sizeof(stepsText), "%.1fK", uiSteps / 1000.0f);
  } else {
    snprintf(stepsText, sizeof(stepsText), "%d", uiSteps);
  }
  lv_label_set_text_fmt(stepsLabel, "%s\nSTEPS", stepsText);
  lv_label_set_text(batteryLabel, powerLabel.c_str());
  lv_obj_set_style_bg_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_border_color(orb, lv_color_hex(accent), 0);
  lv_obj_set_style_shadow_color(orb, lv_color_hex(accent), 0);
}

static void applyRender(const String &line) {
  uiScreen = jsonString(line, "screen", uiScreen);
  uiTitle = jsonString(line, "title", uiTitle);
  uiBody = jsonString(line, "body", uiBody);
  uiStatus = jsonString(line, "status", uiStatus);
  uiHeartRate = constrain(jsonInt(line, "hr", uiHeartRate), 30, 220);
  uiBattery = constrain(jsonInt(line, "battery", uiBattery), 0, 100);
  uiSteps = constrain(jsonInt(line, "steps", uiSteps), 0, 99999);
  uiMove = constrain(jsonInt(line, "move", uiMove), 0, 100);
  accent = hexColor(jsonString(line, "accent", "#C9FF4B"), accent);

  refreshUiFromState();
  Serial.println("VEYORU:OK render");
  reportUiState();
}

static String offlineReply(String prompt, bool *handled = nullptr) {
  prompt.toLowerCase();
  auto found = [&](const char *word) { return prompt.indexOf(word) >= 0; };
  auto answer = [&](const String &text) {
    if (handled) *handled = true;
    return text;
  };
  if (handled) *handled = false;
  if (found("battery") || found("charge") || found("power")) {
    return answer("Power status: " + powerLabel + ".");
  }
  if (found("health") || found("heart") || found("pulse") || found("bpm")) {
    return answer("Health sensors are not installed yet. The displayed " + String(uiHeartRate) + " BPM value is a demo.");
  }
  if (found("location") || found("where am i") || found("gps") || found("navigate")) {
    return answer("Location is unavailable offline until GPS or a phone location link is added.");
  }
  if (found("step") || found("walk") || found("activity") || found("move")) {
    return answer("Activity sensing will be enabled with the motion sensor. Current steps are demonstration data.");
  }
  if (found("time") || found("date") || found("day")) {
    return answer("It is " + offlineTools.timeText() + " on " + offlineTools.dateText() + ".");
  }
  if (found("wifi") || found("internet") || found("connection") || found("online")) {
    return answer(WiFi.status() == WL_CONNECTED ? "Wi-Fi is connected. Online AI is available." : "Wi-Fi is disconnected. VEYORU is using offline controls.");
  }
  if (found("weather") || found("temperature outside") || found("forecast")) {
    return answer("Weather needs internet or a local weather sensor. It is not available offline yet.");
  }
  if (found("focus") || found("timer") || found("countdown")) {
    return answer("Focus mode is ready. Timer controls will remain available without internet.");
  }
  if (found("hello") || prompt.startsWith("hi") || found("good morning") || found("good evening")) {
    return answer("Hello. VEYORU offline assistant is ready.");
  }
  if (found("your name") || found("who are you")) {
    return answer("I am VEYORU, your on-device watch assistant.");
  }
  if (found("help") || found("what can you do") || found("offline command")) {
    return answer("Offline I handle battery, health availability, location availability, time, activity, focus, Wi-Fi and watch controls.");
  }
  return "I am offline. Connect Wi-Fi for a full AI answer.";
}

static String jsonEscape(String value) {
  value.replace("\\", "\\\\");
  value.replace("\"", "\\\"");
  value.replace("\n", " ");
  value.replace("\r", " ");
  value.replace("\t", " ");
  return value;
}

static const char *wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "ssid-not-found";
    case WL_CONNECT_FAILED: return "auth-failed";
    case WL_CONNECTION_LOST: return "connection-lost";
    case WL_DISCONNECTED: return "disconnected";
    case WL_IDLE_STATUS: return "connecting";
    default: return "unknown";
  }
}

static void reportDeviceStatus() {
  wl_status_t status = WiFi.status();
  String ip = status == WL_CONNECTED ? WiFi.localIP().toString() : "none";
  String server = cloudServerUrl.length() ? cloudServerUrl : "none";
  Serial.printf("VEYORU:STATUS ready=1 wifi=%s ip=%s server=%s\n",
                wifiStatusName(status), ip.c_str(), server.c_str());
}

static AssistantReply cloudReply(const String &prompt, const String &context) {
  AssistantReply reply;
  if (WiFi.status() != WL_CONNECTED || cloudServerUrl.length() == 0) return reply;
  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(cloudServerUrl)) {
    reply.mode = "error";
    return reply;
  }
  http.addHeader("Content-Type", "application/json");
  String requestText = prompt;
  if (context.length()) {
    requestText = "Conversation so far:\n" + context + "\nCurrent user: " + prompt;
  }
  // Put the short ESP-side session into text as well as owning it locally.
  // This keeps follow-ups working with both old and context-aware laptop servers.
  reply.httpCode = http.POST(String("{\"text\":\"") + jsonEscape(requestText) + "\"}");
  String response = reply.httpCode >= 200 && reply.httpCode < 300 ? http.getString() : "";
  http.end();
  reply.answer = jsonString(response, "answer", "");
  reply.mode = jsonString(response, "mode", reply.answer.length() ? "unknown" : "error");
  reply.provider = jsonString(response, "provider", "unknown");
  Serial.printf("VEYORU:AI mode=%s provider=%s http=%d\n",
                reply.mode.c_str(), reply.provider.c_str(), reply.httpCode);
  return reply;
}

static void configureWifi(const String &line) {
  String ssid = jsonString(line, "ssid", "");
  String password = jsonString(line, "password", "");
  cloudServerUrl = jsonString(line, "server", "");
  ssid.trim();
  cloudServerUrl.trim();
  if (!ssid.length()) {
    Serial.println("VEYORU:WIFI failed reason=missing-ssid");
    return;
  }
  if ((!cloudServerUrl.startsWith("http://") && !cloudServerUrl.startsWith("https://")) ||
      cloudServerUrl.indexOf('<') >= 0 || cloudServerUrl.indexOf('>') >= 0) {
    Serial.println("VEYORU:WIFI failed reason=invalid-server-url");
    return;
  }
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  wifiConfigured = true;
  wifiFailureReported = false;
  wifiStartedAt = millis();
  lastWifiStatus = WL_IDLE_STATUS;
  Serial.printf("VEYORU:WIFI connecting ssid=%s server=%s\n", ssid.c_str(), cloudServerUrl.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
}

static void updateWifiStatus() {
  if (!wifiConfigured) return;
  wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED && lastWifiStatus != WL_CONNECTED) {
    Serial.printf("VEYORU:WIFI connected ip=%s rssi=%d server=%s\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI(), cloudServerUrl.c_str());
    wifiFailureReported = false;
  } else if (lastWifiStatus == WL_CONNECTED && status != WL_CONNECTED) {
    Serial.printf("VEYORU:WIFI disconnected reason=%s\n", wifiStatusName(status));
  }
  if (status != WL_CONNECTED && !wifiFailureReported && millis() - wifiStartedAt >= 20000) {
    Serial.printf("VEYORU:WIFI failed reason=%s\n", wifiStatusName(status));
    wifiFailureReported = true;
  }
  lastWifiStatus = status;
}

static void runOfflineAssistant(const String &line) {
  String prompt = jsonString(line, "prompt", "");
  String requestedMode = jsonString(line, "mode", "auto");
  requestedMode.toLowerCase();
  bool forceOffline = requestedMode == "offline";
  bool forceCloud = requestedMode == "cloud";
  String normalized = prompt;
  normalized.toLowerCase();
  normalized.trim();
  if (normalized == "new conversation" || normalized == "new chat" ||
      normalized == "clear conversation" || normalized == "clear chat") {
    clearConversation();
    offlineTools.resetConversation();
    uiScreen = "assistant";
    uiTitle = "VEYORU AI";
    uiBody = "New conversation started. What would you like to do?";
    uiStatus = "CHAT READY";
    accent = 0x35F2A1;
    refreshUiFromState();
    reportUiState();
    Serial.println("VEYORU:CHAT cleared");
    Serial.println("VEYORU:ANSWER New conversation started. What would you like to do?");
    return;
  }
  if ((normalized == "repeat that" || normalized == "what did you say") && chatMemoryCount > 0) {
    String answer = chatMemory[chatMemoryCount - 1].assistant;
    rememberTurn(prompt, answer);
    uiScreen = "assistant";
    uiTitle = "Offline AI";
    uiBody = "YOU: " + prompt + "\n\nVEYORU: " + answer;
    uiStatus = "OFFLINE AI";
    accent = 0xD7A7FF;
    refreshUiFromState();
    reportUiState();
    Serial.println("VEYORU:ANSWER " + answer);
    return;
  }
  if ((normalized == "what did i ask" || normalized == "what was my question") && chatMemoryCount > 0) {
    String answer = "You asked: " + chatMemory[chatMemoryCount - 1].user;
    rememberTurn(prompt, answer);
    uiScreen = "assistant";
    uiTitle = "Offline AI";
    uiBody = "YOU: " + prompt + "\n\nVEYORU: " + answer;
    uiStatus = "OFFLINE AI";
    accent = 0xD7A7FF;
    refreshUiFromState();
    reportUiState();
    Serial.println("VEYORU:ANSWER " + answer);
    return;
  }
  String priorContext = conversationContext();
  String localAnswer;
  bool handledOffline = false;
  if (!forceCloud) {
    handledOffline = offlineTools.handle(prompt, uiBattery, uiHeartRate,
                                         WiFi.status() == WL_CONNECTED, powerLabel, localAnswer);
    if (!handledOffline) localAnswer = offlineReply(prompt, &handledOffline);
  }
  if (forceOffline && !handledOffline) {
    handledOffline = true;
    localAnswer = "That question needs online AI. Offline I can manage alarms, timers, reminders, tasks, time, power, and watch status.";
  }
  AssistantReply reply;
  if (!handledOffline) {
    // Show thinking immediately so the round face never looks frozen
    // during the cloud round-trip (gateway timeout is 15s max).
    uiScreen = "assistant";
    uiTitle = "VEYORU AI";
    uiBody = "YOU: " + prompt.substring(0, 90) + "\n\nVEYORU: Thinking…";
    uiStatus = "THINKING";
    accent = 0x35F2A1;
    refreshUiFromState();
    reportUiState();
    reply = cloudReply(prompt, priorContext);
  }
  bool cloud = reply.isOnline();
  String answer = handledOffline ? localAnswer : reply.answer;
  if (!answer.length() || reply.mode == "error") answer = localAnswer;
  accent = cloud ? 0xC9FF4B : 0xD7A7FF;
  // Listening, thinking, and replies all remain on one assistant screen.
  uiScreen = "assistant";
  uiTitle = cloud ? "Cloud AI" : "Offline AI";
  rememberTurn(prompt, answer);
  uiBody = "YOU: " + prompt.substring(0, 90) + "\n\nVEYORU: " + answer;
  uiStatus = cloud ? "CLOUD AI" : "OFFLINE AI";
  refreshUiFromState();
  reportUiState();
  Serial.print("VEYORU:ANSWER ");
  Serial.println(answer);
}

static void handleCommand(const String &line) {
  String cmd = jsonString(line, "cmd", "");
  if (cmd == "hello") {
    Serial.printf("VEYORU:READY controller=%s protocol=1\n", amoled.name());
    Serial.println("VEYORU:HELLO esp32-s3 amoled-466x466 protocol=1");
    reportDeviceStatus();
    reportUiState();
  } else if (cmd == "render") {
    applyRender(line);
  } else if (cmd == "infer") {
    runOfflineAssistant(line);
  } else if (cmd == "wifi") {
    configureWifi(line);
  } else if (cmd == "ping") {
    Serial.println("VEYORU:PONG");
  } else if (cmd == "status") {
    reportDeviceStatus();
  } else if (cmd == "state") {
    reportUiState();
  } else if (cmd == "set_time") {
    bool ok = offlineTools.syncClock(
        jsonInt(line, "year", 0), jsonInt(line, "month", 0), jsonInt(line, "day", 0),
        jsonInt(line, "weekday", 0), jsonInt(line, "hour", 0),
        jsonInt(line, "minute", 0), jsonInt(line, "second", 0));
    Serial.println(ok ? "VEYORU:CLOCK synced" : "VEYORU:CLOCK failed");
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
  offlineTools.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_NUM_BAT_ADC, ADC_11db);
  updatePowerStatus();
  lv_label_set_text(timeLabel, offlineTools.timeText().c_str());
  lv_label_set_text(dateLabel, offlineTools.dateText().c_str());
  Serial.printf("VEYORU:READY controller=%s protocol=1\n", amoled.name());
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      serialLine.trim();
      if (serialLine.length()) handleCommand(serialLine);
      serialLine = "";
    } else if (c != '\r' && serialLine.length() < 2048) {
      serialLine += c;
    }
  }
  updateWifiStatus();
  static uint32_t lastClockRefresh = 0;
  if (millis() - lastClockRefresh >= 1000) {
    lastClockRefresh = millis();
    lv_label_set_text(timeLabel, offlineTools.timeText().c_str());
    lv_label_set_text(dateLabel, offlineTools.dateText().c_str());
  }
  static uint32_t lastPowerRefresh = 0;
  if (millis() - lastPowerRefresh >= 10000) {
    lastPowerRefresh = millis();
    updatePowerStatus();
    lv_label_set_text(batteryLabel, powerLabel.c_str());
  }
  String alert;
  if (offlineTools.poll(alert)) {
    uiScreen = "assistant";
    uiTitle = "VEYORU ALERT";
    uiBody = alert;
    uiStatus = "OFFLINE ALERT";
    accent = 0x35F2A1;
    refreshUiFromState();
    reportUiState();
    Serial.print("VEYORU:ANSWER ");
    Serial.println(alert);
  }
  lv_timer_handler();
  delay(5);
}

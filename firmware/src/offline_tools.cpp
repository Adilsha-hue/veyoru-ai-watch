#include "offline_tools.h"

#include <time.h>
#include "driver/i2c.h"

#include "board_config.h"

static uint8_t fromBcd(uint8_t value) { return (value >> 4) * 10 + (value & 0x0F); }
static uint8_t toBcd(uint8_t value) { return ((value / 10) << 4) | (value % 10); }

static constexpr i2c_port_t RTC_I2C_PORT = I2C_NUM_0;
static bool rtcBusInit() {
  esp_err_t deleted = i2c_driver_delete(RTC_I2C_PORT);
  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = (gpio_num_t)PIN_NUM_TOUCH_SDA;
  config.scl_io_num = (gpio_num_t)PIN_NUM_TOUCH_SCL;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 300000;
  config.clk_flags = 0;
  esp_err_t configured = i2c_param_config(RTC_I2C_PORT, &config);
  esp_err_t installed = configured == ESP_OK
                            ? i2c_driver_install(RTC_I2C_PORT, config.mode, 0, 0, 0)
                            : configured;
  Serial.printf("VEYORU:I2C delete=%d config=%d install=%d\n", deleted, configured, installed);
  return configured == ESP_OK && installed == ESP_OK;
}
static bool rtcRead(uint8_t reg, uint8_t *data, size_t length) {
  return i2c_master_write_read_device(RTC_I2C_PORT, 0x51, &reg, 1, data, length,
                                      pdMS_TO_TICKS(1000)) == ESP_OK;
}
static bool rtcWrite(uint8_t reg, const uint8_t *data, size_t length) {
  uint8_t payload[8];
  if (length > sizeof(payload) - 1) return false;
  payload[0] = reg;
  memcpy(payload + 1, data, length);
  return i2c_master_write_to_device(RTC_I2C_PORT, 0x51, payload, length + 1,
                                    pdMS_TO_TICKS(1000)) == ESP_OK;
}

void OfflineTools::begin() {
  bool busReady = rtcBusInit();
  int y, mo, d, wd, h, mi, s;
  hardwareRtcReady = busReady && readClock(y, mo, d, wd, h, mi, s);
  rtcReady = hardwareRtcReady;
  Serial.printf("VEYORU:RTC ready=%d\n", hardwareRtcReady ? 1 : 0);
  prefs.begin("veyoru", false);
  load();
}

bool OfflineTools::readClock(int &year, int &month, int &day, int &weekday,
                             int &hour, int &minute, int &second) {
  uint8_t data[7];
  if (rtcRead(0x04, data, sizeof(data))) {
    uint8_t rawSecond = data[0];
    second = fromBcd(rawSecond & 0x7F);
    minute = fromBcd(data[1] & 0x7F);
    hour = fromBcd(data[2] & 0x3F);
    day = fromBcd(data[3] & 0x3F);
    weekday = fromBcd(data[4] & 0x07);
    month = fromBcd(data[5] & 0x1F);
    year = 2000 + fromBcd(data[6]);
    bool valid = !(rawSecond & 0x80) && month >= 1 && month <= 12 && day >= 1 && day <= 31 && hour < 24 && minute < 60;
    if (valid) return true;
  }
  if (!softClockReady) return false;
  time_t value = (time_t)(softBaseEpoch + (uint32_t)(millis() - softBaseMillis) / 1000U);
  struct tm current = {};
  localtime_r(&value, &current);
  year = current.tm_year + 1900; month = current.tm_mon + 1; day = current.tm_mday;
  weekday = current.tm_wday; hour = current.tm_hour; minute = current.tm_min; second = current.tm_sec;
  return true;
}

bool OfflineTools::syncClock(int year, int month, int day, int weekday,
                             int hour, int minute, int second) {
  if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) return false;
  uint8_t data[7] = {toBcd((uint8_t)second), toBcd((uint8_t)minute), toBcd((uint8_t)hour),
                     toBcd((uint8_t)day), toBcd((uint8_t)(weekday % 7)),
                     toBcd((uint8_t)month), toBcd((uint8_t)(year % 100))};
  struct tm value = {};
  value.tm_year = year - 1900; value.tm_mon = month - 1; value.tm_mday = day;
  value.tm_hour = hour; value.tm_min = minute; value.tm_sec = second; value.tm_isdst = -1;
  time_t epoch = mktime(&value);
  if (epoch <= 0) return false;
  softBaseEpoch = (uint64_t)epoch;
  softBaseMillis = millis();
  softClockReady = true;
  hardwareRtcReady = rtcWrite(0x04, data, sizeof(data));
  rtcReady = true;
  Serial.printf("VEYORU:CLOCK source=%s\n", hardwareRtcReady ? "rtc" : "software");
  return true;
}

uint64_t OfflineTools::nowSeconds() {
  int y, mo, d, wd, h, mi, s;
  if (!readClock(y, mo, d, wd, h, mi, s)) return 0;
  struct tm value = {};
  value.tm_year = y - 1900;
  value.tm_mon = mo - 1;
  value.tm_mday = d;
  value.tm_hour = h;
  value.tm_min = mi;
  value.tm_sec = s;
  value.tm_isdst = -1;
  time_t result = mktime(&value);
  return result > 0 ? (uint64_t)result : 0;
}

String OfflineTools::timeText() {
  int y, mo, d, wd, h, mi, s;
  if (!readClock(y, mo, d, wd, h, mi, s)) return "--:--";
  char out[6];
  snprintf(out, sizeof(out), "%02d:%02d", h, mi);
  return String(out);
}

String OfflineTools::dateText() {
  static const char *days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  int y, mo, d, wd, h, mi, s;
  if (!readClock(y, mo, d, wd, h, mi, s)) return "RTC NOT SET";
  char out[16];
  snprintf(out, sizeof(out), "%s %02d %s", days[wd % 7], d, months[mo - 1]);
  return String(out);
}

void OfflineTools::load() {
  taskCount = constrain(prefs.getInt("taskCount", 0), 0, MAX_TASKS);
  for (int i = 0; i < taskCount; ++i) tasks[i] = prefs.getString(("task" + String(i)).c_str(), "");
  reminderActive = prefs.getBool("remActive", false);
  reminderDue = prefs.getULong64("remDue", 0);
  reminderText = prefs.getString("remText", "Reminder");
  alarmActive = prefs.getBool("alarmActive", false);
  alarmHour = prefs.getInt("alarmHour", 0);
  alarmMinute = prefs.getInt("alarmMinute", 0);
  alarmText = prefs.getString("alarmText", "Alarm");
}

void OfflineTools::saveTasks() {
  prefs.putInt("taskCount", taskCount);
  for (int i = 0; i < MAX_TASKS; ++i) prefs.putString(("task" + String(i)).c_str(), i < taskCount ? tasks[i] : "");
}

int OfflineTools::firstNumber(const String &text, int start) {
  while (start < (int)text.length() && !isDigit(text[start])) ++start;
  if (start >= (int)text.length()) return -1;
  int end = start;
  while (end < (int)text.length() && isDigit(text[end])) ++end;
  return text.substring(start, end).toInt();
}

String OfflineTools::cleanTask(String text) {
  text.trim();
  while (text.endsWith(".") || text.endsWith("!")) text.remove(text.length() - 1);
  if (text.length() > 80) text = text.substring(0, 80);
  return text;
}

bool OfflineTools::handle(const String &original, int battery, int heartRate,
                          bool wifiConnected, const String &powerStatus, String &answer) {
  String prompt = original;
  prompt.toLowerCase();
  prompt.trim();

  // Keep an incomplete alarm request in the same conversation. A short reply
  // such as "AM" must complete the prior turn instead of becoming a new query.
  if (pendingAlarmMeridiem) {
    bool cancel = prompt == "cancel" || prompt == "never mind" || prompt == "nevermind";
    bool am = prompt == "am" || prompt == "a.m." || prompt.indexOf("morning") >= 0;
    bool pm = prompt == "pm" || prompt == "p.m." || prompt.indexOf("afternoon") >= 0 ||
              prompt.indexOf("evening") >= 0 || prompt.indexOf("night") >= 0;
    if (cancel) {
      pendingAlarmMeridiem = false;
      answer = "Alarm setup cancelled.";
      return true;
    }
    if (!am && !pm) {
      answer = "Please say AM or PM for the " + String(pendingAlarmHour) + ":" +
               (pendingAlarmMinute < 10 ? "0" : "") + String(pendingAlarmMinute) + " alarm.";
      return true;
    }
    alarmHour = pendingAlarmHour;
    alarmMinute = pendingAlarmMinute;
    if (pm && alarmHour < 12) alarmHour += 12;
    if (am && alarmHour == 12) alarmHour = 0;
    pendingAlarmMeridiem = false;
    alarmText = "Alarm";
    alarmActive = true;
    lastAlarmDay = -1;
    prefs.putBool("alarmActive", true);
    prefs.putInt("alarmHour", alarmHour);
    prefs.putInt("alarmMinute", alarmMinute);
    prefs.putString("alarmText", alarmText);
    char formatted[12];
    snprintf(formatted, sizeof(formatted), "%d:%02d %s", pendingAlarmHour,
             pendingAlarmMinute, am ? "AM" : "PM");
    answer = "Offline alarm set for " + String(formatted) + ".";
    return true;
  }

  if (pendingTask) {
    if (prompt == "cancel" || prompt == "never mind" || prompt == "nevermind") {
      pendingTask = false;
      answer = "Task setup cancelled.";
      return true;
    }
    String task = cleanTask(original);
    if (!task.length()) {
      answer = "What task should I add?";
      return true;
    }
    pendingTask = false;
    if (taskCount >= MAX_TASKS) {
      answer = "Your offline task list is full. Complete or delete a task first.";
      return true;
    }
    tasks[taskCount++] = task;
    saveTasks();
    answer = "Added task " + String(taskCount) + ": " + task;
    return true;
  }

  if (prompt == "add task" || prompt == "add todo" || prompt == "add to do") {
    pendingTask = true;
    answer = "What task should I add?";
    return true;
  }

  int addStart = -1;
  if (prompt.startsWith("add task ")) addStart = 9;
  else if (prompt.startsWith("add a task to ")) addStart = 14;
  else if (prompt.startsWith("add a task for ")) addStart = 15;
  else if (prompt.startsWith("add todo ")) addStart = 9;
  else if (prompt.startsWith("add to do ")) addStart = 10;
  else if (prompt.startsWith("make a task to ")) addStart = 15;
  else if (prompt.startsWith("make a task for ")) addStart = 16;
  else if (prompt.startsWith("create a task to ")) addStart = 17;
  else if (prompt.startsWith("create a task for ")) addStart = 18;
  else if (prompt.startsWith("add ") && prompt.indexOf(" to my to-do") > 4) addStart = 4;
  if (addStart >= 0) {
    String task = original.substring(addStart);
    String taskLower = task;
    taskLower.toLowerCase();
    int suffix = taskLower.indexOf(" to my to-do");
    if (suffix >= 0) task = task.substring(0, suffix);
    task = cleanTask(task);
    if (!task.length()) { answer = "Tell me the task to add."; return true; }
    if (taskCount >= MAX_TASKS) { answer = "Your offline task list is full. Complete or delete a task first."; return true; }
    tasks[taskCount++] = task;
    saveTasks();
    answer = "Added task " + String(taskCount) + ": " + task;
    return true;
  }

  if ((prompt.indexOf("show") >= 0 || prompt.indexOf("list") >= 0 || prompt.indexOf("read") >= 0) &&
      (prompt.indexOf("task") >= 0 || prompt.indexOf("todo") >= 0 || prompt.indexOf("to-do") >= 0)) {
    if (!taskCount) { answer = "Your offline to-do list is empty."; return true; }
    answer = "Tasks: ";
    for (int i = 0; i < taskCount; ++i) {
      if (i) answer += "; ";
      answer += String(i + 1) + ". " + tasks[i];
    }
    return true;
  }

  if ((prompt.indexOf("complete task") >= 0 || prompt.indexOf("delete task") >= 0 || prompt.indexOf("remove task") >= 0)) {
    int number = firstNumber(prompt);
    if (number < 1 || number > taskCount) { answer = "Say a valid task number, for example complete task 1."; return true; }
    String removed = tasks[number - 1];
    for (int i = number - 1; i + 1 < taskCount; ++i) tasks[i] = tasks[i + 1];
    tasks[--taskCount] = "";
    saveTasks();
    answer = "Completed and removed: " + removed;
    return true;
  }

  bool reminderRequest = prompt.indexOf("remind me") >= 0 || prompt.startsWith("set timer") ||
                         prompt.startsWith("set a timer") || prompt.startsWith("timer ");
  int durationPos = prompt.indexOf(" in ");
  int durationSkip = 4;
  if (durationPos < 0 && (prompt.startsWith("set timer") || prompt.startsWith("set a timer"))) {
    durationPos = prompt.indexOf(" for ");
    durationSkip = 5;
  }
  if (reminderRequest && durationPos >= 0) {
    int inPos = durationPos + durationSkip;
    int amount = firstNumber(prompt, inPos);
    int multiplier = prompt.indexOf("hour", inPos) >= 0 ? 3600 : prompt.indexOf("second", inPos) >= 0 ? 1 : 60;
    uint64_t now = nowSeconds();
    if (!rtcReady || !now) { answer = "The offline clock is not set. Connect the browser once to synchronize it."; return true; }
    if (amount <= 0) { answer = "Say a duration, for example remind me in 10 minutes."; return true; }
    int toPos = prompt.indexOf(" to ", inPos);
    reminderText = toPos >= 0 ? cleanTask(original.substring(toPos + 4)) : (prompt.indexOf("timer") >= 0 ? "Timer finished" : "Reminder");
    reminderDue = now + (uint64_t)amount * multiplier;
    reminderActive = true;
    prefs.putBool("remActive", true);
    prefs.putULong64("remDue", reminderDue);
    prefs.putString("remText", reminderText);
    answer = "Offline reminder set for " + String(amount) + (multiplier == 3600 ? " hours: " : multiplier == 1 ? " seconds: " : " minutes: ") + reminderText;
    return true;
  }

  if (prompt.indexOf("set alarm") >= 0 || prompt.indexOf("set an alarm") >= 0) {
    int at = prompt.indexOf(" at ");
    if (at < 0) at = prompt.indexOf(" for ");
    int hour = firstNumber(prompt, at >= 0 ? at + 4 : 0);
    int colon = prompt.indexOf(':', at >= 0 ? at : 0);
    int minute = colon >= 0 ? firstNumber(prompt, colon + 1) : 0;
    bool pm = prompt.endsWith("pm") || prompt.endsWith("p.m.") || prompt.indexOf("evening") >= 0 || prompt.indexOf("night") >= 0;
    bool am = prompt.endsWith("am") || prompt.endsWith("a.m.") || prompt.indexOf("morning") >= 0;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      answer = "Say an alarm time, for example set alarm for 7:30 AM.";
      return true;
    }
    if (hour >= 1 && hour <= 12 && !am && !pm) {
      pendingAlarmHour = hour;
      pendingAlarmMinute = minute;
      pendingAlarmMeridiem = true;
      answer = "Is that " + String(hour) + ":" + (minute < 10 ? "0" : "") +
               String(minute) + " AM or PM?";
      return true;
    }
    if (pm && hour < 12) hour += 12;
    if (am && hour == 12) hour = 0;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) { answer = "Say an alarm time, for example set alarm for 7:30 AM."; return true; }
    alarmHour = hour;
    alarmMinute = minute;
    alarmText = "Alarm";
    alarmActive = true;
    lastAlarmDay = -1;
    prefs.putBool("alarmActive", true);
    prefs.putInt("alarmHour", alarmHour);
    prefs.putInt("alarmMinute", alarmMinute);
    prefs.putString("alarmText", alarmText);
    char formatted[6];
    snprintf(formatted, sizeof(formatted), "%02d:%02d", hour, minute);
    answer = "Offline alarm set for " + String(formatted) + ".";
    return true;
  }

  if (prompt.indexOf("cancel alarm") >= 0 || prompt.indexOf("delete alarm") >= 0) {
    alarmActive = false;
    prefs.putBool("alarmActive", false);
    answer = "Offline alarm cancelled.";
    return true;
  }

  if (prompt.indexOf("battery") >= 0 || prompt.indexOf("charge") >= 0 || prompt.indexOf("power") >= 0) {
    answer = powerStatus.length() ? "Power status: " + powerStatus + "."
                                  : "Battery is approximately " + String(battery) + " percent.";
    return true;
  }
  if (prompt.indexOf("health") >= 0 || prompt.indexOf("heart") >= 0 || prompt.indexOf("pulse") >= 0 || prompt.indexOf("bpm") >= 0) {
    answer = "Health sensors are not installed yet. The displayed " + String(heartRate) + " BPM value is a demo."; return true;
  }
  if (prompt.indexOf("location") >= 0 || prompt.indexOf("where am i") >= 0 || prompt.indexOf("gps") >= 0) {
    answer = "Location is unavailable offline until GPS or a phone location link is added."; return true;
  }
  if (prompt.indexOf("time") >= 0 || prompt.indexOf("date") >= 0 || prompt.indexOf("day") >= 0) {
    answer = rtcReady ? "It is " + timeText() + " on " + dateText() + "." : "The offline clock is not set yet."; return true;
  }
  if (prompt.indexOf("wifi") >= 0 || prompt.indexOf("internet") >= 0 || prompt.indexOf("connection") >= 0) {
    answer = wifiConnected ? "Wi-Fi is connected. Online AI is available." : "Wi-Fi is disconnected. VEYORU is using offline controls."; return true;
  }
  if (prompt.indexOf("hello") >= 0 || prompt.startsWith("hi")) { answer = "Hello. VEYORU offline assistant is ready."; return true; }
  if (prompt.indexOf("help") >= 0 || prompt.indexOf("what can you do") >= 0) {
    answer = "Offline I manage alarms, reminders, timers, to-do tasks, time, battery and watch status."; return true;
  }
  return false;
}

void OfflineTools::resetConversation() {
  pendingAlarmMeridiem = false;
  pendingTask = false;
}

bool OfflineTools::poll(String &alert) {
  if (millis() - lastPollMs < 800) return false;
  lastPollMs = millis();
  uint64_t now = nowSeconds();
  if (reminderActive && now && now >= reminderDue) {
    reminderActive = false;
    prefs.putBool("remActive", false);
    alert = "Reminder: " + reminderText;
    return true;
  }
  int y, mo, d, wd, h, mi, s;
  if (alarmActive && readClock(y, mo, d, wd, h, mi, s)) {
    int dayKey = y * 1000 + mo * 32 + d;
    if (h == alarmHour && mi == alarmMinute && dayKey != lastAlarmDay) {
      lastAlarmDay = dayKey;
      alarmActive = false;
      prefs.putBool("alarmActive", false);
      alert = alarmText + " at " + timeText();
      return true;
    }
  }
  return false;
}

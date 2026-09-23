#pragma once

#include <Arduino.h>
#include <Preferences.h>

class OfflineTools {
 public:
  void begin();
  bool syncClock(int year, int month, int day, int weekday, int hour, int minute, int second);
  bool handle(const String &prompt, int battery, int heartRate, bool wifiConnected,
              const String &powerStatus, String &answer);
  bool poll(String &alert);
  void resetConversation();
  String timeText();
  String dateText();

 private:
  static constexpr uint8_t RTC_ADDRESS = 0x51;
  static constexpr int MAX_TASKS = 6;
  Preferences prefs;
  String tasks[MAX_TASKS];
  int taskCount = 0;
  bool rtcReady = false;
  bool hardwareRtcReady = false;
  bool softClockReady = false;
  uint64_t softBaseEpoch = 0;
  uint32_t softBaseMillis = 0;
  bool reminderActive = false;
  uint64_t reminderDue = 0;
  String reminderText;
  bool alarmActive = false;
  int alarmHour = 0;
  int alarmMinute = 0;
  String alarmText;
  bool pendingAlarmMeridiem = false;
  int pendingAlarmHour = 0;
  int pendingAlarmMinute = 0;
  bool pendingTask = false;
  int lastAlarmDay = -1;
  uint32_t lastPollMs = 0;

  bool readClock(int &year, int &month, int &day, int &weekday, int &hour, int &minute, int &second);
  uint64_t nowSeconds();
  void load();
  void saveTasks();
  static int firstNumber(const String &text, int start = 0);
  static String cleanTask(String text);
};

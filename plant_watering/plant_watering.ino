/*
 * AI-assisted code with some improvements for a tailored functionality
 * Automatic Plant Watering System
 * ESP32 — 4 pumps via MOSFETs
 *
 * Schedule: 08:00–08:15, 12:00–12:15, 20:00–20:15
 * Duration: 2 minutes per pump, pumps run sequentially
 * Time check: every 15 minutes via NTP
 *
 * Wiring:
 *   Pump 1 MOSFET gate → GPIO 14
 *   Pump 2 MOSFET gate → GPIO 25
 *   Pump 3 MOSFET gate → GPIO 26
 *   Pump 4 MOSFET gate → GPIO 27
 */
#include "esp_sleep.h"
#include <WiFi.h>
#include "time.h"

// ── WiFi credentials ──────────────────────────────────────────
const char* WIFI_SSID     = "PLAY_Swiatlowodowy_9406";
const char* WIFI_PASSWORD = "YYY";

// ── NTP settings ──────────────────────────────────────────────
const char* NTP_SERVER          = "pool.ntp.org";
const long  GMT_OFFSET_SEC      = 3600;   // UTC+1 Poland (winter)
const int   DAYLIGHT_OFFSET_SEC = 3600;   // +1 for summer (CEST)

// ── Pin definitions ───────────────────────────────────────────
const int PUMP_PINS[4] = { 14, 25, 26, 27 };
const int NUM_PUMPS    = 4;

// ── Schedule: hour at which a 15-min window opens ────────────
const int WATER_HOURS[3] = { 8, 14, 20 };
const int NUM_SCHEDULES  = 3;
const int WINDOW_MINUTES = 15; // window is [HOUR:00, HOUR:15)

// ── Timing constants ──────────────────────────────────────────
const unsigned long PUMP_DURATION_MS  = 2UL * 60UL * 1000UL; // 2 min
const unsigned long CHECK_INTERVAL_MS = 15UL * 60UL * 1000UL; // check every 15 min

// ── State ─────────────────────────────────────────────────────
bool  sessionFired[NUM_SCHEDULES] = { false, false, false };
bool  isWatering      = false;
int   currentPump     = 0;
unsigned long pumpStartTime  = 0;
unsigned long lastCheckTime  = 0;

// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < NUM_PUMPS; i++) {
    pinMode(PUMP_PINS[i], OUTPUT);
    digitalWrite(PUMP_PINS[i], LOW);
  }

  connectWiFi();
  syncTime();

  // Force an immediate check on boot
  lastCheckTime = millis() - CHECK_INTERVAL_MS;
}

// ─────────────────────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── Advance pump sequence (checked every loop iteration) ──
  if (isWatering) {
    advancePumpSequence();
  }

  // ── Time check every 15 minutes ───────────────────────────
  if (now - lastCheckTime >= CHECK_INTERVAL_MS) {
    lastCheckTime = now;

    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
      Serial.println("NTP failed — retrying sync");
      syncTime();
      return;
    }

    Serial.printf("Time check: %02d:%02d\n",
                  timeInfo.tm_hour, timeInfo.tm_min);

    if (!isWatering && shouldStartWatering(timeInfo)) {
      Serial.println("Watering window detected — starting session");
      startWatering();
    }
  }
  delay(1000);
/*
  Serial.println("Sleeping 30s...");
  Serial.flush(); // make sure serial output is sent before sleep
  esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL); // 10 sec in microseconds
  esp_light_sleep_start();
*/
}

// ─────────────────────────────────────────────────────────────

/*
 * Returns true if current time falls inside a watering window
 * AND that session has not already been fired today.
 * Resets session flags once time moves outside the window.
 */
bool shouldStartWatering(struct tm& t) {
  for (int s = 0; s < NUM_SCHEDULES; s++) {
    if (inWateringWindow(t.tm_hour, t.tm_min, WATER_HOURS[s])) {
      if (!sessionFired[s]) {
        sessionFired[s] = true;
        return true;
      }
      return false; // already fired this window
    }
  }

  // Outside all windows — reset flags so next window can fire
  for (int s = 0; s < NUM_SCHEDULES; s++) {
    if (!inWateringWindow(t.tm_hour, t.tm_min, WATER_HOURS[s])) {
      sessionFired[s] = false;
    }
  }
  return false;
}

/*
 * True if (hour:minute) is inside [scheduleHour:00, scheduleHour:15)
 */
bool inWateringWindow(int hour, int minute, int scheduleHour) {
  int totalMinutes = hour * 60 + minute;
  int windowStart  = scheduleHour * 60;
  int windowEnd    = windowStart + WINDOW_MINUTES;
  return totalMinutes >= windowStart && totalMinutes < windowEnd;
}

// ─────────────────────────────────────────────────────────────

void startWatering() {
  isWatering  = true;
  currentPump = 0;
  startPump(currentPump);
}

void startPump(int index) {
  Serial.printf("Pump %d ON (GPIO %d) — 2 min\n",
                index + 1, PUMP_PINS[index]);
  digitalWrite(PUMP_PINS[index], HIGH);
  pumpStartTime = millis();
}

void stopPump(int index) {
  digitalWrite(PUMP_PINS[index], LOW);
  Serial.printf("Pump %d OFF\n", index + 1);
}

void allPumpsOff() {
  for (int i = 0; i < NUM_PUMPS; i++) {
    digitalWrite(PUMP_PINS[i], LOW);
  }
}

/*
 * Called every loop() iteration while isWatering == true.
 * Moves to next pump when current pump has run for PUMP_DURATION_MS.
 */
void advancePumpSequence() {
  if (millis() - pumpStartTime < PUMP_DURATION_MS) return;

  stopPump(currentPump);
  currentPump++;

  if (currentPump < NUM_PUMPS) {
    startPump(currentPump);
  } else {
    isWatering  = false;
    currentPump = 0;
    Serial.println("Watering session complete");
  }
}

// ─────────────────────────────────────────────────────────────

void connectWiFi() {
  Serial.printf("Connecting to: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.printf("attempt %d status: %d\n", attempts, WiFi.status());
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nConnected — IP: %s\n",
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi failed — will retry on next NTP attempt");
  }
}

void syncTime() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  struct tm timeInfo;
  if (getLocalTime(&timeInfo)) {
    Serial.printf("Time synced: %02d:%02d:%02d\n",
                  timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  } else {
    Serial.println("NTP sync failed");
  }
}

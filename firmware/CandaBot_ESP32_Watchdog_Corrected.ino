
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "esp_freertos_hooks.h"

// ============================================================
// ESP32 EMO WATCHDOG - DESKTOP COMPANION
// ESP32 DevKit V1 | SSD1306 128x64 | Arduino Core 3.x
// ============================================================

// ---------------- WIFI ----------------

const char* WIFI_SSID = "cantarona";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ---------------- VERCEL DASHBOARD -----------------

// Set this after deploying the dashboard to Vercel.
const char* TELEMETRY_URL = "https://YOUR-VERCEL-PROJECT.vercel.app/api/telemetry";
const char* DEVICE_REQUEST_URL = "https://YOUR-VERCEL-PROJECT.vercel.app/api/device-request";
const char* DEVICE_TOKEN = "YOUR_DEVICE_TOKEN";
const char* DEVICE_ID = "canda-bot";

unsigned long lastDeviceRequestCheck = 0;

// CPU load estimator: counts how often each FreeRTOS idle task gets CPU time.
// This is an approximation, not a hardware performance-counter measurement.
volatile uint32_t idleHookTicks[2] = {0, 0};
uint32_t lastIdleTicks[2] = {0, 0};
uint32_t peakIdleTicksPerSecond[2] = {1, 1};
unsigned long lastCpuSampleAt = 0;
float cpuLoadPercent = 0.0f;
bool cpuHooksReady = false;

bool IRAM_ATTR candaIdleHook() {
  const int core = xPortGetCoreID();
  if (core >= 0 && core < 2) idleHookTicks[core]++;
  return false;
}

void setupCpuLoadMonitor() {
  const int cores = ESP.getChipCores();
  for (int core = 0; core < cores && core < 2; core++) {
    esp_err_t result = esp_register_freertos_idle_hook_for_cpu(candaIdleHook, core);
    if (result != ESP_OK) {
      Serial.printf("CPU idle hook registration failed on core %d: %d\n", core, (int)result);
      return;
    }
  }
  lastCpuSampleAt = millis();
  cpuHooksReady = true;
}

void updateCpuLoad() {
  if (!cpuHooksReady) return;
  const unsigned long now = millis();
  const unsigned long elapsed = now - lastCpuSampleAt;
  if (elapsed < 1000) return;
  uint32_t current[2] = {idleHookTicks[0], idleHookTicks[1]};
  const int cores = ESP.getChipCores();
  float totalLoad = 0.0f;
  int measuredCores = 0;
  for (int core = 0; core < cores && core < 2; core++) {
    uint32_t delta = current[core] - lastIdleTicks[core];
    uint32_t normalized = (uint32_t)((double)delta * 1000.0 / (double)elapsed);
    if (normalized > peakIdleTicksPerSecond[core]) peakIdleTicksPerSecond[core] = normalized;
    float idleRatio = (float)normalized / (float)peakIdleTicksPerSecond[core];
    if (idleRatio > 1.0f) idleRatio = 1.0f;
    totalLoad += (1.0f - idleRatio) * 100.0f;
    measuredCores++;
    lastIdleTicks[core] = current[core];
  }
  if (measuredCores > 0) cpuLoadPercent = totalLoad / measuredCores;
  lastCpuSampleAt = now;
}

// ---------------- OTA -----------------

const char* OTA_HOSTNAME = "ESP32-Watchdog";
const char* OTA_PASSWORD = "YOUR_OTA_PASSWORD";

// ---------------- PINS -----------------

#define BLUE_LED_PIN   2
#define SPEAKER_1_PIN 13
#define SPEAKER_2_PIN 14

#define OLED_SDA 21
#define OLED_SCL 22

// ---------------- OLED -----------------

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

uint8_t oledAddress = 0x3C;
bool oledAvailable = false;

// ---------------- TIME -----------------

const long GMT_OFFSET_SEC = 19800;
const int DAYLIGHT_OFFSET_SEC = 0;

// ---------------- INTERVALS -----------------

const unsigned long WIFI_RECONNECT_INTERVAL = 10000;
const unsigned long INTERNET_CHECK_INTERVAL = 15000;
const unsigned long SERIAL_INTERVAL = 10000;
const unsigned long DEVICE_REQUEST_CHECK_INTERVAL = 30000UL;

const unsigned long PIXEL_SHIFT_INTERVAL = 60000;

const unsigned long AWAKE_DURATION = 30UL * 60UL * 1000UL;
const unsigned long SLEEP_DURATION = 6UL * 60UL * 1000UL;

const unsigned long ACTIVITY_INTERVAL = 6UL * 60UL * 1000UL;
const unsigned long ACTIVITY_DURATION = 5000UL;

const unsigned long CLOCK_INTERVAL = 10UL * 60UL * 1000UL;
const unsigned long CLOCK_DISPLAY_DURATION = 60000UL; // Show clock for 1 minute

// ---------------- NETWORK ALARM -----------------

const unsigned long SURPRISE_DURATION = 60000;

const unsigned long ALARM_ACTIVE_PERIOD = 60000;
const unsigned long ALARM_SILENT_PERIOD = 120000;

const unsigned long ALARM_CYCLE_PERIOD =
  ALARM_ACTIVE_PERIOD + ALARM_SILENT_PERIOD;

const unsigned long LONG_BEEP_ON = 1000;
const unsigned long LONG_BEEP_OFF = 500;
const unsigned long BEEP_GROUP_GAP = 1800;

const unsigned long THREE_BEEP_GROUP =
  LONG_BEEP_ON * 3 + LONG_BEEP_OFF * 2;

const unsigned long BEEP_PATTERN_LENGTH =
  THREE_BEEP_GROUP + BEEP_GROUP_GAP;

// ---------------- NETWORK -----------------

bool wifiWasConnected = false;
bool internetConnected = false;
bool networkProblem = true;
bool networkWasDown = false;

int internetFailures = 0;
const int MAX_INTERNET_FAILURES = 2;

unsigned long lastWiFiAttempt = 0;
unsigned long lastInternetCheck = 0;
unsigned long lastSerialPrint = 0;

unsigned long networkDownStart = 0;
unsigned long surpriseStartTime = 0;

bool surpriseActive = false;

// ---------------- OTA -----------------

bool otaStarted = false;
bool otaInProgress = false;

// ---------------- LED -----------------

bool ledState = false;
unsigned long lastLEDUpdate = 0;
unsigned long ledInterval = 500;

// ---------------- SPEAKER STRUCTURE -----------------

struct ToneStep {
  uint16_t frequency1;
  uint16_t frequency2;
  uint16_t duration;
};

bool speakersReady = false;

const ToneStep startupSound[] = {
  {1000, 1000, 180},
  {0, 0, 100}
};

const ToneStep wifiConnectedSound[] = {
  {1200, 1200, 150},
  {0, 0, 100},
  {1600, 1600, 200},
  {0, 0, 100}
};

const ToneStep hourlySound[] = {
  {1000, 0, 160},
  {0, 0, 100},
  {1300, 0, 160},
  {0, 0, 100},
  {0, 1000, 160},
  {0, 0, 100},
  {0, 1300, 160},
  {0, 0, 100},
  {1200, 1200, 180},
  {0, 0, 100},
  {1200, 1200, 180}
};

const ToneStep cuteSound1[] = {
  {900, 0, 100},
  {1200, 0, 120},
  {1500, 0, 160}
};

const ToneStep cuteSound2[] = {
  {1300, 1300, 100},
  {1600, 0, 100},
  {1300, 0, 150}
};

const ToneStep cuteSound3[] = {
  {700, 700, 100},
  {950, 950, 120},
  {1200, 1200, 150}
};

const size_t STARTUP_SOUND_LENGTH =
  sizeof(startupSound) / sizeof(startupSound[0]);

const size_t WIFI_SOUND_LENGTH =
  sizeof(wifiConnectedSound) / sizeof(wifiConnectedSound[0]);

const size_t HOURLY_SOUND_LENGTH =
  sizeof(hourlySound) / sizeof(hourlySound[0]);

const size_t CUTE1_LENGTH =
  sizeof(cuteSound1) / sizeof(cuteSound1[0]);

const size_t CUTE2_LENGTH =
  sizeof(cuteSound2) / sizeof(cuteSound2[0]);

const size_t CUTE3_LENGTH =
  sizeof(cuteSound3) / sizeof(cuteSound3[0]);

const ToneStep* currentSequence = nullptr;
size_t currentSequenceLength = 0;
size_t currentSequenceIndex = 0;

bool sequenceRunning = false;
unsigned long sequenceStepStart = 0;

// ---------------- EMO MOODS -----------------

enum FaceMood {
  MOOD_NEUTRAL,
  MOOD_HAPPY,
  MOOD_SURPRISED,
  MOOD_SLEEPY,
  MOOD_PLAYFUL,
  MOOD_TYPING,
  MOOD_EXCITED,
  MOOD_QUESTION,
  MOOD_EARTHQUAKE,
  MOOD_OTA
};

FaceMood currentMood = MOOD_NEUTRAL;

// ---------------- BOOT INTRO -----------------

const FaceMood introFaces[] = {
  MOOD_NEUTRAL,
  MOOD_HAPPY,
  MOOD_PLAYFUL,
  MOOD_SURPRISED,
  MOOD_QUESTION,
  MOOD_EARTHQUAKE,
  MOOD_TYPING,
  MOOD_SLEEPY,
  MOOD_EXCITED,
  MOOD_OTA
};

const size_t INTRO_FACE_COUNT =
  sizeof(introFaces) / sizeof(introFaces[0]);

bool introActive = true;
size_t introIndex = 0;

unsigned long introStartTime = 0;

const unsigned long BOOT_LOGO_DURATION = 2000UL;
const unsigned long INTRO_FACE_DURATION = 900UL;
const unsigned long POWERED_SPLASH_DURATION = 2000UL;
const unsigned long INTRO_LOADING_DURATION = INTRO_FACE_COUNT * INTRO_FACE_DURATION;
const unsigned long INTRO_TOTAL_DURATION =
  BOOT_LOGO_DURATION + INTRO_LOADING_DURATION + POWERED_SPLASH_DURATION;

// ---------------- SLEEP CYCLE -----------------

bool sleeping = false;
bool wakeOnNetworkRecovery = false;

unsigned long awakeStartTime = 0;
unsigned long sleepStartTime = 0;
unsigned long lastSleepAnimation = 0;

int sleepAnimationFrame = 0;

// ---------------- RANDOM ACTIVITIES -----------------

enum IdleAction {
  ACTION_NEUTRAL,
  ACTION_PLAYFUL,
  ACTION_TYPING,
  ACTION_QUESTION,
  ACTION_EARTHQUAKE
};

IdleAction currentAction = ACTION_NEUTRAL;
IdleAction previousAction = ACTION_NEUTRAL;

unsigned long actionStartTime = 0;
unsigned long nextActionTime = 0;

bool actionInitialized = false;
bool activityActive = false;

// ---------------- CLOCK DISPLAY -----------------

unsigned long lastClockEvent = 0;
unsigned long clockDisplayUntil = 0;

bool showClock = false;

// ---------------- BLINKING -----------------

bool blinking = false;

unsigned long nextBlinkTime = 0;
unsigned long blinkStartTime = 0;

int pupilOffsetX = 0;
int pupilOffsetY = 0;

int targetPupilX = 0;
int targetPupilY = 0;

unsigned long nextLookTime = 0;

// ---------------- EXCITED -----------------

bool excitedAnimation = false;
unsigned long excitedUntil = 0;

// ---------------- IDLE SOUNDS -----------------

unsigned long nextCuteSound = 0;

// ---------------- DISPLAY -----------------

unsigned long lastFaceUpdate = 0;
const unsigned long FACE_UPDATE_INTERVAL = 80;

// ---------------- PIXEL SHIFT -----------------

const int8_t shiftXPattern[] = {
  0, 1, 2, 1, 0, -1, -2, -1
};

const int8_t shiftYPattern[] = {
  0, 1, 1, 0, -1, -1, 0, 1
};

const size_t SHIFT_PATTERN_LENGTH = 8;

int oledShiftX = 0;
int oledShiftY = 0;

size_t pixelShiftIndex = 0;
unsigned long lastPixelShift = 0;

// ---------------- HOURLY CHIME -----------------

int lastChimeHour = -1;
int lastChimeDay = -1;

// ============================================================
// FUNCTION DECLARATIONS
// ============================================================

void applyToneStep(const ToneStep& step);
void startSequence(const ToneStep* steps, size_t count);
void updateSound();
void stopSound();

void setupOLED();
void updateOLED();
void drawEMOFace();

void drawNeutralFace();
void drawHappyFace();
void drawSurprisedFace();
void drawSleepyFace();
void drawPlayfulFace();
void drawTypingFace();
void drawExcitedFace();
void drawQuestionFace();
void drawEarthquakeFace();
void drawOTAFace();

void drawLaptop();
void drawNetworkWarning();
void drawClock();
void drawBootIntro();

void updateFaceMood();
void updateFaceAnimation();
void updatePixelShift();

void updateSleepCycle();
void wakeEMO();
void putEMOToSleep();

void updateRandomActivity();
void selectRandomActivity();
void updateCuteSounds();
void updateClockDisplay();

void maintainWiFi();
void monitorInternet();
bool checkInternetURL(const char* url);
void setupOTA();

void updateNetworkState();
void updateAlarmSound();
void updateLED();
void checkHourlyChime();

void printStatus();
void checkForStatusRequest();
void sendTelemetry(const String& requestId);

// ============================================================
// SOUND ENGINE
// ============================================================

void applyToneStep(const ToneStep& step) {

  if (!speakersReady) return;

  ledcWriteTone(SPEAKER_1_PIN, step.frequency1);
  ledcWriteTone(SPEAKER_2_PIN, step.frequency2);
}

void stopSound() {

  if (!speakersReady) return;

  ledcWriteTone(SPEAKER_1_PIN, 0);
  ledcWriteTone(SPEAKER_2_PIN, 0);
}

void startSequence(const ToneStep* steps, size_t count) {

  if (!speakersReady || steps == nullptr || count == 0) return;

  currentSequence = steps;
  currentSequenceLength = count;
  currentSequenceIndex = 0;

  sequenceRunning = true;
  sequenceStepStart = millis();

  applyToneStep(currentSequence[0]);
}

void updateSound() {

  if (!sequenceRunning) return;

  if (millis() - sequenceStepStart >=
      currentSequence[currentSequenceIndex].duration) {

    currentSequenceIndex++;

    if (currentSequenceIndex >= currentSequenceLength) {

      sequenceRunning = false;
      stopSound();
      return;
    }

    sequenceStepStart = millis();

    applyToneStep(currentSequence[currentSequenceIndex]);
  }
}

// ============================================================
// OLED INITIALIZATION
// ============================================================

void setupOLED() {

  Wire.begin(OLED_SDA, OLED_SCL);

  Serial.println("Scanning OLED I2C...");

  bool found = false;

  for (uint8_t address = 1; address < 127; address++) {

    Wire.beginTransmission(address);

    if (Wire.endTransmission() == 0) {

      Serial.print("I2C device: 0x");
      Serial.println(address, HEX);

      if (address == 0x3C || address == 0x3D) {
        oledAddress = address;
        found = true;
      }
    }
  }

  if (!found) {

    Serial.println("OLED not detected.");
    oledAvailable = false;
    return;
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, oledAddress)) {

    Serial.println("OLED initialization failed.");
    oledAvailable = false;
    return;
  }

  oledAvailable = true;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(25, 20);
  display.println("POWERING UP");
  display.setCursor(25, 35);
  display.println("ESP32 EMO");
  display.display();

  Serial.println("OLED initialized.");
}

// ============================================================
// PIXEL SHIFTING
// ============================================================

void updatePixelShift() {

  if (!oledAvailable) return;

  if (millis() - lastPixelShift >= PIXEL_SHIFT_INTERVAL) {

    lastPixelShift = millis();

    pixelShiftIndex++;

    if (pixelShiftIndex >= SHIFT_PATTERN_LENGTH) {
      pixelShiftIndex = 0;
    }

    oledShiftX = shiftXPattern[pixelShiftIndex];
    oledShiftY = shiftYPattern[pixelShiftIndex];
  }
}

// ============================================================
// BOOT INTRO
// ============================================================

void drawBootLogo() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Simple centred, Samsung-style startup title.
  display.drawRoundRect(35 + oledShiftX, 5 + oledShiftY, 58, 42, 10, SSD1306_WHITE);
  display.fillCircle(52 + oledShiftX, 25 + oledShiftY, 7, SSD1306_WHITE);
  display.fillCircle(76 + oledShiftX, 25 + oledShiftY, 7, SSD1306_WHITE);
  display.fillCircle(52 + oledShiftX, 25 + oledShiftY, 3, SSD1306_BLACK);
  display.fillCircle(76 + oledShiftX, 25 + oledShiftY, 3, SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(39 + oledShiftX, 53 + oledShiftY);
  display.print("CANDA BOT");
  display.display();
}

void drawPoweredSplash() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(4 + oledShiftX, 17 + oledShiftY);
  display.print("Powered by");
  display.setTextSize(2);
  display.setCursor(34 + oledShiftX, 39 + oledShiftY);
  display.print("Albin");
  display.display();
}

void drawBootIntro() {
  unsigned long elapsed = millis() - introStartTime;

  if (elapsed >= INTRO_TOTAL_DURATION) {
    introActive = false;
    awakeStartTime = millis();
    currentAction = ACTION_NEUTRAL;
    previousAction = ACTION_NEUTRAL;
    activityActive = false;
    actionInitialized = true;
    nextActionTime = millis() + ACTIVITY_INTERVAL;
    lastClockEvent = millis();
    nextCuteSound = millis() + random(25000, 60000);
    Serial.println("EMO startup intro complete.");
    return;
  }

  if (elapsed < BOOT_LOGO_DURATION) {
    drawBootLogo();
    return;
  }

  unsigned long loadingElapsed = elapsed - BOOT_LOGO_DURATION;

  if (loadingElapsed < INTRO_LOADING_DURATION) {
    display.clearDisplay();
    size_t faceIndex = loadingElapsed / INTRO_FACE_DURATION;
    if (faceIndex >= INTRO_FACE_COUNT) faceIndex = INTRO_FACE_COUNT - 1;

    switch (introFaces[faceIndex]) {
      case MOOD_NEUTRAL:    drawNeutralFace(); break;
      case MOOD_HAPPY:      drawHappyFace(); break;
      case MOOD_PLAYFUL:    drawPlayfulFace(); break;
      case MOOD_SURPRISED:  drawSurprisedFace(); break;
      case MOOD_QUESTION:   drawQuestionFace(); break;
      case MOOD_EARTHQUAKE: drawEarthquakeFace(); break;
      case MOOD_TYPING:     drawTypingFace(); break;
      case MOOD_SLEEPY:     drawSleepyFace(); break;
      case MOOD_EXCITED:    drawExcitedFace(); break;
      case MOOD_OTA:        drawOTAFace(); break;
    }

    float progress = (float)loadingElapsed / (float)INTRO_LOADING_DURATION;
    if (progress > 1.0f) progress = 1.0f;
    int barWidth = (int)(progress * 116);
    display.drawRect(5 + oledShiftX, 54 + oledShiftY, 118, 7, SSD1306_WHITE);
    display.fillRect(6 + oledShiftX, 55 + oledShiftY, barWidth, 5, SSD1306_WHITE);
    // No branding text in the loading-bar screen.
    display.display();
    return;
  }

  // Separate final boot screen, shown only after the loading animation.
  drawPoweredSplash();
}

// ============================================================
// SLEEP AND WAKE CYCLE
// ============================================================

void wakeEMO() {

  if (!sleeping) return;

  sleeping = false;
  wakeOnNetworkRecovery = false;

  awakeStartTime = millis();

  sleepAnimationFrame = 0;

  actionInitialized = true;
  activityActive = false;
  currentAction = ACTION_NEUTRAL;
  previousAction = ACTION_NEUTRAL;
  nextActionTime = millis() + ACTIVITY_INTERVAL;
  excitedAnimation = false;
  showClock = false;
  lastClockEvent = millis();

  nextCuteSound = millis() + random(25000, 60000);

  Serial.println("EMO WAKING UP.");
}

void putEMOToSleep() {

  if (sleeping) return;

  sleeping = true;

  sleepStartTime = millis();
  // Wake early only when recovering from a network outage during sleep.
  wakeOnNetworkRecovery = networkProblem;

  actionInitialized = true;
  activityActive = false;
  currentAction = ACTION_NEUTRAL;
  excitedAnimation = false;
  showClock = false;

  sequenceRunning = false;
  stopSound();

  Serial.println("EMO GOING TO SLEEP.");
}

void updateSleepCycle() {

  unsigned long now = millis();

  if (sleeping) {

    if (now - sleepStartTime >= SLEEP_DURATION) {
      wakeEMO();
    }

    if (wakeOnNetworkRecovery &&
        !networkProblem &&
        WiFi.status() == WL_CONNECTED) {

      wakeEMO();
    }

    return;
  }

  if (now - awakeStartTime >= AWAKE_DURATION) {
    putEMOToSleep();
  }
}

// ============================================================
// RANDOM ACTIVITY SELECTION
// ============================================================

void selectRandomActivity() {
  // Neutral is the permanent resting state. Only choose a temporary action.
  IdleAction newAction;
  do {
    newAction = (IdleAction)random(1, 5); // playful, typing, question, earthquake
  } while (newAction == previousAction);

  previousAction = newAction;
  currentAction = newAction;
  actionStartTime = millis();
  nextActionTime = actionStartTime + ACTIVITY_DURATION;
  activityActive = true;
  actionInitialized = true;

  switch (currentAction) {
    case ACTION_NEUTRAL:    Serial.println("EMO: neutral eyes"); break;
    case ACTION_PLAYFUL:    Serial.println("EMO temporary action: playful"); break;
    case ACTION_TYPING:     Serial.println("EMO temporary action: typing"); break;
    case ACTION_QUESTION:   Serial.println("EMO temporary action: curious"); break;
    case ACTION_EARTHQUAKE: Serial.println("EMO temporary action: earthquake"); break;
  }
}

void updateRandomActivity() {
  if (introActive || sleeping || networkProblem || otaInProgress ||
      surpriseActive || excitedAnimation || showClock) {
    return;
  }

  unsigned long now = millis();

  if (activityActive && now >= nextActionTime) {
    activityActive = false;
    currentAction = ACTION_NEUTRAL;
    nextActionTime = now + ACTIVITY_INTERVAL;
    Serial.println("EMO returning to neutral eyes.");
    return;
  }

  if (!activityActive && now >= nextActionTime) {
    selectRandomActivity();
  }
}

// ============================================================
// CLOCK DISPLAY EVERY 10 MINUTES
// ============================================================

void updateClockDisplay() {

  if (introActive || sleeping || networkProblem || otaInProgress) {
    return;
  }

  unsigned long now = millis();

  if (showClock) {

    if (now >= clockDisplayUntil) {
      showClock = false;
    }

    return;
  }

  if (now - lastClockEvent >= CLOCK_INTERVAL) {

    lastClockEvent = now;

    showClock = true;
    clockDisplayUntil = now + CLOCK_DISPLAY_DURATION;

    Serial.println("EMO clock display.");
  }
}

void drawClock() {

  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 10)) {

    display.setTextSize(1);
    display.setCursor(40 + oledShiftX, 29 + oledShiftY);
    display.print("TIME SYNC");
    return;
  }

  char timeText[12];

  strftime(
    timeText,
    sizeof(timeText),
    "%H:%M:%S",
    &timeInfo
  );

  char dateText[16];

  strftime(
    dateText,
    sizeof(dateText),
    "%d-%m-%Y",
    &timeInfo
  );

  display.setTextSize(2);
  display.setCursor(20 + oledShiftX, 19 + oledShiftY);
  display.print(timeText);

  display.setTextSize(1);
  display.setCursor(43 + oledShiftX, 42 + oledShiftY);
  display.print(dateText);
}

// ============================================================
// CUTE IDLE SOUNDS
// ============================================================

void updateCuteSounds() {

  if (introActive ||
      sleeping ||
      networkProblem ||
      surpriseActive ||
      otaInProgress ||
      excitedAnimation ||
      sequenceRunning) {

    return;
  }

  if (millis() < nextCuteSound) return;

  nextCuteSound = millis() + random(25000, 60000);

  int choice = random(0, 3);

  if (choice == 0) {
    startSequence(cuteSound1, CUTE1_LENGTH);
  } else if (choice == 1) {
    startSequence(cuteSound2, CUTE2_LENGTH);
  } else {
    startSequence(cuteSound3, CUTE3_LENGTH);
  }
}

// ============================================================
// FACE MOOD SELECTION
// ============================================================

void updateFaceMood() {

  if (otaInProgress) {
    currentMood = MOOD_OTA;
    return;
  }

  if (sleeping) {
    currentMood = MOOD_SLEEPY;
    return;
  }

  if (surpriseActive &&
      millis() - surpriseStartTime < SURPRISE_DURATION) {

    currentMood = MOOD_SURPRISED;
    return;
  }

  if (excitedAnimation && millis() < excitedUntil) {

    currentMood = MOOD_EXCITED;
    return;
  }

  if (excitedAnimation && millis() >= excitedUntil) {
    excitedAnimation = false;
  }

  if (showClock) {
    currentMood = MOOD_NEUTRAL;
    return;
  }

  switch (currentAction) {

    case ACTION_NEUTRAL:
      currentMood = MOOD_NEUTRAL;
      break;

    case ACTION_PLAYFUL:
      currentMood = MOOD_PLAYFUL;
      break;

    case ACTION_TYPING:
      currentMood = MOOD_TYPING;
      break;

    case ACTION_QUESTION:
      currentMood = MOOD_QUESTION;
      break;

    case ACTION_EARTHQUAKE:
      currentMood = MOOD_EARTHQUAKE;
      break;
  }
}

// ============================================================
// BLINKING AND EYE MOVEMENT
// ============================================================

void updateFaceAnimation() {

  unsigned long now = millis();

  if (!blinking && now >= nextBlinkTime) {

    blinking = true;
    blinkStartTime = now;
  }

  if (blinking && now - blinkStartTime >= 140) {

    blinking = false;
    nextBlinkTime = now + random(2200, 5200);
  }

  if (now >= nextLookTime && !sleeping) {

    targetPupilX = random(-5, 6);
    targetPupilY = random(-2, 3);

    nextLookTime = now + random(1800, 4000);
  }

  if (sleeping) {

    targetPupilX = 0;
    targetPupilY = 0;
  }

  if (pupilOffsetX < targetPupilX) pupilOffsetX++;
  if (pupilOffsetX > targetPupilX) pupilOffsetX--;

  if (pupilOffsetY < targetPupilY) pupilOffsetY++;
  if (pupilOffsetY > targetPupilY) pupilOffsetY--;
}

// ============================================================
// NEUTRAL FACE
// ============================================================

void drawNeutralFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.fillRoundRect(
    15 + x, 14 + y,
    43, 34,
    12,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x, 14 + y,
    43, 34,
    12,
    SSD1306_WHITE
  );

  if (blinking) {

    display.fillRoundRect(
      15 + x, 28 + y,
      43, 5,
      3,
      SSD1306_BLACK
    );

    display.fillRoundRect(
      70 + x, 28 + y,
      43, 5,
      3,
      SSD1306_BLACK
    );

    return;
  }

  display.fillCircle(
    36 + x + pupilOffsetX,
    31 + y + pupilOffsetY,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    91 + x + pupilOffsetX,
    31 + y + pupilOffsetY,
    8,
    SSD1306_BLACK
  );

  display.fillCircle(
    39 + x + pupilOffsetX,
    28 + y + pupilOffsetY,
    2,
    SSD1306_WHITE
  );

  display.fillCircle(
    94 + x + pupilOffsetX,
    28 + y + pupilOffsetY,
    2,
    SSD1306_WHITE
  );
}

// ============================================================
// HAPPY FACE
// ============================================================

void drawHappyFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.fillRoundRect(
    17 + x, 13 + y,
    41, 33,
    13,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x, 13 + y,
    41, 33,
    13,
    SSD1306_WHITE
  );

  if (!blinking) {

    display.fillCircle(
      37 + x + pupilOffsetX,
      29 + y + pupilOffsetY,
      8,
      SSD1306_BLACK
    );

    display.fillCircle(
      90 + x + pupilOffsetX,
      29 + y + pupilOffsetY,
      8,
      SSD1306_BLACK
    );
  }
}

// ============================================================
// SURPRISED FACE
// ============================================================

void drawSurprisedFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.drawRoundRect(
    13 + x, 8 + y,
    46, 43,
    17,
    SSD1306_WHITE
  );

  display.drawRoundRect(
    69 + x, 8 + y,
    46, 43,
    17,
    SSD1306_WHITE
  );

  if (!blinking) {

    display.fillCircle(
      36 + x, 29 + y, 11,
      SSD1306_WHITE
    );

    display.fillCircle(
      92 + x, 29 + y, 11,
      SSD1306_WHITE
    );

    display.fillCircle(
      36 + x, 29 + y, 4,
      SSD1306_BLACK
    );

    display.fillCircle(
      92 + x, 29 + y, 4,
      SSD1306_BLACK
    );
  }
}

// ============================================================
// SLEEP FACE
// White square eyes with a horizontal closed-eye line
// ============================================================

void drawSleepyFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  // White square eyes.

  display.fillRoundRect(
    15 + x, 15 + y,
    43, 33,
    5,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x, 15 + y,
    43, 33,
    5,
    SSD1306_WHITE
  );

  // Closed horizontal eye lines.

  display.fillRoundRect(
    20 + x, 29 + y,
    33, 5,
    2,
    SSD1306_BLACK
  );

  display.fillRoundRect(
    75 + x, 29 + y,
    33, 5,
    2,
    SSD1306_BLACK
  );

  // Zzz animation.

  display.setTextSize(1);

  int zOffset = (sleepAnimationFrame % 3) * 3;

  display.setCursor(98 + x, 3 + y - zOffset);
  display.print("z");

  display.setCursor(109 + x, 0 + y - zOffset);
  display.print("z");

  if (sleepAnimationFrame % 2 == 0) {

    display.setCursor(116 + x, 5 + y);
    display.print("z");
  }
}

// ============================================================
// PLAYFUL FACE
// ============================================================

void drawPlayfulFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  bool wink = ((millis() / 700) % 2) == 0;

  display.fillRoundRect(
    17 + x, 13 + y,
    41, 34,
    12,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x, 13 + y,
    41, 34,
    12,
    SSD1306_WHITE
  );

  if (wink) {

    display.fillCircle(
      37 + x, 29 + y, 8,
      SSD1306_BLACK
    );

  } else {

    display.drawLine(
      18 + x, 30 + y,
      57 + x, 30 + y,
      SSD1306_BLACK
    );
  }

  if (!blinking) {

    display.fillCircle(
      90 + x + pupilOffsetX,
      29 + y,
      8,
      SSD1306_BLACK
    );
  }

  display.fillRoundRect(
    54 + x, 44 + y,
    20, 8,
    4,
    SSD1306_WHITE
  );
}

// ============================================================
// LAPTOP
// ============================================================

void drawLaptop() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.drawRoundRect(
    42 + x, 39 + y,
    44, 18,
    2,
    SSD1306_WHITE
  );

  display.drawLine(
    47 + x, 44 + y,
    58 + x, 44 + y,
    SSD1306_WHITE
  );

  display.drawLine(
    47 + x, 48 + y,
    76 + x, 48 + y,
    SSD1306_WHITE
  );

  int cursor = 47 + ((millis() / 200) % 28);

  display.drawLine(
    cursor + x, 52 + y,
    cursor + x, 55 + y,
    SSD1306_WHITE
  );

  display.drawLine(
    34 + x, 59 + y,
    94 + x, 59 + y,
    SSD1306_WHITE
  );

  display.drawLine(
    39 + x, 57 + y,
    34 + x, 59 + y,
    SSD1306_WHITE
  );

  display.drawLine(
    89 + x, 57 + y,
    94 + x, 59 + y,
    SSD1306_WHITE
  );
}

// ============================================================
// TYPING FACE
// Eyes blink while typing.
// ============================================================

void drawTypingFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.fillRoundRect(
    17 + x, 9 + y,
    41, 25,
    10,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x, 9 + y,
    41, 25,
    10,
    SSD1306_WHITE
  );

  if (blinking) {

    display.fillRoundRect(
      17 + x, 19 + y,
      41, 5,
      3,
      SSD1306_BLACK
    );

    display.fillRoundRect(
      70 + x, 19 + y,
      41, 5,
      3,
      SSD1306_BLACK
    );

  } else {

    display.fillCircle(
      37 + x + pupilOffsetX,
      22 + y + pupilOffsetY,
      6,
      SSD1306_BLACK
    );

    display.fillCircle(
      90 + x + pupilOffsetX,
      22 + y + pupilOffsetY,
      6,
      SSD1306_BLACK
    );
  }

  drawLaptop();
}

// ============================================================
// QUESTION MARK FACE
// ============================================================

void drawQuestionFace() {

  drawNeutralFace();

  int x = oledShiftX;
  int y = oledShiftY;

  display.setTextSize(2);
  display.setCursor(105 + x, 0 + y);
  display.print("?");
}

// ============================================================
// EARTHQUAKE FACE
// Shaking neutral eyes and question mark.
// ============================================================

void drawEarthquakeFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  int shakeX = ((millis() / 55) % 3) - 1;
  int shakeY = ((millis() / 75) % 3) - 1;

  display.fillRoundRect(
    15 + x + shakeX, 14 + y + shakeY,
    43, 34,
    12,
    SSD1306_WHITE
  );

  display.fillRoundRect(
    70 + x + shakeX, 14 + y + shakeY,
    43, 34,
    12,
    SSD1306_WHITE
  );

  if (!blinking) {

    display.fillCircle(
      36 + x + shakeX + pupilOffsetX,
      31 + y + shakeY + pupilOffsetY,
      8,
      SSD1306_BLACK
    );

    display.fillCircle(
      91 + x + shakeX + pupilOffsetX,
      31 + y + shakeY + pupilOffsetY,
      8,
      SSD1306_BLACK
    );
  }

  display.setTextSize(2);
  display.setCursor(105 + x, 0 + y);
  display.print("?");
}

// ============================================================
// EXCITED FACE
// ============================================================

void drawExcitedFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.drawRoundRect(
    15 + x, 10 + y,
    44, 39,
    13,
    SSD1306_WHITE
  );

  display.drawRoundRect(
    69 + x, 10 + y,
    44, 39,
    13,
    SSD1306_WHITE
  );

  if (!blinking) {

    display.fillCircle(
      37 + x, 29 + y, 10,
      SSD1306_WHITE
    );

    display.fillCircle(
      91 + x, 29 + y, 10,
      SSD1306_WHITE
    );

    display.fillCircle(
      37 + x, 29 + y, 4,
      SSD1306_BLACK
    );

    display.fillCircle(
      91 + x, 29 + y, 4,
      SSD1306_BLACK
    );
  }

  display.drawLine(5 + x, 18 + y, 10 + x, 18 + y, SSD1306_WHITE);
  display.drawLine(7 + x, 15 + y, 7 + x, 21 + y, SSD1306_WHITE);

  display.drawLine(117 + x, 18 + y, 122 + x, 18 + y, SSD1306_WHITE);
  display.drawLine(119 + x, 15 + y, 119 + x, 21 + y, SSD1306_WHITE);
}

// ============================================================
// OTA FACE
// ============================================================

void drawOTAFace() {

  int x = oledShiftX;
  int y = oledShiftY;

  display.drawRoundRect(
    17 + x, 14 + y,
    41, 34,
    10,
    SSD1306_WHITE
  );

  display.drawRoundRect(
    70 + x, 14 + y,
    41, 34,
    10,
    SSD1306_WHITE
  );

  int movement = (millis() / 300) % 5;

  display.fillCircle(
    37 + x + movement,
    30 + y,
    7,
    SSD1306_WHITE
  );

  display.fillCircle(
    90 + x + movement,
    30 + y,
    7,
    SSD1306_WHITE
  );
}

// ============================================================
// NETWORK WARNING
// ============================================================

void drawNetworkWarning() {

  if (!networkProblem) return;

  int x = 112 + oledShiftX;
  int y = 2 + oledShiftY;

  display.drawTriangle(
    x - 6, y + 10,
    x, y - 1,
    x + 6, y + 10,
    SSD1306_WHITE
  );

  display.drawLine(
    x, y + 3,
    x, y + 6,
    SSD1306_WHITE
  );

  display.drawPixel(
    x, y + 8,
    SSD1306_WHITE
  );
}

// ============================================================
// COMPLETE FACE DRAWING
// ============================================================

void drawEMOFace() {

  display.clearDisplay();

  if (showClock && !sleeping && !otaInProgress && !networkProblem) {

    drawClock();

  } else {

    switch (currentMood) {

      case MOOD_NEUTRAL:
        drawNeutralFace();
        break;

      case MOOD_HAPPY:
        drawHappyFace();
        break;

      case MOOD_SURPRISED:
        drawSurprisedFace();
        break;

      case MOOD_SLEEPY:
        drawSleepyFace();
        break;

      case MOOD_PLAYFUL:
        drawPlayfulFace();
        break;

      case MOOD_TYPING:
        drawTypingFace();
        break;

      case MOOD_EXCITED:
        drawExcitedFace();
        break;

      case MOOD_QUESTION:
        drawQuestionFace();
        break;

      case MOOD_EARTHQUAKE:
        drawEarthquakeFace();
        break;

      case MOOD_OTA:
        drawOTAFace();
        break;
    }
  }

  drawNetworkWarning();

  display.display();
}

// ============================================================
// OLED UPDATE
// ============================================================

void updateOLED() {

  if (!oledAvailable) return;

  if (millis() - lastFaceUpdate < FACE_UPDATE_INTERVAL) {
    return;
  }

  lastFaceUpdate = millis();

  if (introActive) {

    drawBootIntro();
    return;
  }

  updateFaceMood();
  updateFaceAnimation();

  if (sleeping &&
      millis() - lastSleepAnimation >= 1000) {

    lastSleepAnimation = millis();
    sleepAnimationFrame++;
  }

  drawEMOFace();
}

// ============================================================
// WIFI MANAGEMENT
// ============================================================

void maintainWiFi() {

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {

    if (!wifiWasConnected) {

      wifiWasConnected = true;

      Serial.println("WiFi connected!");

      Serial.print("IP: ");
      Serial.println(WiFi.localIP());

      Serial.print("RSSI: ");
      Serial.println(WiFi.RSSI());

      if (!networkProblem) {

        startSequence(
          wifiConnectedSound,
          WIFI_SOUND_LENGTH
        );
      }

      if (!otaStarted) {
        setupOTA();
      }

      configTime(
        GMT_OFFSET_SEC,
        DAYLIGHT_OFFSET_SEC,
        "pool.ntp.org",
        "time.google.com"
      );
    }

    return;
  }

  if (wifiWasConnected) {

    Serial.println("WiFi disconnected.");

    wifiWasConnected = false;
    internetConnected = false;
  }

  if (millis() - lastWiFiAttempt >= WIFI_RECONNECT_INTERVAL) {

    lastWiFiAttempt = millis();

    Serial.println("Attempting WiFi reconnection...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// ============================================================
// OTA
// ============================================================

void setupOTA() {

  if (WiFi.status() != WL_CONNECTED) return;

  WiFi.setSleep(false);

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);

  ArduinoOTA.onStart([]() {

    otaInProgress = true;

    sequenceRunning = false;
    stopSound();

    Serial.println("OTA update started.");
  });

  ArduinoOTA.onEnd([]() {

    otaInProgress = false;

    Serial.println("OTA update complete.");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

    unsigned int percent = (progress * 100) / total;

    Serial.printf("OTA progress: %u%%\r", percent);
  });

  ArduinoOTA.onError([](ota_error_t error) {

    otaInProgress = false;

    Serial.printf("OTA error: %u\n", error);
  });

  ArduinoOTA.begin();

  otaStarted = true;

  Serial.println("OTA READY");
  Serial.print("Hostname: ");
  Serial.println(OTA_HOSTNAME);
  Serial.println("OTA port: 3232");
}

// ============================================================
// INTERNET CHECK
// ============================================================

bool checkInternetURL(const char* url) {

  WiFiClientSecure client;

  client.setInsecure();

  HTTPClient https;

  https.setTimeout(5000);

  if (!https.begin(client, url)) {
    return false;
  }

  int httpCode = https.GET();

  https.end();

  Serial.print("HTTP response: ");
  Serial.println(httpCode);

  return (httpCode == 200 || httpCode == 204);
}

void monitorInternet() {

  if (WiFi.status() != WL_CONNECTED) {

    internetConnected = false;
    return;
  }

  if (millis() - lastInternetCheck < INTERNET_CHECK_INTERVAL) {
    return;
  }

  lastInternetCheck = millis();

  Serial.println("Checking internet...");

  bool result = checkInternetURL(
    "https://connectivitycheck.gstatic.com/generate_204"
  );

  if (!result) {

    Serial.println("Trying backup internet check...");

    result = checkInternetURL(
      "https://www.google.com/generate_204"
    );
  }

  if (result) {

    internetConnected = true;
    internetFailures = 0;

  } else {

    internetConnected = false;
    internetFailures++;

    Serial.print("Internet failures: ");
    Serial.println(internetFailures);
  }
}

// ============================================================
// NETWORK STATE
// ============================================================

void updateNetworkState() {

  networkProblem =
    (WiFi.status() != WL_CONNECTED) ||
    (internetFailures >= MAX_INTERNET_FAILURES);

  if (networkProblem && !networkWasDown) {

    networkWasDown = true;

    networkDownStart = millis();

    surpriseActive = true;
    surpriseStartTime = millis();

    sequenceRunning = false;
    stopSound();

    Serial.println("NETWORK FAILURE: SURPRISED FACE.");
  }

  if (!networkProblem && networkWasDown) {

    networkWasDown = false;
    surpriseActive = false;

    Serial.println("NETWORK RESTORED.");

    if (sleeping) {
      wakeEMO();
    }

    excitedAnimation = true;
    excitedUntil = millis() + 4000;
  }

  if (surpriseActive &&
      millis() - surpriseStartTime >= SURPRISE_DURATION) {

    surpriseActive = false;

    Serial.println("Surprise finished. Returning to normal activity.");
  }
}

// ============================================================
// NETWORK ALARM
// 1 MINUTE ALARM / 2 MINUTES SILENCE
// THREE LONG BEEPS PER GROUP
// ============================================================

void updateAlarmSound() {

  if (!networkProblem || !speakersReady) {

    stopSound();
    return;
  }

  unsigned long elapsed =
    millis() - networkDownStart;

  unsigned long cyclePosition =
    elapsed % ALARM_CYCLE_PERIOD;

  bool alarmWindow =
    cyclePosition < ALARM_ACTIVE_PERIOD;

  if (!alarmWindow) {

    stopSound();
    return;
  }

  unsigned long patternPosition =
    cyclePosition % BEEP_PATTERN_LENGTH;

  bool shouldBeep = false;
  int speakerChoice = 0;

  unsigned long singleBeepDuration =
    LONG_BEEP_ON + LONG_BEEP_OFF;

  unsigned long beepSequenceDuration =
    THREE_BEEP_GROUP;

  if (patternPosition < beepSequenceDuration) {

    int beepIndex =
      patternPosition / singleBeepDuration;

    unsigned long positionInBeep =
      patternPosition % singleBeepDuration;

    if (beepIndex < 3 &&
        positionInBeep < LONG_BEEP_ON) {

      shouldBeep = true;
      speakerChoice = beepIndex % 2;
    }
  }

  if (shouldBeep) {

    if (speakerChoice == 0) {

      ledcWriteTone(SPEAKER_1_PIN, 1000);
      ledcWriteTone(SPEAKER_2_PIN, 0);

    } else {

      ledcWriteTone(SPEAKER_1_PIN, 0);
      ledcWriteTone(SPEAKER_2_PIN, 1000);
    }

  } else {

    stopSound();
  }
}

// ============================================================
// BLUE LED
// ============================================================

void updateLED() {

  if (millis() - lastLEDUpdate < ledInterval) {
    return;
  }

  lastLEDUpdate = millis();

  if (networkProblem) {

    ledInterval = 150;
    ledState = !ledState;

  } else {

    ledInterval = random(1500, 5000);
    ledState = !ledState;
  }

  digitalWrite(
    BLUE_LED_PIN,
    ledState ? HIGH : LOW
  );
}

// ============================================================
// HOURLY CHIME
// ============================================================

void checkHourlyChime() {

  struct tm timeInfo;

  if (!getLocalTime(&timeInfo, 10)) return;

  if (timeInfo.tm_min == 0 &&
      timeInfo.tm_sec < 2 &&
      timeInfo.tm_hour != lastChimeHour) {

    if (!networkProblem &&
        !otaInProgress &&
        !sleeping &&
        !introActive) {

      lastChimeHour = timeInfo.tm_hour;
      lastChimeDay = timeInfo.tm_yday;
      Serial.println("Hourly chime.");

      excitedAnimation = true;
      excitedUntil = millis() + 4000;

      startSequence(
        hourlySound,
        HOURLY_SOUND_LENGTH
      );
    }
  }

  if (timeInfo.tm_yday != lastChimeDay &&
      timeInfo.tm_hour == 0 &&
      timeInfo.tm_min == 0) {

    lastChimeHour = -1;
  }
}

// ============================================================
// ON-DEMAND VERCEL DASHBOARD TELEMETRY
// ESP32 checks for a pending request every 30 seconds. Full
// hardware telemetry is uploaded only after a dashboard click.
// ============================================================

bool dashboardCredentialsConfigured() {
  return String(TELEMETRY_URL).indexOf("YOUR-VERCEL-PROJECT") < 0 &&
         String(DEVICE_REQUEST_URL).indexOf("YOUR-VERCEL-PROJECT") < 0 &&
         String(DEVICE_TOKEN).indexOf("YOUR_DEVICE_TOKEN") < 0;
}

void checkForStatusRequest() {
  if (WiFi.status() != WL_CONNECTED || !dashboardCredentialsConfigured()) return;

  unsigned long now = millis();
  if (now - lastDeviceRequestCheck < DEVICE_REQUEST_CHECK_INTERVAL) return;
  lastDeviceRequestCheck = now;

  WiFiClientSecure client;
  client.setInsecure(); // HTTPS encrypted; server certificate is not validated.
  HTTPClient https;
  https.setTimeout(5000);
  if (!https.begin(client, DEVICE_REQUEST_URL)) {
    Serial.println("Dashboard request check: HTTPS begin failed.");
    return;
  }

  https.addHeader("X-Device-Token", DEVICE_TOKEN);
  int code = https.GET();
  if (code == HTTP_CODE_OK) {
    String body = https.getString();
    int keyAt = body.indexOf("\"requestId\"");
    if (body.indexOf("\"pending\":true") >= 0 && keyAt >= 0) {
      int colon = body.indexOf(':', keyAt);
      int firstQuote = body.indexOf('"', colon + 1);
      int secondQuote = body.indexOf('"', firstQuote + 1);
      if (firstQuote >= 0 && secondQuote > firstQuote) {
        String requestId = body.substring(firstQuote + 1, secondQuote);
        Serial.println("Dashboard status requested. Collecting telemetry...");
        https.end();
        sendTelemetry(requestId);
        return;
      }
    }
  } else if (code < 0) {
    Serial.printf("Dashboard request check failed: %s\n", https.errorToString(code).c_str());
  }
  https.end();
}

void sendTelemetry(const String& requestId) {
  if (WiFi.status() != WL_CONNECTED || requestId.length() == 0 || !dashboardCredentialsConfigured()) return;

  WiFiClientSecure client;
  client.setInsecure(); // HTTPS encrypted; server certificate is not validated.
  HTTPClient https;
  https.setTimeout(7000);
  if (!https.begin(client, TELEMETRY_URL)) {
    Serial.println("Telemetry: HTTPS begin failed.");
    return;
  }

  https.addHeader("Content-Type", "application/json");
  https.addHeader("X-Device-Token", DEVICE_TOKEN);

  String ip = WiFi.localIP().toString();
  String chipModel = String(ESP.getChipModel());
  uint64_t mac = ESP.getEfuseMac();
  char macText[18];
  snprintf(macText, sizeof(macText), "%02X:%02X:%02X:%02X:%02X:%02X",
           (uint8_t)(mac >> 40), (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 24), (uint8_t)(mac >> 16),
           (uint8_t)(mac >> 8), (uint8_t)mac);

  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapFree = ESP.getFreeHeap();
  uint32_t heapMinFree = ESP.getMinFreeHeap();
  uint32_t flashSize = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();
  uint32_t sketchFree = ESP.getFreeSketchSpace();
  uint32_t psramTotal = ESP.getPsramSize();
  uint32_t psramFree = ESP.getFreePsram();
  updateCpuLoad();
  float chipTemperatureC = temperatureRead();

  String payload = "{";
  payload += "\"deviceId\":\"" + String(DEVICE_ID) + "\",";
  payload += "\"requestId\":\"" + requestId + "\",";
  payload += "\"deviceName\":\"Canda Bot\",";
  payload += "\"chipModel\":\"" + chipModel + "\",";
  payload += "\"chipRevision\":" + String(ESP.getChipRevision()) + ",";
  payload += "\"cpuMHz\":" + String(ESP.getCpuFreqMHz()) + ",";
  payload += "\"cpuLoadPercent\":" + String(cpuLoadPercent, 1) + ",";
  payload += "\"chipTemperatureC\":" + String(chipTemperatureC, 1) + ",";
  payload += "\"cores\":" + String(ESP.getChipCores()) + ",";
  payload += "\"heapTotal\":" + String(heapTotal) + ",";
  payload += "\"heapFree\":" + String(heapFree) + ",";
  payload += "\"heapMinFree\":" + String(heapMinFree) + ",";
  payload += "\"heapUsed\":" + String(heapTotal - heapFree) + ",";
  payload += "\"flashSize\":" + String(flashSize) + ",";
  payload += "\"sketchSize\":" + String(sketchSize) + ",";
  payload += "\"sketchFree\":" + String(sketchFree) + ",";
  payload += "\"psramTotal\":" + String(psramTotal) + ",";
  payload += "\"psramFree\":" + String(psramFree) + ",";
  payload += "\"uptimeSeconds\":" + String(millis() / 1000UL) + ",";
  payload += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  payload += "\"ip\":\"" + ip + "\",";
  payload += "\"mac\":\"" + String(macText) + "\",";
  payload += "\"wifiConnected\":true,";
  payload += "\"internetConnected\":" + String(internetConnected ? "true" : "false") + ",";
  payload += "\"networkProblem\":" + String(networkProblem ? "true" : "false") + ",";
  payload += "\"sleeping\":" + String(sleeping ? "true" : "false") + ",";
  payload += "\"otaInProgress\":" + String(otaInProgress ? "true" : "false") + ",";
  payload += "\"oledAvailable\":" + String(oledAvailable ? "true" : "false") + ",";
  payload += "\"internetFailures\":" + String(internetFailures);
  payload += "}";

  int httpCode = https.POST(payload);
  Serial.print("On-demand telemetry HTTP response: ");
  Serial.println(httpCode);
  https.end();
}

// ============================================================
// SERIAL STATUS
// ============================================================

void printStatus() {

  if (millis() - lastSerialPrint < SERIAL_INTERVAL) {
    return;
  }

  lastSerialPrint = millis();

  Serial.println();
  Serial.println("========= EMO WATCHDOG =========");

  Serial.print("WiFi: ");
  Serial.println(
    WiFi.status() == WL_CONNECTED ? "CONNECTED" : "DISCONNECTED"
  );

  Serial.print("Internet: ");
  Serial.println(internetConnected ? "ONLINE" : "OFFLINE");

  Serial.print("Network problem: ");
  Serial.println(networkProblem ? "YES" : "NO");

  Serial.print("IP: ");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("--");
  }

  Serial.print("RSSI: ");

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("--");
  }

  Serial.print("Failures: ");
  Serial.println(internetFailures);

  Serial.print("Surprise: ");
  Serial.println(surpriseActive ? "ACTIVE" : "INACTIVE");

  Serial.print("Sleep: ");
  Serial.println(sleeping ? "SLEEPING" : "AWAKE");

  Serial.print("Current activity: ");
  Serial.println((int)currentAction);

  Serial.print("OTA: ");
  Serial.println(otaStarted ? "READY" : "NOT READY");

  Serial.print("OLED: ");
  Serial.println(oledAvailable ? "READY" : "NOT FOUND");

  struct tm timeInfo;

  if (getLocalTime(&timeInfo, 10)) {

    char timeString[30];

    strftime(
      timeString,
      sizeof(timeString),
      "%d-%m-%Y %H:%M:%S",
      &timeInfo
    );

    Serial.print("IST: ");
    Serial.println(timeString);
  }

  Serial.println("===============================");
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("================================");
  Serial.println("      ESP32 EMO WATCHDOG");
  Serial.println("================================");

  pinMode(BLUE_LED_PIN, OUTPUT);
  digitalWrite(BLUE_LED_PIN, LOW);

  // ESP32 Core 3.x LEDC.

  bool speaker1 = ledcAttach(SPEAKER_1_PIN, 2000, 8);
  bool speaker2 = ledcAttach(SPEAKER_2_PIN, 2000, 8);

  speakersReady = speaker1 && speaker2;

  if (speakersReady) {

    Serial.println("Speakers initialized.");

    startSequence(
      startupSound,
      STARTUP_SOUND_LENGTH
    );

  } else {

    Serial.println("Speaker initialization failed.");
  }

  randomSeed(esp_random());

  setupOLED();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.setSleep(false);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lastWiFiAttempt = millis();

  lastInternetCheck =
    millis() - INTERNET_CHECK_INTERVAL;

  // Boot intro begins now.

  introStartTime = millis();
  introActive = true;
  introIndex = 0;

  // Awake and clock timers are reset when the boot sequence completes.
  lastClockEvent = millis();
  awakeStartTime = millis();
  nextActionTime = millis() + ACTIVITY_INTERVAL;
  actionInitialized = true;
  activityActive = false;

  nextBlinkTime = millis() + random(2000, 5000);
  nextLookTime = millis() + random(1500, 3500);

  setupCpuLoadMonitor();

  Serial.println("Connecting to WiFi...");
  Serial.println("Starting EMO boot animation.");
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {

  updateCpuLoad();

  // OTA.

  if (otaStarted) {
    ArduinoOTA.handle();
  }

  // Network monitoring.

  maintainWiFi();
  monitorInternet();
  updateNetworkState();
  checkForStatusRequest();

  // Boot animation.

  if (introActive) {

    if (oledAvailable &&
        millis() - lastFaceUpdate >= FACE_UPDATE_INTERVAL) {

      lastFaceUpdate = millis();
      drawBootIntro();
    }

    // Network, startup sound and LED monitoring continue during intro.
    if (networkProblem) {
      sequenceRunning = false;
      updateAlarmSound();
    } else {
      updateSound();
    }
    updateLED();
    updatePixelShift();
    printStatus();

    delay(5);
    return;
  }

  // Sleep / wake cycle.

  updateSleepCycle();

  // Network alarm has priority over other sounds.

  if (networkProblem) {

    sequenceRunning = false;
    updateAlarmSound();

  } else {

    updateSound();
  }

  // Character activity.

  updateRandomActivity();
  updateCuteSounds();

  // Time display.

  updateClockDisplay();

  // Hourly chime.

  checkHourlyChime();

  // LED.

  updateLED();

  // OLED and burn-in protection.

  updatePixelShift();
  updateOLED();

  // Serial diagnostics.

  printStatus();

  delay(5);
}

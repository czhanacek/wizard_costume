#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include <esp_wifi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <stdarg.h>

#ifndef DEBUG_NET_SERIAL
#define DEBUG_NET_SERIAL 1
#endif

#if DEBUG_NET_SERIAL
WiFiServer debugServer(23);
WiFiClient debugClient;
unsigned long nextTouchLogMs = 0;

static void debugBegin() {
  debugServer.begin();
  debugServer.setNoDelay(true);
}

static void debugAcceptClient() {
  if (!debugClient || !debugClient.connected()) {
    WiFiClient n = debugServer.available();
    if (n) {
      if (debugClient) debugClient.stop();
      debugClient = n;
      debugClient.setNoDelay(true);
      Serial.println("NetSerial: client connected");
    }
  }
}

static void debugPrint(const char* s) {
  if (debugClient && debugClient.connected()) debugClient.print(s);
}

static void debugPrintln(const char* s) {
  if (debugClient && debugClient.connected()) {
    debugClient.print(s);
    debugClient.print("\r\n");
  }
}

static void debugPrintf(const char* fmt, ...) {
  if (!(debugClient && debugClient.connected())) return;
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  debugClient.print(buf);
}

static void logBoth(const char* s) { Serial.print(s); debugPrint(s); }
static void logBothLn(const char* s) { Serial.println(s); debugPrintln(s); }
static void logBothF(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
  if (debugClient && debugClient.connected()) debugClient.print(buf);
}
#endif

#if DEBUG_NET_SERIAL
bool debugActive = false;
#endif

// Staff: ESP-NOW controller with 2 LED strands + 3 capacitive touch sensors
// Uses "stole" LED count/config for both strands, broadcasts spells to receivers,
// and exposes a 25s OTA window on boot with status indicators.

// ===================== OTA/WiFi Config =====================
#define OTA_ENABLED 1
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "wizard-staff"  // Overridden by build_flags
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""              // Overridden by build_flags
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// ===================== LED/ESP-NOW Config =====================
// LED configuration (stole-equivalent on both strands)
// NOTE: LED_PIN_B (GPIO14) sacrificed for third touch pad
#ifndef LED_PIN_A
#define LED_PIN_A 13
#endif
#ifndef LED_PIN_B
#define LED_PIN_B 14  // DISABLED: used for touch pad 3
#endif
#ifndef NUM_LEDS_STOLE
#define NUM_LEDS_STOLE 225
#endif
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// ESP-NOW channel (must match receivers)
#ifndef ESPNOW_CHANNEL
#define ESPNOW_CHANNEL 1
#endif

// Capacitive touch pins (ESP32 touch-capable pins)
// 2-pin layout: GPIO12 and GPIO14
#ifndef TOUCH_PIN_1
#define TOUCH_PIN_1 12  // T5
#endif
#ifndef TOUCH_PIN_2
#define TOUCH_PIN_2 14  // T6 (sacrificed LED_PIN_B for this)
#endif
#ifndef TOUCH_PIN_3
#define TOUCH_PIN_3 -1  // DISABLED (2-pin layout only)
#endif

// Touch calibration
#ifndef TOUCH_SAMPLES
#define TOUCH_SAMPLES 64
#endif
#ifndef TOUCH_DELTA
#define TOUCH_DELTA 10  // lowered threshold delta for more sensitive touch detection
#endif

// Built-in LED dim blink during OTA window (DISABLED: GPIO4 now used for touch)
// #ifndef BUILTIN_LED_PIN
// #define BUILTIN_LED_PIN 4
// #endif
#ifndef LEDC_CHANNEL_BUILTIN
#define LEDC_CHANNEL_BUILTIN 3
#endif
#ifndef LEDC_TIMER_BITS
#define LEDC_TIMER_BITS 8
#endif
#ifndef LEDC_FREQ_HZ
#define LEDC_FREQ_HZ 5000
#endif

// ===================== Global State =====================
uint8_t globalBrightness = 128;
const uint8_t BRIGHTNESS_STEP = 16;

// Background effect state (mirrors receivers for cohesion)
int currentEffect = 1;  // 0/4=off, 1=rainbow, 2=breathing
int lastEffect = -1;
int backgroundEffect = 0;

// ===================== Simple 2-Button Spell UI =====================
// Button 1 (Pad 0): Cycle effects (1->2->3->1)
// Button 2 (Pad 1): Brightness up (spell 8)
// Both buttons: Brightness down (spell 7)

// Rainbow
uint8_t rainbowHue = 0;
unsigned long nextRainbowMs = 0;
const unsigned long RAINBOW_INTERVAL_MS = 20;

// Breathing
uint8_t breathBrightness = 0;
int8_t breathStep = 4;
unsigned long nextBreathMs = 0;
const unsigned long BREATH_INTERVAL_MS = 15;

// Tempo control
float tempoFactor = 1.0f;
const float TEMPO_MIN = 0.25f;
const float TEMPO_MAX = 4.0f;
inline unsigned long tempoMs(unsigned long baseMs) {
  float scaled = baseMs / tempoFactor;
  if (scaled < 1.0f) scaled = 1.0f;
  return (unsigned long)scaled;
}

// ESP-NOW spell packet
typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket spell;

// Broadcast address (ff:ff:ff:ff:ff:ff)
uint8_t broadcastAddress[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// LED buffers
CRGB ledsA[NUM_LEDS_STOLE];
CRGB ledsB[NUM_LEDS_STOLE];

// Packet send visual ack
volatile bool packetFlash = false;
unsigned long packetFlashUntil = 0;

// Touch state
struct TouchChan {
  int pin;
  uint16_t baseline;
  uint16_t threshold;
  bool pressed;
  unsigned long pressStartMs;  // When this pad was first pressed
};

TouchChan touchChans[2] = {
  {TOUCH_PIN_1, 0, 0, false, 0},
  {TOUCH_PIN_2, 0, 0, false, 0},
};

// Combo detection state
const unsigned long HOLD_THRESHOLD_MS = 300;  // 0.3s to register as "hold"
const unsigned long BOTH_HOLD_THRESHOLD_MS = 400;  // 0.4s for both-hold action
bool pad0_held = false;  // True if pad 0 is held (not tapped)
bool pad1_held = false;  // True if pad 1 is held (not tapped)
unsigned long bothPressStartMs = 0;  // When both pads were pressed together
bool bothPressedTogether = false;  // True if both pressed simultaneously

static uint16_t sampleTouch(int pin, int samples) {
  uint32_t acc = 0;
  for (int i = 0; i < samples; ++i) {
    acc += (uint32_t)touchRead(pin);
    delayMicroseconds(200);
  }
  return (uint16_t)(acc / (uint32_t)samples);
}

static void calibrateTouch() {
  Serial.println("Calibrating capacitive touch baselines...");
#if DEBUG_NET_SERIAL
  debugPrintln("Calibrating capacitive touch baselines...");
#endif
  for (int i = 0; i < 2; ++i) {
    uint16_t base = sampleTouch(touchChans[i].pin, TOUCH_SAMPLES);
    touchChans[i].baseline = base;
    uint16_t delta = (base > 1) ? (uint16_t)min((uint16_t)TOUCH_DELTA, (uint16_t)(base - 1)) : 0;
    uint16_t thr = (uint16_t)(base - delta);
    touchChans[i].threshold = thr;
    touchChans[i].pressed = false;
    Serial.printf(" Touch pin %d: baseline=%u, threshold=%u\n", touchChans[i].pin, base, thr);
#if DEBUG_NET_SERIAL
    debugPrintf(" Touch pin %d: baseline=%u, threshold=%u\n", touchChans[i].pin, base, thr);
#endif
  }
}

// ===================== OTA Window/Status =====================
#if OTA_ENABLED
const unsigned long OTA_WINDOW_MS = 25000;  // 25 seconds
bool otaWindowActive = false;
volatile bool otaInProgress = false;
unsigned long otaWindowEndMs = 0;
unsigned long otaVisualNextMs = 0;
const unsigned long OTA_VISUAL_INTERVAL_MS = 30;
uint8_t otaVisualHue = 160; // blue-ish
uint16_t otaVisualPos = 0;  // index into NUM_LEDS_STOLE per strand

bool builtinLedReady = false;
unsigned long builtinLedNextToggleMs = 0;
const unsigned long BUILTIN_LED_TOGGLE_MS = 300;
#endif

// ===================== ESP-NOW =====================
static void sendSpell(int id) {
  spell.effect_id = id;
  esp_now_send(broadcastAddress, (uint8_t *)&spell, sizeof(spell));
  Serial.printf("Cast spell %d\n", id);
#if DEBUG_NET_SERIAL
  debugPrintf("Cast spell %d\n", id);
#endif
  packetFlash = true;
  packetFlashUntil = millis() + 120;
}


// ===================== Setup & Loop =====================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("ESP-NOW Staff (2 LED strands + 3 cap-touch + OTA)");
#if DEBUG_NET_SERIAL
  Serial.println("NetSerial: will start after OTA window (post-OTA).");
#endif

  // LEDs (Strand B disabled: GPIO14 used for touch pad 3)
  FastLED.addLeds<LED_TYPE, LED_PIN_A, COLOR_ORDER>(ledsA, NUM_LEDS_STOLE);
  // FastLED.addLeds<LED_TYPE, LED_PIN_B, COLOR_ORDER>(ledsB, NUM_LEDS_STOLE);
  FastLED.setBrightness(globalBrightness);
  FastLED.clear();
  FastLED.show();
  Serial.printf("Strand A: %d LEDs @ pin %d\n", NUM_LEDS_STOLE, LED_PIN_A);
  Serial.printf("Strand B: DISABLED (GPIO14 used for touch pad 3)\n");

  // Built-in LED PWM for status (DISABLED: GPIO4 now used for touch)
  // ledcSetup(LEDC_CHANNEL_BUILTIN, LEDC_FREQ_HZ, LEDC_TIMER_BITS);
  // ledcAttachPin(BUILTIN_LED_PIN, LEDC_CHANNEL_BUILTIN);
  // ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
  builtinLedReady = false;

  // WiFi/ESP-NOW: Start with SoftAP FIRST to pin channel (critical for ESP-NOW)
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  // Hidden SoftAP on fixed channel to pin radio for ESP-NOW (MUST be before esp_now_init)
  WiFi.softAP("wr-sync", "", ESPNOW_CHANNEL, 1 /* hidden */);
  delay(100);
  
  // Initialize ESP-NOW AFTER SoftAP is created
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  // Use dynamic channel (0) to follow current radio channel (SoftAP/STA may move channel)
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add broadcast peer");
  }
  Serial.printf("ESP-NOW initialized on channel %d\n", ESPNOW_CHANNEL);

#if OTA_ENABLED
  // Connect STA for OTA
  Serial.println("Connecting to WiFi for OTA...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected for OTA");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("WiFi channel: %d\n", WiFi.channel());

    // Configure OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("Start updating " + type);
      otaInProgress = true;
      backgroundEffect = 0;
      FastLED.clear();
      FastLED.show();
      // steady dim built-in LED during update
      if (builtinLedReady) ledcWrite(LEDC_CHANNEL_BUILTIN, 24);
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd OTA");
      // Brief green success flash across both strands
      fill_solid(ledsA, NUM_LEDS_STOLE, CRGB::Green);
      fill_solid(ledsB, NUM_LEDS_STOLE, CRGB::Green);
      FastLED.show();
      delay(200);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
      if (builtinLedReady) ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
#if DEBUG_NET_SERIAL
      if (!debugActive) {
        debugBegin();
        debugActive = true;
        Serial.println("NetSerial: started on TCP port 23 (post-OTA end)");
      }
#endif
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      if (total == 0) return;
      uint32_t pct = (static_cast<uint32_t>(progress) * 100U) / static_cast<uint32_t>(total);
      static uint32_t lastPct = 101U;
      if (pct != lastPct) {
        lastPct = pct;
        Serial.printf("OTA Progress: %u%%\r", pct);
      }

      // Visual OTA progress (blue bar fill across A then B)
      const uint32_t totalLeds = (uint32_t)NUM_LEDS_STOLE * 2U;
      uint32_t lit = ((uint64_t)progress * totalLeds) / total;

      FastLED.clear();
      CRGB onColor = CHSV(160, 255, globalBrightness);

      uint32_t remaining = lit;
      uint32_t cA = remaining > (uint32_t)NUM_LEDS_STOLE ? (uint32_t)NUM_LEDS_STOLE : remaining;
      if (cA > 0) fill_solid(ledsA, (int)cA, onColor);
      remaining = (remaining > (uint32_t)NUM_LEDS_STOLE) ? (remaining - (uint32_t)NUM_LEDS_STOLE) : 0;

      uint32_t cB = remaining > (uint32_t)NUM_LEDS_STOLE ? (uint32_t)NUM_LEDS_STOLE : remaining;
      if (cB > 0) fill_solid(ledsB, (int)cB, onColor);

      FastLED.show();
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]\n", error);
      // Flash red on error
      fill_solid(ledsA, NUM_LEDS_STOLE, CRGB::Red);
      fill_solid(ledsB, NUM_LEDS_STOLE, CRGB::Red);
      FastLED.show();
      delay(1000);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
      if (builtinLedReady) ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready");
    Serial.printf("Hostname: %s\n", OTA_HOSTNAME);

    // Start 25s OTA window
    otaWindowActive = true;
    otaWindowEndMs = millis() + OTA_WINDOW_MS;
    Serial.printf("OTA upload window active for %lu ms\n", OTA_WINDOW_MS);
  } else {
    Serial.println("\nWiFi connection failed. OTA disabled; continuing with ESP-NOW only.");
  }
#endif

  // Touch calibration
  calibrateTouch();

  // Start with visible background
  currentEffect = 1;
  Serial.println("\n=== SIMPLE 2-BUTTON SPELL UI ===");
  Serial.println("Top Button: Cycle Effects (Rainbow -> Breathing -> Off)");
  Serial.println("Bottom Button: Tempo Up");
  Serial.println("Hold Top + Tap Bottom: Brightness Down");
  Serial.println("Hold Bottom + Tap Top: Brightness Up");
  Serial.println("Hold Both > 0.4s: Shoot Animation");
  Serial.println("==================================\n");
}

void loop() {
#if DEBUG_NET_SERIAL
  if (debugActive) debugAcceptClient();
#endif
#if OTA_ENABLED
  // During the initial OTA window, handle OTA and show a status indicator.
  if (otaWindowActive) {
    ArduinoOTA.handle();

    if (!otaInProgress) {
      unsigned long now = millis();
      if ((long)(now - otaVisualNextMs) >= 0) {
        otaVisualNextMs = now + OTA_VISUAL_INTERVAL_MS;

        // Render a colorful comet animation across both strands to indicate "upload mode"
        FastLED.clear();

        int head = otaVisualPos % NUM_LEDS_STOLE;
        ledsA[head] = CHSV(otaVisualHue, 220, globalBrightness);
        ledsB[head] = CHSV(otaVisualHue + 64, 220, globalBrightness);

        if (NUM_LEDS_STOLE > 1) {
          int t = (head + NUM_LEDS_STOLE - 1) % NUM_LEDS_STOLE;
          ledsA[t] = CHSV(otaVisualHue, 220, globalBrightness / 4);
          ledsB[t] = CHSV(otaVisualHue + 64, 220, globalBrightness / 4);
        }

        otaVisualPos = (otaVisualPos + 1) % NUM_LEDS_STOLE;
        otaVisualHue++; // slowly cycle hues
        FastLED.show();

        // Dim pulsing built-in LED during OTA window (very low peak)
        if (builtinLedReady) {
          static uint8_t phase = 0; // 0..255
          phase += 4; // pulse speed
          uint8_t tri = (phase < 128) ? phase : (255 - phase); // 0..127 triangle wave
          const uint8_t MAX_DUTY = 8; // very dim peak
          uint8_t duty = (uint16_t)tri * MAX_DUTY / 127;
          ledcWrite(LEDC_CHANNEL_BUILTIN, duty);
        }
      }
    }

    // Close OTA window after timeout (unless an OTA is currently active)
    if ((long)(millis() - otaWindowEndMs) >= 0 && !otaInProgress) {
      otaWindowActive = false;
      FastLED.clear();
      FastLED.show();
      // Turn off built-in LED after OTA window closes
      if (builtinLedReady) {
        ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
      }
      Serial.printf("OTA window closed; continuing normal staff operation on ESPNOW channel %d\n", ESPNOW_CHANNEL);
#if DEBUG_NET_SERIAL
      if (!debugActive) {
        debugBegin();
        debugActive = true;
        Serial.println("NetSerial: started on TCP port 23 (post-OTA)");
      }
#endif
    }

    // While in OTA window, skip normal effect rendering
    if (otaWindowActive) {
      return;
    }
  }
#endif

  // Optional: Serial number input fallback (0-9 to send exact spell)
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '0' && c <= '9') {
      int id = c - '0';
      if (id >= 1 && id <= 4) {
        currentEffect = id;
      }
      if (id == 7) {
        uint16_t b = globalBrightness;
        if (b > BRIGHTNESS_STEP) b -= BRIGHTNESS_STEP; else b = 1;
        globalBrightness = (uint8_t)b;
        FastLED.setBrightness(globalBrightness);
      } else if (id == 8) {
        uint16_t b = globalBrightness;
        b = (b + BRIGHTNESS_STEP > 255) ? 255 : (b + BRIGHTNESS_STEP);
        globalBrightness = (uint8_t)b;
        FastLED.setBrightness(globalBrightness);
      }
      sendSpell(id);
    }
  }

  // Touch handling (2-pin layout) with combo detection
  unsigned long now = millis();
  
  // Read both pads first
  uint16_t val0 = touchRead(touchChans[0].pin);
  uint16_t val1 = touchRead(touchChans[1].pin);
  uint16_t base0 = touchChans[0].baseline;
  uint16_t base1 = touchChans[1].baseline;
  uint16_t drop0 = (val0 < base0) ? (base0 - val0) : 0;
  uint16_t drop1 = (val1 < base1) ? (base1 - val1) : 0;
  bool isPressed0 = (drop0 >= TOUCH_DELTA);
  bool isPressed1 = (drop1 >= TOUCH_DELTA);
  
  bool wasPressed0 = touchChans[0].pressed;
  bool wasPressed1 = touchChans[1].pressed;
  bool bothPressed = isPressed0 && isPressed1;
  bool wasBothPressed = wasPressed0 && wasPressed1;
  
  // ===== PRESS/RELEASE TRANSITIONS =====
  
  // Pad 0 press (rising edge)
  if (isPressed0 && !wasPressed0) {
    touchChans[0].pressStartMs = now;
    pad0_held = false;
    Serial.printf("Pad 0 pressed at %lu ms\n", now);
  }
  
  // Pad 1 press (rising edge)
  if (isPressed1 && !wasPressed1) {
    touchChans[1].pressStartMs = now;
    pad1_held = false;
    Serial.printf("Pad 1 pressed at %lu ms\n", now);
  }
  
  // Both pressed together (rising edge)
  if (bothPressed && !wasBothPressed) {
    bothPressStartMs = now;
    bothPressedTogether = true;
    Serial.printf("Both pads pressed together at %lu ms\n", now);
  }
  
  // ===== HOLD DETECTION (while pressed) =====
  
  // Pad 0 hold detection
  if (isPressed0 && !pad0_held && (now - touchChans[0].pressStartMs) >= HOLD_THRESHOLD_MS) {
    pad0_held = true;
    Serial.printf("Pad 0 held (> %lu ms)\n", HOLD_THRESHOLD_MS);
  }
  
  // Pad 1 hold detection
  if (isPressed1 && !pad1_held && (now - touchChans[1].pressStartMs) >= HOLD_THRESHOLD_MS) {
    pad1_held = true;
    Serial.printf("Pad 1 held (> %lu ms)\n", HOLD_THRESHOLD_MS);
  }
  
  // ===== COMBO ACTIONS =====
  
  // Hold Pad 0 + Tap Pad 1 (Pad 1 release while Pad 0 still held)
  if (!isPressed1 && wasPressed1 && isPressed0 && pad0_held && !pad1_held) {
    Serial.println("COMBO: Hold Top + Tap Bottom -> Brightness Down");
    uint16_t b = globalBrightness;
    if (b > BRIGHTNESS_STEP) b -= BRIGHTNESS_STEP; else b = 1;
    globalBrightness = (uint8_t)b;
    FastLED.setBrightness(globalBrightness);
    sendSpell(7);  // Brightness down
    Serial.printf("Brightness: %u/255\n", globalBrightness);
  }
  
  // Hold Pad 1 + Tap Pad 0 (Pad 0 release while Pad 1 still held)
  if (!isPressed0 && wasPressed0 && isPressed1 && pad1_held && !pad0_held) {
    Serial.println("COMBO: Hold Bottom + Tap Top -> Brightness Up");
    uint16_t b = globalBrightness;
    b = (b + BRIGHTNESS_STEP > 255) ? 255 : (b + BRIGHTNESS_STEP);
    globalBrightness = (uint8_t)b;
    FastLED.setBrightness(globalBrightness);
    sendSpell(8);  // Brightness up
    Serial.printf("Brightness: %u/255\n", globalBrightness);
  }
  
  // Both held > 0.4s (while both still pressed)
  if (bothPressed && bothPressedTogether && (now - bothPressStartMs) >= BOTH_HOLD_THRESHOLD_MS) {
    Serial.println("COMBO: Both held > 0.4s -> Shoot Animation");
    sendSpell(12);  // One-shot shoot animation
    bothPressedTogether = false;  // Prevent repeated triggers
  }
  
  // ===== SINGLE TAP ACTIONS (only if not part of a combo) =====
  
  if (!bothPressed) {
    // Pad 0 tap (release while not held, and pad 1 not pressed)
    if (!isPressed0 && wasPressed0 && !pad0_held && !isPressed1) {
      Serial.println("TAP: Top Button -> Cycle Effect");
      currentEffect++;
      if (currentEffect > 3) currentEffect = 1;
      sendSpell(currentEffect);
      const char* effectNames[] = {"", "Rainbow", "Breathing", "Off"};
      Serial.printf("Effect: %s\n", effectNames[currentEffect]);
    }
    
    // Pad 1 tap (release while not held, and pad 0 not pressed)
    if (!isPressed1 && wasPressed1 && !pad1_held && !isPressed0) {
      Serial.println("TAP: Bottom Button -> Toggle Tempo");
      static bool tempoFast = false;
      if (tempoFast) {
        tempoFactor = 1.0f;  // normal speed
      } else {
        tempoFactor = 2.0f;  // fast mode
      }
      tempoFast = !tempoFast;
      sendSpell(10);  // Tempo toggle
      Serial.printf("Tempo toggled: %.2fx\n", tempoFactor);
    }
  }
  
  // ===== RELEASE TRANSITIONS =====
  
  // Pad 0 release (falling edge)
  if (!isPressed0 && wasPressed0) {
    Serial.printf("Pad 0 released (held: %s)\n", pad0_held ? "yes" : "no");
  }
  
  // Pad 1 release (falling edge)
  if (!isPressed1 && wasPressed1) {
    Serial.printf("Pad 1 released (held: %s)\n", pad1_held ? "yes" : "no");
  }
  
  // Both released (falling edge)
  if (!bothPressed && wasBothPressed) {
    bothPressedTogether = false;
    Serial.println("Both pads released");
  }
  
  // Update state
  touchChans[0].pressed = isPressed0;
  touchChans[1].pressed = isPressed1;

#if DEBUG_NET_SERIAL
  // Periodic touch diagnostics (raw values vs thresholds)
  if (debugActive && (long)(millis() - nextTouchLogMs) >= 0) {
    nextTouchLogMs = millis() + 200;
    uint16_t v0 = touchRead(touchChans[0].pin);
    uint16_t v1 = touchRead(touchChans[1].pin);
    uint16_t d0 = (v0 < touchChans[0].baseline) ? (touchChans[0].baseline - v0) : 0;
    uint16_t d1 = (v1 < touchChans[1].baseline) ? (touchChans[1].baseline - v1) : 0;
    logBothF("T1 pin=%d val=%u drop=%u base=%u | T2 pin=%d val=%u drop=%u base=%u (thrDelta=%d)\r\n",
      touchChans[0].pin, v0, d0, touchChans[0].baseline,
      touchChans[1].pin, v1, d1, touchChans[1].baseline,
      (int)TOUCH_DELTA
    );
  }
#endif

  // Detect effect changes and reset state
  if (lastEffect != currentEffect) {
    lastEffect = currentEffect;
    switch (currentEffect) {
      case 0:
      case 4:
        backgroundEffect = 0;
        FastLED.clear();
        FastLED.show();
        break;
      case 1:
        backgroundEffect = 1;
        rainbowHue = 0;
        nextRainbowMs = millis();
        break;
      case 2:
        backgroundEffect = 2;
        breathBrightness = globalBrightness / 10;
        breathStep = abs(breathStep);
        nextBreathMs = millis();
        break;
      case 3:
        backgroundEffect = 0;
        FastLED.clear();
        FastLED.show();
        break;
      default:
        break;
    }
  }

  // Render background effect (reuse 'now' from touch handling above)

  switch (backgroundEffect) {
    case 0: {
      // Off
    } break;

    case 1: {
      if ((long)(now - nextRainbowMs) >= 0) {
        nextRainbowMs = now + tempoMs(RAINBOW_INTERVAL_MS);
        for (int i = 0; i < NUM_LEDS_STOLE; i++) {
          uint8_t hue = rainbowHue + (i * 256 / NUM_LEDS_STOLE);
          ledsA[i] = CHSV(hue, 255, globalBrightness);
          ledsB[i] = CHSV(hue, 255, globalBrightness);
        }
        rainbowHue += 1;
      }
    } break;

    case 2: {
      if ((long)(now - nextBreathMs) >= 0) {
        nextBreathMs = now + tempoMs(BREATH_INTERVAL_MS);
        uint8_t maxBreath = globalBrightness;
        uint8_t minBreath = globalBrightness / 10;

        int16_t b = (int16_t)breathBrightness + breathStep;
        if (b >= maxBreath) {
          b = maxBreath;
          breathStep = -breathStep;
        } else if (b <= minBreath) {
          b = minBreath;
          breathStep = -breathStep;
        }
        breathBrightness = (uint8_t)b;

        for (int i = 0; i < NUM_LEDS_STOLE; i++) {
          uint8_t hue = rainbowHue + (i * 256 / NUM_LEDS_STOLE);
          ledsA[i] = CHSV(hue, 255, breathBrightness);
          ledsB[i] = CHSV(hue, 255, breathBrightness);
        }
        rainbowHue += 1;
      }
    } break;


    default:
      backgroundEffect = 0;
      break;
  }

  // Show frame for background effects and/or packet flash overlay
  if (currentEffect >= 0 && currentEffect <= 4) {
    // Overlay short green pixel as TX ack at LED 0
    if (packetFlash) {
      if ((long)(millis() - packetFlashUntil) < 0) {
        ledsA[0] = CRGB::Green;
        ledsA[0].nscale8(globalBrightness);
        ledsB[0] = CRGB::Green;
        ledsB[0].nscale8(globalBrightness);
      } else {
        packetFlash = false;
      }
    }
    FastLED.show();
  }
}

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>

// OTA Configuration
#define OTA_ENABLED 1
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "wizard-hat"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Debug Configuration
// Set to 1 to enable strand length cycling (helps identify physical strand mapping)
// Set to 0 for normal operation
#define DEBUG_MODE 0
#define DEBUG_STRAND_CYCLING 1  // Enable strand length cycling to identify physical mapping

/* ESP32-CAM (AI Thinker) pin notes (summary):
- GPIO13/14/15 are SD interface pins; can be repurposed for WS2812 if SD not used.
- GPIO2 is a boot strap pin (must be HIGH at boot); typically OK for WS2812 data.
- GPIO4 drives onboard flash LED; avoid if you want to keep flash functionality.
- Avoid GPIO1/3 if you need reliable serial logging/programming.
Electrical guidance:
- 330–470 Ω series resistor on each data line near the ESP32-CAM.
- Common ground across ESP32 and LED power rails.
- Large capacitor (e.g., 1000 µF, >=6.3V) across LED power rails.
- Consider a 74HCT level shifter for long runs at 5V LED power.
*/

// Two LED strands on the hat, each using the "stole" count/config
#ifndef LED_PIN_A
#define LED_PIN_A 13
#endif
#ifndef LED_PIN_B
#define LED_PIN_B 14
#endif

#ifndef NUM_LEDS_STOLE
#define NUM_LEDS_STOLE 750
#endif

#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// Dynamic ESP-NOW channel (default 1, pinned as needed)
int espnowChannel = 1;

// Brightness control
uint8_t globalBrightness = 128;
const uint8_t BRIGHTNESS_STEP = 16;

CRGB ledsA[NUM_LEDS_STOLE];
CRGB ledsB[NUM_LEDS_STOLE];

typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket incoming;
volatile int currentEffect = 0;  // updated by callback
volatile bool effectUpdated = false;
volatile bool packetFlash = false;
unsigned long packetFlashUntil = 0;

// Effect state
int lastEffect = -1;
int backgroundEffect = 0;   // 0=off, 1=rainbow, 2=breathing
volatile bool otaInProgress = false;

#if OTA_ENABLED
const unsigned long OTA_WINDOW_MS = 25000;
bool otaWindowActive = false;
unsigned long otaWindowEndMs = 0;
unsigned long otaVisualNextMs = 0;
const unsigned long OTA_VISUAL_INTERVAL_MS = 30;
uint8_t otaVisualHue = 160;
uint8_t otaVisualPos = 0;

// Built-in LED dim blink during OTA window
#ifndef BUILTIN_LED_PIN
#define BUILTIN_LED_PIN 4
#endif
#ifndef LEDC_CHANNEL_BUILTIN
#define LEDC_CHANNEL_BUILTIN 3
#endif
#ifndef LEDC_TIMER_BITS
#define LEDC_TIMER_BITS 8
#endif
#ifndef LEDC_FREQ_HZ
#define LEDC_FREQ_HZ 5000
#endif
bool builtinLedReady = false;
unsigned long builtinLedNextToggleMs = 0;
const unsigned long BUILTIN_LED_TOGGLE_MS = 300;
#endif

// Tempo control for effects
float tempoFactor = 1.0f;
const float TEMPO_MIN = 0.25f;
const float TEMPO_MAX = 4.0f;
inline unsigned long tempoMs(unsigned long baseMs) {
  float scaled = baseMs / tempoFactor;
  if (scaled < 1.0f) scaled = 1.0f;
  return (unsigned long)scaled;
}

// Rainbow
uint8_t rainbowHue = 0;
unsigned long nextRainbowMs = 0;
const unsigned long RAINBOW_INTERVAL_MS = 20;

// Breathing
uint8_t breathBrightness = 0;
int8_t breathStep = 4;
unsigned long nextBreathMs = 0;
const unsigned long BREATH_INTERVAL_MS = 15;


#if DEBUG_MODE
int debugEffectIndex = 0;
unsigned long nextDebugEffectMs = 0;
const unsigned long DEBUG_EFFECT_DURATION_MS = 1000;
const int DEBUG_EFFECTS[] = {1, 2, 3, 0};
const int DEBUG_EFFECTS_COUNT = sizeof(DEBUG_EFFECTS) / sizeof(DEBUG_EFFECTS[0]);
#endif

#if DEBUG_STRAND_CYCLING
// Strand length cycling for physical mapping identification
int strandCycleIndex = 0;
unsigned long nextStrandCycleMs = 0;
const unsigned long STRAND_CYCLE_DURATION_MS = 3000;  // 3 seconds per length
const int STRAND_LENGTHS[] = {100, 200, 300, 400, 500, 600, 750};  // Different lengths to test
const int STRAND_LENGTHS_COUNT = sizeof(STRAND_LENGTHS) / sizeof(STRAND_LENGTHS[0]);
int currentTestLength = STRAND_LENGTHS[0];
#endif

void onRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len >= (int)sizeof(SpellPacket)) {
    memcpy((void*)&incoming, incomingData, sizeof(incoming));
    int spell = incoming.effect_id;
    currentEffect = spell;

    // Spells mapping:
    // 1-4: set background; 5-8: tempo/brightness controls
    if (spell == 5) {
      tempoFactor *= 0.85f;
      if (tempoFactor < TEMPO_MIN) tempoFactor = TEMPO_MIN;
    } else if (spell == 6) {
      tempoFactor *= 1.15f;
      if (tempoFactor > TEMPO_MAX) tempoFactor = TEMPO_MAX;
    } else if (spell == 7) {
      uint16_t b = globalBrightness;
      if (b > BRIGHTNESS_STEP) b -= BRIGHTNESS_STEP; else b = 1;
      globalBrightness = (uint8_t)b;
      FastLED.setBrightness(globalBrightness);
    } else if (spell == 8) {
      uint16_t b = globalBrightness;
      b = (b + BRIGHTNESS_STEP > 255) ? 255 : (b + BRIGHTNESS_STEP);
      globalBrightness = (uint8_t)b;
      FastLED.setBrightness(globalBrightness);
    }

    effectUpdated = true;
    packetFlash = true;
    packetFlashUntil = millis() + 120;
  }
}

static void reinitEspNow() {
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error re-initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onRecv);
  esp_wifi_set_channel((uint8_t)espnowChannel, WIFI_SECOND_CHAN_NONE);
  Serial.printf("ESP-NOW reinitialized on channel %d\n", WiFi.channel());
}

void setup() {
  Serial.begin(115200);
  // Setup built-in LED PWM for status
#if OTA_ENABLED
  ledcSetup(LEDC_CHANNEL_BUILTIN, LEDC_FREQ_HZ, LEDC_TIMER_BITS);
  ledcAttachPin(BUILTIN_LED_PIN, LEDC_CHANNEL_BUILTIN);
  ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
  builtinLedReady = true;
#endif

  // Initialize LEDs
  FastLED.addLeds<LED_TYPE, LED_PIN_A, COLOR_ORDER>(ledsA, NUM_LEDS_STOLE);
  FastLED.addLeds<LED_TYPE, LED_PIN_B, COLOR_ORDER>(ledsB, NUM_LEDS_STOLE);
  FastLED.setBrightness(globalBrightness);
  FastLED.clear();
  FastLED.show();
  Serial.println("Wizard Hat initialized");
  Serial.printf("Strand A: %d LEDs @ pin %d\n", NUM_LEDS_STOLE, LED_PIN_A);
  Serial.printf("Strand B: %d LEDs @ pin %d\n", NUM_LEDS_STOLE, LED_PIN_B);
  Serial.printf("Global brightness: %u/255\n", globalBrightness);
  Serial.println("Hat is ready to receive spells from the staff!");
  currentEffect = 1;  // start with rainbow

#if OTA_ENABLED
  Serial.println("Connecting to WiFi for OTA...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);

  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("WiFi channel: %d\n", WiFi.channel());
    espnowChannel = 1;
    Serial.printf("ESP-NOW channel forced to %d\n", espnowChannel);

    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]() {
      otaInProgress = true;
      backgroundEffect = 0;
      FastLED.clear();
      FastLED.show();
    });

    ArduinoOTA.onEnd([]() {
      fill_solid(ledsA, NUM_LEDS_STOLE, CRGB::Green);
      fill_solid(ledsB, NUM_LEDS_STOLE, CRGB::Green);
      FastLED.show();
      delay(200);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      if (total == 0) return;
      uint32_t pct = (static_cast<uint32_t>(progress) * 100U) / static_cast<uint32_t>(total);
      static uint32_t lastPct = 101U;
      if (pct != lastPct) {
        lastPct = pct;
        Serial.printf("Progress: %u%%\r", pct);
      }

      const uint32_t totalLeds = (uint32_t)NUM_LEDS_STOLE * 2U;
      uint32_t lit = (total > 0) ? ((uint64_t)progress * totalLeds) / total : 0;

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
      fill_solid(ledsA, NUM_LEDS_STOLE, CRGB::Red);
      fill_solid(ledsB, NUM_LEDS_STOLE, CRGB::Red);
      FastLED.show();
      delay(1000);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
    });

    ArduinoOTA.begin();
    Serial.println("OTA Ready");
    Serial.printf("Hostname: %s\n", OTA_HOSTNAME);
    otaWindowActive = true;
    otaWindowEndMs = millis() + OTA_WINDOW_MS;
    Serial.printf("OTA upload window active for %lu ms\n", OTA_WINDOW_MS);
  } else {
    Serial.println("\nWiFi failed; OTA disabled. Using ESP-NOW only...");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_STA);
    delay(100);
    esp_wifi_set_channel((uint8_t)espnowChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("ESP-NOW only mode on channel %d (STA)\n", espnowChannel);
  }
#else
  WiFi.mode(WIFI_STA);
#endif

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onRecv);

#if DEBUG_MODE
  Serial.println("DEBUG MODE: effect cycling");
  nextDebugEffectMs = millis() + DEBUG_EFFECT_DURATION_MS;
  currentEffect = DEBUG_EFFECTS[0];
  backgroundEffect = currentEffect;
#endif
}

void loop() {
#if OTA_ENABLED
  if (otaWindowActive) {
    ArduinoOTA.handle();

    if (!otaInProgress) {
      unsigned long now = millis();
      if ((long)(now - otaVisualNextMs) >= 0) {
        otaVisualNextMs = now + OTA_VISUAL_INTERVAL_MS;
        FastLED.clear();
        int head = otaVisualPos % NUM_LEDS_STOLE;
        ledsA[head] = CHSV(otaVisualHue, 220, globalBrightness);
        ledsB[head] = CHSV(otaVisualHue + 64, 220, globalBrightness);
        if (NUM_LEDS_STOLE > 1) {
          int tA = (head + NUM_LEDS_STOLE - 1) % NUM_LEDS_STOLE;
          int tB = tA;
          ledsA[tA] = CHSV(otaVisualHue, 220, globalBrightness / 4);
          ledsB[tB] = CHSV(otaVisualHue + 64, 220, globalBrightness / 4);
        }
        otaVisualPos = (otaVisualPos + 1) % NUM_LEDS_STOLE;
        otaVisualHue++;
        FastLED.show();

        // Dim pulsing built-in LED during OTA window (very low peak)
#if OTA_ENABLED
        if (builtinLedReady) {
          static uint8_t phase = 0; // 0..255
          phase += 4; // pulse speed
          uint8_t tri = (phase < 128) ? phase : (255 - phase); // 0..127 triangle wave
          const uint8_t MAX_DUTY = 8; // very dim peak
          uint8_t duty = (uint16_t)tri * MAX_DUTY / 127;
          ledcWrite(LEDC_CHANNEL_BUILTIN, duty);
        }
#endif
      }
    }

    if ((long)(millis() - otaWindowEndMs) >= 0 && !otaInProgress) {
      otaWindowActive = false;
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_STA);
      delay(100);
      esp_wifi_set_channel((uint8_t)espnowChannel, WIFI_SECOND_CHAN_NONE);
      reinitEspNow();
      FastLED.clear();
      FastLED.show();
#if OTA_ENABLED
      if (builtinLedReady) {
        ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
      }
#endif
      Serial.printf("OTA window closed; switched to ESP-NOW STA mode on channel %d\n", espnowChannel);
    }

    if (otaWindowActive) {
      return;
    }
  }
#endif

  if (effectUpdated) {
    effectUpdated = false;
    Serial.printf("Received effect %d\n", (int)currentEffect);
  }

  if (lastEffect != currentEffect) {
    lastEffect = currentEffect;
    switch (currentEffect) {
      case 0:
      case 3:
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
      default:
        break;
    }
  }

  unsigned long now = millis();

  if (otaInProgress) {
    return;
  }

#if DEBUG_STRAND_CYCLING
  // Strand length cycling mode - helps identify which physical strand is which
  if ((long)(now - nextStrandCycleMs) >= 0) {
    strandCycleIndex = (strandCycleIndex + 1) % STRAND_LENGTHS_COUNT;
    currentTestLength = STRAND_LENGTHS[strandCycleIndex];
    nextStrandCycleMs = now + STRAND_CYCLE_DURATION_MS;
    
    Serial.printf("STRAND CYCLING: Testing length %d LEDs (Pin A=13: RED, Pin B=14: BLUE)\n", currentTestLength);
    
    // Clear all LEDs first
    FastLED.clear();
    
    // Light up Strand A (pin 13) in RED for the test length
    for (int i = 0; i < currentTestLength && i < NUM_LEDS_STOLE; i++) {
      ledsA[i] = CRGB::Red;
    }
    
    // Light up Strand B (pin 14) in BLUE for the test length
    for (int i = 0; i < currentTestLength && i < NUM_LEDS_STOLE; i++) {
      ledsB[i] = CRGB::Blue;
    }
    
    FastLED.show();
  }
  // Skip normal effects when strand cycling is active
  return;
#endif

#if DEBUG_MODE
  if ((long)(now - nextDebugEffectMs) >= 0) {
    debugEffectIndex = (debugEffectIndex + 1) % DEBUG_EFFECTS_COUNT;
    currentEffect = DEBUG_EFFECTS[debugEffectIndex];
    nextDebugEffectMs = now + DEBUG_EFFECT_DURATION_MS;
    const char* effectNames[] = {"Off", "Rainbow", "Breathing"};
    Serial.printf("DEBUG: Switching to %d (%s)\n", currentEffect, currentEffect < 3 ? effectNames[currentEffect] : "Unknown");
  }
#endif

  // Render background effect
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

  if (currentEffect >= 0 && currentEffect <= 4) {
    FastLED.show();
  }

  // Brief green flash at index 0 on packet receipt
  if (!otaWindowActive && !otaInProgress && packetFlash) {
    if ((long)(millis() - packetFlashUntil) < 0) {
      ledsA[0] = CRGB::Green;
      ledsA[0].nscale8(globalBrightness);
      ledsB[0] = CRGB::Green;
      ledsB[0].nscale8(globalBrightness);
      FastLED.show();
    } else {
      packetFlash = false;
    }
  }
}

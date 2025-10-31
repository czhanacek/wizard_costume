#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <esp_wifi.h>
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

// OTA Configuration
// Set your WiFi credentials for OTA updates
// When OTA is enabled, the device will connect to WiFi for updates
// Note: ESP-NOW and WiFi station mode can coexist
#define OTA_ENABLED 1
#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "wizard-cape"  // Default hostname; override via build_flags
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""        // Set via .env -> build_flags; leave blank by default
#endif

 // WiFi credentials for OTA (only used when OTA_ENABLED is 1)
 // Provided via build flags from .env (WIFI_SSID / WIFI_PASSWORD)
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// Debug Configuration
// Set to 1 to enable demo mode (automatic effect cycling every 3 seconds)
// Set to 0 to disable demo mode (normal ESP-NOW operation only)
#define DEBUG_MODE 0

/*
// LED Strip Configuration
ESP32-CAM (AI Thinker) pin notes:
- GPIO13/14/15 are SD interface pins. Safe to repurpose for WS2812 data if the SD card is not used.
- GPIO2 is a boot strap pin (should be HIGH at boot). WS2812 DIN is high-impedance at reset; board has a pull-up, typically OK.
- GPIO12 is also a strap pin (affects flash voltage). Leave disabled unless necessary; ensure no external pull-up that forces HIGH at boot.
- GPIO4 controls the onboard flash LED. Using it for a strip disables the flashlight functionality.
- Avoid GPIO1/3 (UART0) if you need reliable serial logging/programming.
Electrical guidance:
- Add a 330–470 Ω series resistor on each data line near the ESP32-CAM.
- Common ground between ESP32-CAM and all LED power supplies is required.
- Place a large capacitor (e.g., 1000 µF, >=6.3V) across LED power rails.
- ESP32 outputs 3.3V; WS2812B often accepts 3.3V data at 5V power, but a 74HCT level shifter is recommended for long runs or reliability.
*/
#define NUM_STRIPS 4
#define LED_PIN_1 13      // Strip A
#define LED_PIN_2 14      // Strip B
#define LED_PIN_3 15      // Strip C
#define LED_PIN_4 2       // Strip D
// Reserved for future expansion (disabled):
// #define LED_PIN_5 12    // Use with care (boot strap pin)
// #define LED_PIN_6 4     // Conflicts with on-board flash LED

// Hat strand (connected to cape)
// GPIO12 is a boot strap pin but safe for LED data after boot
// Alternative: GPIO16 if GPIO12 causes issues
#ifndef LED_PIN_STOLE
#define LED_PIN_STOLE 12   // GPIO12 - Hat LEDs (900 LEDs - 3.6x original)
#endif
#ifndef NUM_LEDS_STOLE
#define NUM_LEDS_STOLE 250  // Match staff LED count for stole/hat port
#endif

#define NUM_LEDS 250       // LEDs per strip
#define LED_TYPE WS2812B  // LED strip type (WS2812B)
#define COLOR_ORDER GRB   // Color order for WS2812B strips (typically GRB)
// Dynamic ESP-NOW channel (defaults to 1, updated to AP channel if connected during OTA)
int espnowChannel = 1;

// Global brightness control (0-255)
// Adjust this value to change maximum brightness for all effects
uint8_t globalBrightness = 128;  // Full brightness (adjust as needed)
const uint8_t BRIGHTNESS_STEP = 16; // Step used by spells 7/8

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];
CRGB leds3[NUM_LEDS];
CRGB leds4[NUM_LEDS];
CRGB ledsStole[NUM_LEDS_STOLE];

typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket incoming;
volatile int currentEffect = 0;  // updated in ISR/callback

// Deferred-work flags/state to keep onRecv minimal and non-blocking
volatile bool effectUpdated = false;   // for deferred Serial logging
volatile bool packetFlash = false;        // transient visual pulse on any received packet
unsigned long packetFlashUntil = 0;
// Control request flags (set in onRecv, handled in loop)
volatile bool tempoDownRequested = false;
volatile bool tempoUpRequested = false;
volatile bool brightnessDownRequested = false;
volatile bool brightnessUpRequested = false;

// Effect state
int lastEffect = -1;
int backgroundEffect = 0;  // Current background effect (0=off, 1=rainbow, 2=breathing)
volatile bool otaInProgress = false;  // Flag to stop effects during OTA
#if OTA_ENABLED
const unsigned long OTA_WINDOW_MS = 25000;  // OTA upload window after boot (25s)
bool otaWindowActive = false;
unsigned long otaWindowEndMs = 0;
// Visual indicator state during OTA window
unsigned long otaVisualNextMs = 0;
const unsigned long OTA_VISUAL_INTERVAL_MS = 30;
uint8_t otaVisualHue = 160; // blue-ish indicator
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
uint8_t builtinLedDuty = 0;
unsigned long builtinLedNextToggleMs = 0;
const unsigned long BUILTIN_LED_TOGGLE_MS = 300;
#endif

 // Tempo control (applies to all background effects)
float tempoFactor = 1.0f;        // 1.0 = normal speed
const float TEMPO_MIN = 0.25f;   // 0.25x (very slow)
const float TEMPO_MAX = 4.0f;    // 4x (very fast)
inline unsigned long tempoMs(unsigned long baseMs) {
  float scaled = baseMs / tempoFactor;
  if (scaled < 1.0f) scaled = 1.0f;
  return (unsigned long)scaled;
}

// Background rainbow effect (effect 1)
uint8_t rainbowHue = 0;
unsigned long nextRainbowMs = 0;
const unsigned long RAINBOW_INTERVAL_MS = 20;  // update rate for smooth animation

// Background breathing effect (effect 2)
uint8_t breathBrightness = 0;
int8_t breathStep = 4;                     // brightness step per tick
unsigned long nextBreathMs = 0;
const unsigned long BREATH_INTERVAL_MS = 15;  // update rate



#if DEBUG_MODE
// Debug mode variables for automatic effect cycling
int debugEffectIndex = 0;
unsigned long nextDebugEffectMs = 0;
const unsigned long DEBUG_EFFECT_DURATION_MS = 1000;  // 1 second per effect
const int DEBUG_EFFECTS[] = {1, 2, 3, 0};  // Effects to cycle through
const int DEBUG_EFFECTS_COUNT = sizeof(DEBUG_EFFECTS) / sizeof(DEBUG_EFFECTS[0]);
#endif

void onRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Keep callback minimal: parse/copy and set flags only
  if (len >= (int)sizeof(SpellPacket)) {
    memcpy((void*)&incoming, incomingData, sizeof(incoming));
    int spell = incoming.effect_id;
    currentEffect = spell; // keep for logging

    // Map spells:
    // 1-4: set base background effect (4=Off)
    // 5: tempo down, 6: tempo up, 7: brightness down, 8: brightness up
    if (spell == 5) {
      tempoDownRequested = true;
    } else if (spell == 6) {
      tempoUpRequested = true;
    } else if (spell == 7) {
      brightnessDownRequested = true;
    } else if (spell == 8) {
      brightnessUpRequested = true;
    }

    // Signal loop() to do any heavier work
    effectUpdated = true;   // deferred Serial logging
    // Visual pulse to confirm radio reception regardless of effect mapping
    packetFlash = true;
    packetFlashUntil = millis() + 120; // ~120ms green blip
  }
}

static void reinitEspNow() {
  esp_now_deinit();
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error re-initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onRecv);
  // Ensure radio channel is explicitly set for ESP-NOW
  uint8_t ch = (WiFi.status() == WL_CONNECTED) ? (uint8_t)WiFi.channel() : (uint8_t)espnowChannel;
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  espnowChannel = ch;
  Serial.printf("ESP-NOW reinitialized on channel %d\n", WiFi.channel());
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("WS2812B LED Strip Cape (with NetSerial)");
#if DEBUG_NET_SERIAL
  Serial.println("NetSerial: will start after OTA window (post-OTA).");
#endif
  // Built-in LED (GPIO4) not used - hat LEDs on GPIO12 instead
  builtinLedReady = false;

  // Removed strand diagnostic mode prior to OTA initialization

  // Initialize FastLED for 4 strips + stole
  FastLED.addLeds<LED_TYPE, LED_PIN_1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_2, COLOR_ORDER>(leds2, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_3, COLOR_ORDER>(leds3, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_4, COLOR_ORDER>(leds4, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, LED_PIN_STOLE, COLOR_ORDER>(ledsStole, NUM_LEDS_STOLE);
  FastLED.setBrightness(globalBrightness);  // Use global brightness setting
  FastLED.clear();
  FastLED.show();
  logBothLn("WS2812B LED Strip Cape initialized");
  logBothF("Controlling %d LEDs per strip across %d strips on pins: %d,%d,%d,%d\n", NUM_LEDS, NUM_STRIPS, LED_PIN_1, LED_PIN_2, LED_PIN_3, LED_PIN_4);
  logBothF("Stole strand: %d LEDs on pin %d\n", NUM_LEDS_STOLE, LED_PIN_STOLE);
  logBothF("Global brightness set to: %d/255\n", globalBrightness);
  // Default to a visible background effect so LEDs show after boot
  currentEffect = 1;

  // WiFi/ESP-NOW: Start with SoftAP FIRST to pin channel (CRITICAL - must be before esp_now_init)
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP("cape-sync", "", espnowChannel, 1 /* hidden */);
  delay(100);
  
  // Initialize ESP-NOW AFTER SoftAP is created
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onRecv);
  Serial.printf("ESP-NOW initialized on channel %d\n", espnowChannel);

#if OTA_ENABLED
  // Connect to WiFi for OTA updates
  Serial.println("Connecting to WiFi for OTA...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  // Wait for connection with timeout
  int wifi_retry = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_retry < 20) {
    delay(500);
    Serial.print(".");
    wifi_retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("WiFi channel: %d\n", WiFi.channel());
    
    // Configure OTA
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }
      Serial.println("Start updating " + type);
      // Stop all effects and turn off LEDs during update
      otaInProgress = true;
      backgroundEffect = 0;
      FastLED.clear();
      FastLED.show();
    });
    
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
      // Brief green success flash
      fill_solid(leds1, NUM_LEDS, CRGB::Green);
      fill_solid(leds2, NUM_LEDS, CRGB::Green);
      fill_solid(leds3, NUM_LEDS, CRGB::Green);
      fill_solid(leds4, NUM_LEDS, CRGB::Green);
      fill_solid(ledsStole, NUM_LEDS_STOLE, CRGB::Green);
      FastLED.show();
      delay(200);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
#if DEBUG_NET_SERIAL
      if (!debugActive) {
        debugBegin();
        debugActive = true;
        Serial.println("NetSerial: started on TCP port 23 (post-OTA end)");
      }
#endif
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      if (total == 0) {
        Serial.printf("Progress: %u/%u\r", progress, total);
        return;
      }
      uint32_t pct = (static_cast<uint32_t>(progress) * 100U) / static_cast<uint32_t>(total);
      static uint32_t lastPct = 101U;
      if (pct != lastPct) {
        lastPct = pct;
        Serial.printf("Progress: %u%%\r", pct);
      }

      // Visual OTA progress across all strips (blue bar fill)
      // Map progress 0..total to 0..(NUM_LEDS * NUM_STRIPS)
      const uint32_t totalLeds = (uint32_t)NUM_LEDS * (uint32_t)NUM_STRIPS;
      uint32_t lit = (total > 0) ? ((uint64_t)progress * totalLeds) / total : 0;

      // Clear all LEDs, then fill lit portion in order: strip1 -> strip4
      FastLED.clear();

      uint8_t hue = 160; // blue-ish
      CRGB onColor = CHSV(hue, 255, globalBrightness);

      uint32_t remaining = lit;

      // Strip 1
      uint32_t c1 = remaining > (uint32_t)NUM_LEDS ? (uint32_t)NUM_LEDS : remaining;
      if (c1 > 0) fill_solid(leds1, (int)c1, onColor);
      remaining = (remaining > (uint32_t)NUM_LEDS) ? (remaining - (uint32_t)NUM_LEDS) : 0;

      // Strip 2
      uint32_t c2 = remaining > (uint32_t)NUM_LEDS ? (uint32_t)NUM_LEDS : remaining;
      if (c2 > 0) fill_solid(leds2, (int)c2, onColor);
      remaining = (remaining > (uint32_t)NUM_LEDS) ? (remaining - (uint32_t)NUM_LEDS) : 0;

      // Strip 3
      uint32_t c3 = remaining > (uint32_t)NUM_LEDS ? (uint32_t)NUM_LEDS : remaining;
      if (c3 > 0) fill_solid(leds3, (int)c3, onColor);
      remaining = (remaining > (uint32_t)NUM_LEDS) ? (remaining - (uint32_t)NUM_LEDS) : 0;

      // Strip 4
      uint32_t c4 = remaining > (uint32_t)NUM_LEDS ? (uint32_t)NUM_LEDS : remaining;
      if (c4 > 0) fill_solid(leds4, (int)c4, onColor);

      FastLED.show();
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        Serial.println("Auth Failed");
      } else if (error == OTA_BEGIN_ERROR) {
        Serial.println("Begin Failed");
      } else if (error == OTA_CONNECT_ERROR) {
        Serial.println("Connect Failed");
      } else if (error == OTA_RECEIVE_ERROR) {
        Serial.println("Receive Failed");
      } else if (error == OTA_END_ERROR) {
        Serial.println("End Failed");
      }
      // Flash red on error
      fill_solid(leds1, NUM_LEDS, CRGB::Red);
      fill_solid(leds2, NUM_LEDS, CRGB::Red);
      fill_solid(leds3, NUM_LEDS, CRGB::Red);
      fill_solid(leds4, NUM_LEDS, CRGB::Red);
      fill_solid(ledsStole, NUM_LEDS_STOLE, CRGB::Red);
      FastLED.show();
      delay(1000);
      FastLED.clear();
      FastLED.show();
      otaInProgress = false;
    });
    
    ArduinoOTA.begin();
    Serial.println("OTA Ready");
    Serial.printf("Hostname: %s\n", OTA_HOSTNAME);
    // Start limited OTA window
    otaWindowActive = true;
    otaWindowEndMs = millis() + OTA_WINDOW_MS;
    Serial.printf("OTA upload window active for %lu ms\n", OTA_WINDOW_MS);
  } else {
    Serial.println("\nWiFi connection failed. OTA disabled.");
    Serial.println("Continuing with ESP-NOW only (already initialized)...");
  }
#else
  Serial.println("OTA disabled in build config");
#endif
  
#if DEBUG_MODE
  Serial.println("DEBUG MODE: Automatic effect cycling enabled");
  Serial.println("Effects will cycle every 1 second: Rainbow -> Breathing -> Off");
  nextDebugEffectMs = millis() + DEBUG_EFFECT_DURATION_MS;
  currentEffect = DEBUG_EFFECTS[0];  // Start with first effect
  backgroundEffect = currentEffect;  // Set initial background effect
#endif
}

void loop() {
#if DEBUG_NET_SERIAL
  if (debugActive) debugAcceptClient();
#endif
#if OTA_ENABLED
  // During the initial OTA window, handle OTA and show a special LED indicator.
  if (otaWindowActive) {
    ArduinoOTA.handle();

    if (!otaInProgress) {
      unsigned long now = millis();
      if ((long)(now - otaVisualNextMs) >= 0) {
        otaVisualNextMs = now + OTA_VISUAL_INTERVAL_MS;
        // Render a colorful comet animation across all strips to indicate "upload mode"
        FastLED.clear();

        uint8_t hue0 = otaVisualHue;
        uint8_t hue1 = otaVisualHue + 42;
        uint8_t hue2 = otaVisualHue + 84;
        uint8_t hue3 = otaVisualHue + 126;

        int head = otaVisualPos % NUM_LEDS;

        // Strip 1
        leds1[head] = CHSV(hue0, 220, globalBrightness);
        if (NUM_LEDS > 1) {
          int t1 = (head + NUM_LEDS - 1) % NUM_LEDS;
          leds1[t1] = CHSV(hue0, 220, globalBrightness / 4);
        }

        // Strip 2
        leds2[head] = CHSV(hue1, 220, globalBrightness);
        if (NUM_LEDS > 1) {
          int t2 = (head + NUM_LEDS - 1) % NUM_LEDS;
          leds2[t2] = CHSV(hue1, 220, globalBrightness / 4);
        }

        // Strip 3
        leds3[head] = CHSV(hue2, 220, globalBrightness);
        if (NUM_LEDS > 1) {
          int t3 = (head + NUM_LEDS - 1) % NUM_LEDS;
          leds3[t3] = CHSV(hue2, 220, globalBrightness / 4);
        }

        // Strip 4
        leds4[head] = CHSV(hue3, 220, globalBrightness);
        if (NUM_LEDS > 1) {
          int t4 = (head + NUM_LEDS - 1) % NUM_LEDS;
          leds4[t4] = CHSV(hue3, 220, globalBrightness / 4);
        }

        otaVisualPos = (otaVisualPos + 1) % NUM_LEDS;
        otaVisualHue++; // slowly cycle hues for a prettier effect
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
      // Prepare ESP-NOW mode. If STA is connected, keep it and adopt its channel.
      if (WiFi.status() == WL_CONNECTED) {
        WiFi.mode(WIFI_AP_STA);
        uint8_t chNow = (uint8_t)WiFi.channel();
        WiFi.softAP("cape-sync", "", chNow, 1 /* hidden */);
        delay(100);
        espnowChannel = chNow;
      } else {
        WiFi.disconnect(true, true);
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP("cape-sync", "", espnowChannel, 1 /* hidden */);
        delay(100);
      }
      reinitEspNow();
      FastLED.clear();
      FastLED.show();
      // Turn off built-in LED after OTA window closes
      if (builtinLedReady) {
        ledcWrite(LEDC_CHANNEL_BUILTIN, 0);
      }
      logBothF("OTA window closed; switching to ESP-NOW receiver mode on channel %d\n", espnowChannel);
      logBothF("Current channel after switch: %d\n", WiFi.channel());
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

  // Deferred logging to avoid Serial in callback
  if (effectUpdated) {
    effectUpdated = false;
    int effect = currentEffect; // read once
    logBothF("Received effect %d\n", effect);
  }

  // Handle control requests from spells 5-8
  if (tempoDownRequested) {
    tempoDownRequested = false;
    tempoFactor *= 0.85f; // slow down ~15%
    if (tempoFactor < TEMPO_MIN) tempoFactor = TEMPO_MIN;
    logBothF("Tempo decreased. tempoFactor=%.2f\n", tempoFactor);
  }
  if (tempoUpRequested) {
    tempoUpRequested = false;
    tempoFactor *= 1.15f; // speed up ~15%
    if (tempoFactor > TEMPO_MAX) tempoFactor = TEMPO_MAX;
    logBothF("Tempo increased. tempoFactor=%.2f\n", tempoFactor);
  }
  if (brightnessDownRequested) {
    brightnessDownRequested = false;
    uint16_t b = globalBrightness;
    if (b > BRIGHTNESS_STEP) b -= BRIGHTNESS_STEP; else b = 1;
    globalBrightness = (uint8_t)b;
    FastLED.setBrightness(globalBrightness);
    logBothF("Brightness decreased to %u/255\n", globalBrightness);
  }
  if (brightnessUpRequested) {
    brightnessUpRequested = false;
    uint16_t b = globalBrightness;
    b = (b + BRIGHTNESS_STEP > 255) ? 255 : (b + BRIGHTNESS_STEP);
    globalBrightness = (uint8_t)b;
    FastLED.setBrightness(globalBrightness);
    logBothF("Brightness increased to %u/255\n", globalBrightness);
  }

  // Detect effect change and reset state as needed
  if (lastEffect != currentEffect) {
    lastEffect = currentEffect;

    // Reset per-effect state for background effects only
    switch (currentEffect) {
      case 0: // Off - clear background effect
        backgroundEffect = 0;
        FastLED.clear();
        FastLED.show();
        break;
      case 1: // Background rainbow
        backgroundEffect = 1;
        rainbowHue = 0;
        nextRainbowMs = millis();
        break;
      case 2: // Background breathing
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
      case 4: // Off (spell 4)
        backgroundEffect = 0;
        FastLED.clear();
        FastLED.show();
        break;
      default:
        // Ignore non-background spells (5-8) here
        break;
    }
  }

  unsigned long now = millis();

  // Skip all effects if OTA is in progress
  if (otaInProgress) {
    return;
  }

#if DEBUG_MODE
  // Debug mode: automatically cycle background effects
  if ((long)(now - nextDebugEffectMs) >= 0) {
    debugEffectIndex = (debugEffectIndex + 1) % DEBUG_EFFECTS_COUNT;
    currentEffect = DEBUG_EFFECTS[debugEffectIndex];
    nextDebugEffectMs = now + DEBUG_EFFECT_DURATION_MS;
    
    const char* effectNames[] = {"Off", "Rainbow", "Breathing"};
    Serial.printf("DEBUG: Switching to background effect %d (%s)\n", currentEffect, 
                  currentEffect < 3 ? effectNames[currentEffect] : "Unknown");
  }
#endif

  // First, render background effect (if any)
  switch (backgroundEffect) {
    case 0: {
      // Off - no background effect
    } break;

    case 1: {
      // Background Rainbow: continuous rainbow cycling
      if ((long)(now - nextRainbowMs) >= 0) {
        nextRainbowMs = now + tempoMs(RAINBOW_INTERVAL_MS);

        // Create rainbow effect across all LEDs
        for (int i = 0; i < NUM_LEDS; i++) {
          uint8_t ledHue = rainbowHue + (i * 256 / NUM_LEDS);
          leds1[i] = CHSV(ledHue, 255, globalBrightness);
          leds2[i] = CHSV(ledHue, 255, globalBrightness);
          leds3[i] = CHSV(ledHue, 255, globalBrightness);
          leds4[i] = CHSV(ledHue, 255, globalBrightness);
        }
        // Stole strand (scaled to its length)
        for (int j = 0; j < NUM_LEDS_STOLE; j++) {
          uint8_t ledHue2 = rainbowHue + (j * 256 / NUM_LEDS_STOLE);
          ledsStole[j] = CHSV(ledHue2, 255, globalBrightness);
        }

        rainbowHue += 1;  // wraps at 256
      }
    } break;

    case 2: {
      // Background Breathing: continuous breathing effect
      if ((long)(now - nextBreathMs) >= 0) {
        nextBreathMs = now + tempoMs(BREATH_INTERVAL_MS);

        uint8_t maxBreath = globalBrightness;
        uint8_t minBreath = globalBrightness / 10;

        // Update brightness with bounds
        int16_t b = (int16_t)breathBrightness + breathStep;
        if (b >= maxBreath) {
          b = maxBreath;
          breathStep = -breathStep; // start decreasing
        } else if (b <= minBreath) {
          b = minBreath;
          breathStep = -breathStep; // start increasing
        }
        breathBrightness = (uint8_t)b;

        // Apply rainbow with breathing brightness
        for (int i = 0; i < NUM_LEDS; i++) {
          uint8_t ledHue = rainbowHue + (i * 256 / NUM_LEDS);
          leds1[i] = CHSV(ledHue, 255, breathBrightness);
          leds2[i] = CHSV(ledHue, 255, breathBrightness);
          leds3[i] = CHSV(ledHue, 255, breathBrightness);
          leds4[i] = CHSV(ledHue, 255, breathBrightness);
        }
        // Stole strand (scaled to its length)
        for (int j = 0; j < NUM_LEDS_STOLE; j++) {
          uint8_t ledHue2 = rainbowHue + (j * 256 / NUM_LEDS_STOLE);
          ledsStole[j] = CHSV(ledHue2, 255, breathBrightness);
        }

        // Step hue slowly for variation
        rainbowHue += 1;
      }
    } break;


    default: {
      // Unknown background effect, turn off
      backgroundEffect = 0;
    } break;
  }

  // No one-shot effects; ensure LEDs update when only background is active
  if (currentEffect >= 0 && currentEffect <= 4) {
    FastLED.show();
  }

  // Brief green flash on LED 0 to acknowledge any received packet
  if (!otaWindowActive && !otaInProgress && packetFlash) {
    if ((long)(millis() - packetFlashUntil) < 0) {
      // Overlay a bright green pixel on ALL strips to make it very visible
      leds1[0] = CRGB::Green;
      leds2[0] = CRGB::Green;
      leds3[0] = CRGB::Green;
      leds4[0] = CRGB::Green;
      ledsStole[0] = CRGB::Green;
      FastLED.show();
    } else {
      packetFlash = false;
    }
  }

  // Other non-blocking work can go here
}

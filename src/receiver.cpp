#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <FastLED.h>

// Debug Configuration
// Set to 1 to enable demo mode (automatic effect cycling every 3 seconds)
// Set to 0 to disable demo mode (normal ESP-NOW operation only)
#define DEBUG_MODE 0

// LED Strip Configuration
#define LED_PIN 2        // GPIO pin connected to LED strip data line
#define NUM_LEDS 50      // Number of LEDs to control
#define LED_TYPE WS2812B // LED strip type (WS2812B)
#define COLOR_ORDER GRB  // Color order for WS2812B strips (typically GRB)

// Global brightness control (0-255)
// Adjust this value to change maximum brightness for all effects
uint8_t globalBrightness = 128;  // Full brightness (adjust as needed)

CRGB leds[NUM_LEDS];

typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket incoming;
volatile int currentEffect = 0;  // updated in ISR/callback

// Deferred-work flags/state to keep onRecv minimal and non-blocking
volatile bool effectUpdated = false;   // for deferred Serial logging
volatile bool rainbowRequested = false;   // used for effect 1 (one-shot rainbow)
volatile bool breathRequested = false;    // used for effect 2 (one-shot breathing)
volatile bool strobeRequested = false;    // used for effect 3 (one-shot strobe)
volatile bool chaseRequested = false;     // used for effect 5 (one-shot LED chase)

// Effect state
int lastEffect = -1;

// One-shot rainbow effect (effect 1)
bool rainbowActive = false;
uint8_t rainbowHue = 0;
uint8_t rainbowCycles = 0;
const uint8_t RAINBOW_MAX_CYCLES = 3;      // Number of full rainbow cycles
unsigned long nextRainbowMs = 0;
const unsigned long RAINBOW_INTERVAL_MS = 20;  // update rate for smooth animation

// One-shot breathing effect (effect 2)
bool breathActive = false;
uint8_t breathBrightness = 0;
int8_t breathStep = 4;                     // brightness step per tick
uint8_t breathCycles = 0;
const uint8_t BREATH_MAX_CYCLES = 2;       // Number of breath cycles
unsigned long nextBreathMs = 0;
const unsigned long BREATH_INTERVAL_MS = 15;  // update rate

// One-shot strobe effect (effect 3)
bool strobeActive = false;
bool strobeOn = false;
uint8_t strobeFlashes = 0;
const uint8_t STROBE_MAX_FLASHES = 6;      // Number of flashes
unsigned long nextStrobeMs = 0;
const unsigned long STROBE_ON_MS = 60;
const unsigned long STROBE_OFF_MS = 140;

// Chase effect (effect 5) - supports multiple simultaneous chasers
bool chaseActive = false;
unsigned long nextChaseMs = 0;
const unsigned long CHASE_INTERVAL_MS = 50; // Speed of chase (milliseconds per LED)
const int MAX_CHASERS = 10;                // Maximum number of simultaneous chasers

struct Chaser {
  int position;                            // Current LED position (-1 means inactive)
  bool active;                             // Whether this chaser is active
};

Chaser chasers[MAX_CHASERS];

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
    currentEffect = incoming.effect_id;

    // Trigger requested one-shot based on effect
    if (currentEffect == 1) {
      rainbowRequested = true;
    } else if (currentEffect == 2) {
      breathRequested = true;
    } else if (currentEffect == 3) {
      strobeRequested = true;
    } else if (currentEffect == 5) {
      chaseRequested = true;
    }

    // Signal loop() to do any heavier work
    effectUpdated = true;   // deferred Serial logging
  }
}

void setup() {
  Serial.begin(115200);

  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(globalBrightness);  // Use global brightness setting
  FastLED.clear();
  FastLED.show();
  Serial.println("WS2812B LED Strip Receiver initialized");
  Serial.printf("Controlling %d LEDs on pin %d\n", NUM_LEDS, LED_PIN);
  Serial.printf("Global brightness set to: %d/255\n", globalBrightness);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onRecv);
  
#if DEBUG_MODE
  Serial.println("DEBUG MODE: Automatic effect cycling enabled");
  Serial.println("Effects will cycle every 1 second: Rainbow -> Breathing -> Strobe -> Off");
  nextDebugEffectMs = millis() + DEBUG_EFFECT_DURATION_MS;
  currentEffect = DEBUG_EFFECTS[0];  // Start with first effect
  if (currentEffect == 1) {
    rainbowRequested = true;
  } else if (currentEffect == 2) {
    breathRequested = true;
  } else if (currentEffect == 3) {
    strobeRequested = true;
  }
#endif
}

void loop() {
  // Deferred logging to avoid Serial in callback
  if (effectUpdated) {
    effectUpdated = false;
    int effect = currentEffect; // read once
    Serial.printf("Received effect %d\n", effect);
  }

  // Detect effect change and reset state as needed
  if (lastEffect != currentEffect) {
    lastEffect = currentEffect;

    // Reset per-effect state
    switch (currentEffect) {
      case 0: // Off
        FastLED.clear();
        FastLED.show();
        break;
      case 1: // One-shot rainbow trigger
        rainbowRequested = true;
        nextRainbowMs = millis();
        break;
      case 2: // One-shot breathing trigger
        breathRequested = true;
        nextBreathMs = millis();
        break;
      case 3: { // One-shot strobe trigger
        strobeRequested = true;
        nextStrobeMs = millis();
        break;
      }
      case 5: { // Multi-chase trigger - initialize chasers array
        // Initialize all chasers as inactive when entering chase mode
        for (int i = 0; i < MAX_CHASERS; i++) {
          chasers[i].active = false;
          chasers[i].position = -1;
        }
        chaseActive = false;
        chaseRequested = true;
        nextChaseMs = millis();
        break;
      }
      default:
        // Default to rainbow cycling for unknown effects
        currentEffect = 1;
        rainbowHue = 0;
        nextRainbowMs = millis();
        break;
    }
  }

  unsigned long now = millis();

#if DEBUG_MODE
  // Debug mode: automatically trigger one-shot effects
  if ((long)(now - nextDebugEffectMs) >= 0) {
    debugEffectIndex = (debugEffectIndex + 1) % DEBUG_EFFECTS_COUNT;
    currentEffect = DEBUG_EFFECTS[debugEffectIndex];
    nextDebugEffectMs = now + DEBUG_EFFECT_DURATION_MS;

    if (currentEffect == 1) {
      rainbowRequested = true;
    } else if (currentEffect == 2) {
      breathRequested = true;
    } else if (currentEffect == 3) {
      strobeRequested = true;
    }
    
    const char* effectNames[] = {"Off", "Rainbow", "Breathing", "Strobe"};
    Serial.printf("DEBUG: Triggering one-shot effect %d (%s)\n", currentEffect, 
                  currentEffect < 4 ? effectNames[currentEffect] : "Unknown");
  }
#endif

  // Effect processing (non-blocking state machines)
  switch (currentEffect) {
    case 0: {
      // Off - LEDs already cleared in effect change
      // Nothing to do
    } break;

    case 1: {
      // One-shot Rainbow: run a few full hue cycles, then stop
      if (rainbowRequested) {
        rainbowRequested = false;
        rainbowActive = true;
        rainbowHue = 0;
        rainbowCycles = 0;
        nextRainbowMs = now;
      }

      if (rainbowActive && (long)(now - nextRainbowMs) >= 0) {
        nextRainbowMs = now + RAINBOW_INTERVAL_MS;

        // Create rainbow effect across all LEDs
        for (int i = 0; i < NUM_LEDS; i++) {
          uint8_t ledHue = rainbowHue + (i * 256 / NUM_LEDS);
          leds[i] = CHSV(ledHue, 255, globalBrightness);
        }
        FastLED.show();

        uint8_t prev = rainbowHue;
        rainbowHue += 1;  // wraps at 256
        if (rainbowHue == 0 && prev != 0) {
          // Completed a full hue cycle
          rainbowCycles++;
          if (rainbowCycles >= RAINBOW_MAX_CYCLES) {
            rainbowActive = false;
            FastLED.clear();
            FastLED.show();
            currentEffect = 0;  // back to off
          }
        }
      }
    } break;

    case 2: {
      // One-shot Breathing: run a few full breath cycles, then stop
      if (breathRequested) {
        breathRequested = false;
        breathActive = true;
        breathCycles = 0;
        breathBrightness = globalBrightness / 10; // start dim
        breathStep = abs(breathStep);
        nextBreathMs = now;
      }

      if (breathActive && (long)(now - nextBreathMs) >= 0) {
        nextBreathMs = now + BREATH_INTERVAL_MS;

        uint8_t maxBreath = globalBrightness;
        uint8_t minBreath = globalBrightness / 10;

        // Update brightness with bounds
        int16_t b = (int16_t)breathBrightness + breathStep;
        bool hitMax = false;
        bool hitMin = false;
        if (b >= maxBreath) {
          b = maxBreath;
          breathStep = -breathStep; // start decreasing
          hitMax = true;
        } else if (b <= minBreath) {
          b = minBreath;
          breathStep = -breathStep; // start increasing
          hitMin = true;
        }
        breathBrightness = (uint8_t)b;

        // Apply rainbow with breathing brightness
        for (int i = 0; i < NUM_LEDS; i++) {
          uint8_t ledHue = rainbowHue + (i * 256 / NUM_LEDS);
          leds[i] = CHSV(ledHue, 255, breathBrightness);
        }
        FastLED.show();

        // Step hue slowly for variation
        rainbowHue += 1;

        // Count a cycle when we bounce off the min
        if (hitMin) {
          breathCycles++;
          if (breathCycles >= BREATH_MAX_CYCLES) {
            breathActive = false;
            FastLED.clear();
            FastLED.show();
            currentEffect = 0;  // back to off
          }
        }
      }
    } break;

    case 3: {
      // One-shot Strobe: flash a fixed number of times, then stop
      if (strobeRequested) {
        strobeRequested = false;
        strobeActive = true;
        strobeOn = false;       // will toggle to ON first tick
        strobeFlashes = 0;
        nextStrobeMs = now;
      }

      if (strobeActive && (long)(now - nextStrobeMs) >= 0) {
        strobeOn = !strobeOn;
        if (strobeOn) {
          CRGB strobeColor = CRGB::White;
          strobeColor.nscale8(globalBrightness);
          fill_solid(leds, NUM_LEDS, strobeColor);
          nextStrobeMs = now + STROBE_ON_MS;
        } else {
          FastLED.clear();
          nextStrobeMs = now + STROBE_OFF_MS;
          strobeFlashes++;
          if (strobeFlashes >= STROBE_MAX_FLASHES) {
            strobeActive = false;
            FastLED.show();
            currentEffect = 0; // back to off
            break;
          }
        }
        FastLED.show();
      }
    } break;

    case 5: {
      // Multi-Chase: each spell cast fires off a new LED that chases across the strip
      if (chaseRequested) {
        chaseRequested = false;
        
        // Find an available chaser slot and start a new chase
        for (int i = 0; i < MAX_CHASERS; i++) {
          if (!chasers[i].active) {
            chasers[i].active = true;
            chasers[i].position = 0;  // Start at beginning of strip
            chaseActive = true;       // At least one chaser is active
            nextChaseMs = now;
            break;
          }
        }
      }

      if (chaseActive && (long)(now - nextChaseMs) >= 0) {
        nextChaseMs = now + CHASE_INTERVAL_MS;

        // Clear all LEDs first
        FastLED.clear();
        
        bool anyActive = false;
        
        // Update and render all active chasers
        for (int i = 0; i < MAX_CHASERS; i++) {
          if (chasers[i].active) {
            // Light up this chaser's current position
            if (chasers[i].position >= 0 && chasers[i].position < NUM_LEDS) {
              leds[chasers[i].position] = CRGB::White;
              leds[chasers[i].position].nscale8(globalBrightness);
            }
            
            // Move to next position
            chasers[i].position++;
            
            // Check if this chaser has completed its journey
            if (chasers[i].position >= NUM_LEDS) {
              chasers[i].active = false;  // Deactivate this chaser
            } else {
              anyActive = true;  // At least one chaser is still active
            }
          }
        }
        
        FastLED.show();
        
        // If no chasers are active, turn off chase mode
        if (!anyActive) {
          chaseActive = false;
          // Don't automatically switch to effect 0 - stay in chase mode for next cast
        }
      }
    } break;

    default: {
      // Unknown effect, keep LEDs off
      FastLED.clear();
      FastLED.show();
    } break;
  }

  // Other non-blocking work can go here
}

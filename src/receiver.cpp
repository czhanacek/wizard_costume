#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

typedef struct {
  int effect_id;
} SpellPacket;

SpellPacket incoming;
volatile int currentEffect = 0;  // updated in ISR/callback

#define LED_FLASH 4  // onboard flash LED

// LEDC (PWM) configuration
static const int LEDC_CHANNEL = 0;
static const int LEDC_TIMER_BITS = 13;
static const int LEDC_BASE_FREQ = 5000;  // Hz
static const uint32_t MAX_DUTY = (1u << LEDC_TIMER_BITS) - 1;

// Deferred-work flags/state to keep onRecv minimal and non-blocking
volatile bool pulseRequested = false;  // used for effect 3 (one-shot pulse)
volatile bool sawtoothRequested = false;  // used for effect 4 (one-shot sawtooth)
volatile bool doubleBlinkRequested = false;  // used for effect 5 (one-shot double-blink)
volatile bool effectUpdated = false;   // for deferred Serial logging

// Effect state
int lastEffect = -1;

// Breathing effect (effect 1)
uint32_t breathDuty = 0;
int32_t breathStep = 64;                 // duty step per tick
unsigned long nextBreathMs = 0;
const unsigned long BREATH_INTERVAL_MS = 8;  // update rate

// Strobe effect (effect 2)
bool strobeOn = false;
unsigned long nextStrobeMs = 0;
const unsigned long STROBE_ON_MS = 60;
const unsigned long STROBE_OFF_MS = 140;

// One-shot pulse (effect 3) triggered on receive
bool pulseActive = false;
bool pulseRising = true;
uint32_t pulseDuty = 0;
int32_t pulseStep = 256;                 // duty step per tick
unsigned long nextPulseMs = 0;
const unsigned long PULSE_INTERVAL_MS = 4;  // update rate

// One-shot sawtooth effect (effect 4) triggered on receive
bool sawtoothActive = false;
uint32_t sawDuty = 0;
uint32_t sawStep = 128;                  // duty step per tick
unsigned long nextSawMs = 0;
const unsigned long SAW_INTERVAL_MS = 4; // update rate

// One-shot double-blink effect (effect 5) triggered on receive
bool doubleBlinkActive = false;
int dblStep = 0;                         // 0:on, 1:off-short, 2:on, 3:off-long
unsigned long nextDblMs = 0;
const unsigned long DBL_ON_MS = 80;
const unsigned long DBL_OFF_MS = 80;
const unsigned long DBL_PAUSE_MS = 400;

static inline void applyDuty(uint32_t duty) {
  if (duty > MAX_DUTY) duty = MAX_DUTY;
  ledcWrite(LEDC_CHANNEL, duty);
}

void onRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  // Keep callback minimal: parse/copy and set flags only
  if (len >= (int)sizeof(SpellPacket)) {
    memcpy((void*)&incoming, incomingData, sizeof(incoming));
    currentEffect = incoming.effect_id;

    // Signal loop() to do any heavier work
    effectUpdated = true;   // deferred Serial logging

    // For effects 3, 4, 5, each receive triggers a one-shot effect
    if (currentEffect == 3) {
      pulseRequested = true;
    } else if (currentEffect == 4) {
      sawtoothRequested = true;
    } else if (currentEffect == 5) {
      doubleBlinkRequested = true;
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Configure LED pin and PWM (LEDC)
  pinMode(LED_FLASH, OUTPUT);
  digitalWrite(LED_FLASH, LOW);
  ledcSetup(LEDC_CHANNEL, LEDC_BASE_FREQ, LEDC_TIMER_BITS);
  ledcAttachPin(LED_FLASH, LEDC_CHANNEL);
  applyDuty(0);

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onRecv);
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
        applyDuty(0);
        break;
      case 1: // Breathing
        breathDuty = 0;
        breathStep = abs(breathStep); // start by increasing
        nextBreathMs = millis();
        break;
      case 2: // Strobe
        // Start immediately ON so the change is visible right away
        strobeOn = true;
        applyDuty(MAX_DUTY);
        nextStrobeMs = millis() + STROBE_ON_MS;
        break;
      case 3: // One-shot pulse on each receive; also start a pulse on entering this mode
        pulseActive = false;
        pulseRising = true;
        pulseDuty = 0;
        pulseRequested = true; // kick off first pulse immediately
        nextPulseMs = millis();
        applyDuty(0);
        break;
      case 4: // One-shot sawtooth on each receive
        sawtoothActive = false;
        sawDuty = 0;
        sawtoothRequested = true; // kick off first sawtooth immediately
        nextSawMs = millis();
        applyDuty(0);
        break;
      case 5: // One-shot double-blink on each receive
        doubleBlinkActive = false;
        dblStep = 0;
        doubleBlinkRequested = true; // kick off first double-blink immediately
        nextDblMs = millis();
        applyDuty(0);
        break;
      default:
        break;
    }
  }

  unsigned long now = millis();

  // Effect processing (non-blocking state machines)
  switch (currentEffect) {
    case 0: {
      // Off
      // Nothing to do
    } break;

    case 1: {
      // Breathing: ramp duty 0..MAX..0 continuously
      if ((long)(now - nextBreathMs) >= 0) {
        nextBreathMs = now + BREATH_INTERVAL_MS;

        int32_t d = (int32_t)breathDuty + breathStep;
        if (d >= (int32_t)MAX_DUTY) {
          d = MAX_DUTY;
          breathStep = -breathStep; // start decreasing
        } else if (d <= 0) {
          d = 0;
          breathStep = -breathStep; // start increasing
        }
        breathDuty = (uint32_t)d;
        applyDuty(breathDuty);
      }
    } break;

    case 2: {
      // Strobe: on/off at fixed intervals
      if ((long)(now - nextStrobeMs) >= 0) {
        strobeOn = !strobeOn;
        applyDuty(strobeOn ? MAX_DUTY : 0);
        nextStrobeMs = now + (strobeOn ? STROBE_ON_MS : STROBE_OFF_MS);
      }
    } break;

    case 3: {
      // One-shot pulse per receive: fade in then fade out once
      if (pulseRequested) {
        pulseRequested = false;
        pulseActive = true;
        pulseRising = true;
        pulseDuty = 0;
        nextPulseMs = now; // start immediately
      }

      if (pulseActive && (long)(now - nextPulseMs) >= 0) {
        nextPulseMs = now + PULSE_INTERVAL_MS;

        if (pulseRising) {
          int32_t d = (int32_t)pulseDuty + pulseStep;
          if (d >= (int32_t)MAX_DUTY) {
            d = MAX_DUTY;
            pulseRising = false; // begin fading out
          }
          pulseDuty = (uint32_t)d;
        } else {
          int32_t d = (int32_t)pulseDuty - pulseStep;
          if (d <= 0) {
            d = 0;
            pulseActive = false; // finished
          }
          pulseDuty = (uint32_t)d;
        }

        applyDuty(pulseDuty);
      }
    } break;

    case 4: {
      // One-shot sawtooth per receive: ramp up to MAX then snap to 0 once
      if (sawtoothRequested) {
        sawtoothRequested = false;
        sawtoothActive = true;
        sawDuty = 0;
        nextSawMs = now; // start immediately
      }

      if (sawtoothActive && (long)(now - nextSawMs) >= 0) {
        nextSawMs = now + SAW_INTERVAL_MS;
        uint32_t d = sawDuty + sawStep;
        if (d >= MAX_DUTY) {
          // Reached max, snap to 0 and finish
          sawDuty = 0;
          applyDuty(0);
          sawtoothActive = false; // finished
        } else {
          sawDuty = d;
          applyDuty(sawDuty);
        }
      }
    } break;

    case 5: {
      // One-shot double-blink per receive: on-off-on-off once
      if (doubleBlinkRequested) {
        doubleBlinkRequested = false;
        doubleBlinkActive = true;
        dblStep = 0;
        nextDblMs = now; // start immediately
      }

      if (doubleBlinkActive && (long)(now - nextDblMs) >= 0) {
        switch (dblStep) {
          case 0: // turn on
            applyDuty(MAX_DUTY);
            nextDblMs = now + DBL_ON_MS;
            dblStep = 1;
            break;
          case 1: // short off
            applyDuty(0);
            nextDblMs = now + DBL_OFF_MS;
            dblStep = 2;
            break;
          case 2: // second on
            applyDuty(MAX_DUTY);
            nextDblMs = now + DBL_ON_MS;
            dblStep = 3;
            break;
          case 3: // long pause then finish
            applyDuty(0);
            nextDblMs = now + DBL_PAUSE_MS;
            dblStep = 4;
            break;
          case 4: // finished
            doubleBlinkActive = false;
            applyDuty(0);
            break;
        }
      }
    } break;

    default: {
      // Unknown effect, keep LED off
      applyDuty(0);
    } break;
  }

  // Other non-blocking work can go here
}

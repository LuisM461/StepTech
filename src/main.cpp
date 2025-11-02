#include <Arduino.h>
#include <FastLED.h>

// ----------------------------
// Hardware config (single tile)
// ----------------------------
#define LED_PIN         13
#define SENSOR_PIN      34          // ESP32 ADC1 pin recommended
#define LEDS_PER_TILE   256         // e.g., 16x16
#define LED_COUNT       LEDS_PER_TILE
#define BRIGHTNESS      32

// ----------------------------
// Velostat tuning
// ----------------------------
// If pressed reads LOWER than idle, leave as 1. If pressed reads HIGHER, set to 0.
#define PRESS_ACTIVE_LOW  1
// Pick a threshold between your idle and pressed readings (0..4095 on ESP32 ADC)
#define THRESHOLD         1500
#define DEBOUNCE_MS       150

// ----------------------------
// Timing (ms)
// ----------------------------
#define FLASH_GREEN_MS     300      // startup cue flash
#define FEEDBACK_BLUE_MS   700      // blue confirmation flash after release

CRGB leds[LED_COUNT];

enum State { CUE_FLASH_GREEN, WAIT_FOR_PRESS, PRESSED_HELD, FEEDBACK_BLUE };
State state = CUE_FLASH_GREEN;

uint32_t stateStartMs = 0;

// Edge/debounce tracking
bool lastPressed = false;
uint32_t lastEdgeMs = 0;

// ----------------------------
// Helpers
// ----------------------------
inline bool readPressedRaw() {
  int v = analogRead(SENSOR_PIN);
#if PRESS_ACTIVE_LOW
  return v < THRESHOLD;
#else
  return v > THRESHOLD;
#endif
}

inline void showGreen() {
  fill_solid(leds, LED_COUNT, CRGB::Green);
  FastLED.show();
}

inline void showBlue() {
  fill_solid(leds, LED_COUNT, CRGB::Blue);
  FastLED.show();
}

inline void clearTile() {
  FastLED.clear();
  FastLED.show();
}

inline void enter(State s) {
  state = s;
  stateStartMs = millis();
}

// Debounced rising edge: goes true once when we transition to pressed
bool pressEdge(uint32_t now, bool pressedNow) {
  if (pressedNow && !lastPressed && (now - lastEdgeMs >= DEBOUNCE_MS)) {
    lastPressed = true;
    lastEdgeMs  = now;
    return true;
  }
  return false;
}

// Debounced falling edge: goes true once when we transition to released
bool releaseEdge(uint32_t now, bool pressedNow) {
  if (!pressedNow && lastPressed && (now - lastEdgeMs >= DEBOUNCE_MS)) {
    lastPressed = false;
    lastEdgeMs  = now;
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(100);

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);

  // SENSOR_PIN is analog-input only on many ESP32 boards; no pinMode needed,
  // but it's harmless to set explicitly:
  pinMode(SENSOR_PIN, INPUT);

  // Startup: flash green, then off → wait for press
  showGreen();
  enter(CUE_FLASH_GREEN);
}

void loop() {
  uint32_t now = millis();
  bool pressedNow = readPressedRaw();

  switch (state) {
    case CUE_FLASH_GREEN: {
      if (now - stateStartMs >= FLASH_GREEN_MS) {
        clearTile();                   // turn off and wait
        enter(WAIT_FOR_PRESS);
      }
    } break;

    case WAIT_FOR_PRESS: {
      if (pressEdge(now, pressedNow)) {
        // On initial press: go green and hold it while pressed
        showGreen();
        enter(PRESSED_HELD);
      }
      // no else: keep waiting
    } break;

    case PRESSED_HELD: {
      // Keep it green while the user is pressing (already set in pressEdge)
      if (releaseEdge(now, pressedNow)) {
        // On release: flash blue to confirm recorded "correct" press
        showBlue();
        enter(FEEDBACK_BLUE);
      }
    } break;

    case FEEDBACK_BLUE: {
      if (now - stateStartMs >= FEEDBACK_BLUE_MS) {
        // Loop back: flash green again then wait for next press
        showGreen();
        enter(CUE_FLASH_GREEN);
      }
    } break;
  }
}

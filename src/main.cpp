#include <Arduino.h>
#include <FastLED.h>

// ----------------------------
// Hardware config (single tile)
// ----------------------------
#define LED_PIN         13
#define SENSOR_PIN      34        // ESP32 ADC1 pin recommended
#define LEDS_PER_TILE   256       // your tile size (e.g., 16x16)
#define LED_COUNT       LEDS_PER_TILE
#define BRIGHTNESS      32

// ----------------------------
// Velostat tuning
// ----------------------------
// If pressed reads LOWER than idle, leave as 1. If pressed reads HIGHER, set to 0.
#define PRESS_ACTIVE_LOW  1
// Pick a threshold between your idle and pressed readings (0..4095 on ESP32)
#define THRESHOLD         1500
#define DEBOUNCE_MS       150

// ----------------------------
// Timing (ms)
// ----------------------------
#define SHOW_MS           1200    // how long to show the cue color
#define FEEDBACK_MS       800     // how long to show green after a press

CRGB leds[LED_COUNT];

enum State { SHOW_CUE, WAIT_PRESS, FEEDBACK };
State state = SHOW_CUE;

uint32_t stateStartMs = 0;

bool lastPressed = false;
uint32_t lastPressMs = 0;

// ----------------------------
// Helpers
// ----------------------------
inline bool readPressed() {
  int v = analogRead(SENSOR_PIN);
#if PRESS_ACTIVE_LOW
  return v < THRESHOLD;
#else
  return v > THRESHOLD;
#endif
}

void showCue() {
  // A visible cue color (purple/orange/etc.)
  fill_solid(leds, LED_COUNT, CRGB::Red);  // purple
  FastLED.show();
}

void clearTile() {
  FastLED.clear();
  FastLED.show();
}

void showGreen() {
  fill_solid(leds, LED_COUNT, CRGB::Green);
  FastLED.show();
}

void enter(State s) {
  state = s;
  stateStartMs = millis();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(BRIGHTNESS);
  clearTile();

  pinMode(SENSOR_PIN, INPUT);

  // Start by showing the cue
  showCue();
  enter(SHOW_CUE);
}

void loop() {
  uint32_t now = millis();

  switch (state) {
    case SHOW_CUE: {
      if (now - stateStartMs >= SHOW_MS) {
        clearTile();              // turn off and wait for user
        enter(WAIT_PRESS);
      }
    } break;

    case WAIT_PRESS: {
      bool pressed = readPressed();
      bool newPress = pressed && !lastPressed && (now - lastPressMs > DEBOUNCE_MS);

      // Debug once in a while (optional)
      // Serial.println(analogRead(SENSOR_PIN));

      if (newPress) {
        lastPressMs = now;
        showGreen();              // success feedback
        enter(FEEDBACK);
      }
      lastPressed = pressed;
    } break;

    case FEEDBACK: {
      if (now - stateStartMs >= FEEDBACK_MS) {
        // Restart the cycle: show the cue again
        showCue();
        enter(SHOW_CUE);
      }
    } break;
  }
}

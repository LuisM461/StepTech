#include <Arduino.h>
#include <FastLED.h>

// ----------------------------
// Hardware config (single tile)
// ----------------------------
#define LED_PIN         13
#define SENSOR_PIN      34          // ESP32 ADC1 pin recommended
#define LEDS_PER_TILE   256         // e.g., 16x16
#define LED_COUNT       LEDS_PER_TILE
#define BRIGHTNESS      25

// Logical matrix size (16x16 = 256)
#define MATRIX_W        16
#define MATRIX_H        16

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

// Map (row, col) → LED index (assuming simple row-major wiring)
inline uint16_t indexFromRC(uint8_t r, uint8_t c) {
  return r * MATRIX_W + c;  // 0 <= r < 16, 0 <= c < 16
}

inline bool readPressedRaw() {
  int v = analogRead(SENSOR_PIN);
#if PRESS_ACTIVE_LOW
  return v < THRESHOLD;
#else
  return v > THRESHOLD;
#endif
}

inline void clearTile() {
  FastLED.clear();
  FastLED.show();
}

// Fill a rectangular region of the matrix with a color
void fillRect(uint8_t r0, uint8_t r1, uint8_t c0, uint8_t c1, const CRGB &color) {
  for (uint8_t r = r0; r <= r1 && r < MATRIX_H; ++r) {
    for (uint8_t c = c0; c <= c1 && c < MATRIX_W; ++c) {
      uint16_t idx = indexFromRC(r, c);
      if (idx < LED_COUNT) {
        leds[idx] = color;
      }
    }
  }
}

// Show 9 zones (3x3) with different colors
void showZonesPattern() {
  // Clear first so edges are clean
  FastLED.clear();

  // We split 16 rows/cols into 3 bands:
  // rows: 0–4, 5–9, 10–15
  // cols: 0–4, 5–9, 10–15

  // Top row of zones
  fillRect(0, 3,  0, 0,  CRGB::Red);       // top-left
  //fillRect(0, 4,  5, 9,  CRGB::Green);     // top-middle
  //fillRect(0, 4, 10, 15, CRGB::Blue);      // top-right

  // Middle row of zones
  //fillRect(5, 9,  0, 4,  CRGB::Yellow);    // mid-left
  //fillRect(5, 9,  5, 9,  CRGB::Magenta);   // mid-middle
  //fillRect(5, 9, 10, 15, CRGB::Cyan);      // mid-right

  // Bottom row of zones
  //fillRect(10, 15,  0, 4,  CRGB::Orange);  // bottom-left
  //fillRect(10, 15,  5, 9,  CRGB::Purple);  // bottom-middle
  //fillRect(10, 15, 10, 15, CRGB::White);   // bottom-right

  FastLED.show();
}

// Keep solid blue as your "feedback" color
inline void showBlue() {
  fill_solid(leds, LED_COUNT, CRGB::Blue);
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

  pinMode(SENSOR_PIN, INPUT);

  // Startup: show 9-zone pattern, then go into CUE_FLASH_GREEN state
  showZonesPattern();
  enter(CUE_FLASH_GREEN);
}

void loop() {
  uint32_t now = millis();
  bool pressedNow = readPressedRaw();

  switch (state) {
    case CUE_FLASH_GREEN: {
      // Show zones for a brief cue, then go to WAIT_FOR_PRESS
      if (now - stateStartMs >= FLASH_GREEN_MS) {
        // Keep zones visible; just change state
        enter(WAIT_FOR_PRESS);
      }
    } break;

    case WAIT_FOR_PRESS: {
      // Idle: zones pattern visible
      if (pressEdge(now, pressedNow)) {
        // On initial press: keep zones visible for now
        // (Later you can change to highlight a specific zone)
        showZonesPattern();
        enter(PRESSED_HELD);
      }
    } break;

    case PRESSED_HELD: {
      // While pressed: still show zones (you can change this later if desired)
      if (releaseEdge(now, pressedNow)) {
        // On release: flash blue to confirm recorded press
        showBlue();
        enter(FEEDBACK_BLUE);
      }
    } break;

    case FEEDBACK_BLUE: {
      if (now - stateStartMs >= FEEDBACK_BLUE_MS) {
        // After blue feedback, go back to zone pattern and wait again
        showZonesPattern();
        enter(WAIT_FOR_PRESS);
      }
    } break;
  }
}
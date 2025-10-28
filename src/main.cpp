#include <Arduino.h>
#include <FastLED.h>
#include "../include/pins.hpp"

// LED MATRIX DEFINES --------------
//#define MATRIX_W        16
//#define MATRIX_H        16
//#define NUM_LEDS        (MATRIX_W * MATRIX_H)
//#define BRIGHTNESS      35          // keep low while testing (0..255)
//#define LED_TYPE        WS2812B
//#define COLOR_ORDER     GRB
//CRGB leds[NUM_LEDS];
//----------------------------------

static const unsigned long debouncePressMs   = 30;      // faster commit on press (falling)
static const unsigned long debounceReleaseMs = 80;      // slower commit on release (rising)
static constexpr int R = Pins::rows;
static constexpr int C = Pins::cols;

static int lastReading[R][C];
static int stableState[R][C];
static unsigned long lastChangeMs[R][C];


// Button debounce code for consistent button press
void initDebounce() {
  unsigned long now = millis();
  for (int r = 0; r < R; ++r) {
    for (int c = 0; c < C; ++c) {
      lastReading[r][c]  = HIGH;   // INPUT_PULLUP idle = HIGH
      stableState[r][c]  = HIGH;
      lastChangeMs[r][c] = now;
    }
  }
}


// static inline int xy(int r, int c) {
//   if (r < 0 || r >= MATRIX_H || c < 0 || c >= MATRIX_W) 
//     return 0;

//   if (r & 1) 
//     return r * MATRIX_W + (MATRIX_W - 1 - c);           // odd row

//   return r * MATRIX_W + c;                              // even row
// }

void setup() {
  Serial.begin(115200);
  Serial.println("Booting...");
  delay(50);

  for (int r = 0; r < Pins::rows; ++r) {
    for (int c = 0; c < Pins::cols; ++c) {
      pinMode(Pins::LEDS[r][c], OUTPUT);
      digitalWrite(Pins::LEDS[r][c], LOW);
    }
  }

  for (int r = 0; r < Pins::rows; ++r) {
    for (int c = 0; c < Pins::cols; ++c) {
      pinMode(Pins::BUTTONS[r][c], INPUT_PULLUP);       // active low
    }
  }

  initDebounce();
  // FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  // FastLED.setBrightness(BRIGHTNESS);
  // FastLED.clear(true);            // clears and shows leds

  // --- Show top-left 4 pixels by (row,col) using the map ---
  // FastLED.clear();
  // leds[xy(0,0)] = CRGB::Red;      // top-left
  // leds[xy(0,1)] = CRGB::Red;
  // leds[xy(1,0)] = CRGB::Red;
  // leds[xy(1,1)] = CRGB::Red;
  // FastLED.show();
  // Serial.println("Shown: 2x2 block at top-left via xy()");
  // delay(2500);

  // // --- First row chase so you can confirm serpentine direction ---
  // FastLED.clear(true);            // clears and shows leds
}

void loop() {
  unsigned long now = millis();

  for (int r = 0; r < Pins::rows; ++r) {
    for (int c = 0; c < Pins:: cols; ++c) {
      int reading = digitalRead(Pins::BUTTONS[r][c]);
      
      // --- Button debounce code ---
      if(reading != lastReading[r][c]) {
        lastReading[r][c]  = reading;
        lastChangeMs[r][c] = now;
      }
      
      if (reading != stableState[r][c]) {
        unsigned long needStable =
          (stableState[r][c] == HIGH && reading == LOW)
            ? debouncePressMs        // falling edge (press)
            : debounceReleaseMs;     // rising edge (release)

        if ((now - lastChangeMs[r][c]) >= needStable) {
          stableState[r][c] = reading;  // commit debounced state
        }
      }
      // ----------------------------

      bool pressed = (stableState[r][c] == LOW);
      digitalWrite(Pins::LEDS[r][c], pressed ? HIGH : LOW);
    }
  }
}
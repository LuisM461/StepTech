#include <Arduino.h>
#include <FastLED.h>

// PINS ----------------------------
#define BUTTON_PIN      4           // PIN 4 FOR BUTTON ESP32
#define LED_PIN         15          // GPIO feeding DIN
//----------------------------------

// LED MATRIX DEFINES --------------
#define MATRIX_W        16
#define MATRIX_H        16
#define NUM_LEDS        (MATRIX_W * MATRIX_H)
#define BRIGHTNESS      35          // keep low while testing (0..255)
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB
CRGB leds[NUM_LEDS];
//----------------------------------


static inline int xy(int r, int c) {
  if (r < 0 || r >= MATRIX_H || c < 0 || c >= MATRIX_W) 
    return 0;

  if (r & 1) 
    return r * MATRIX_W + (MATRIX_W - 1 - c);           // odd row

  return r * MATRIX_W + c;                              // even row
}

void setup() {
  Serial.begin(115200);
  delay(50);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.clear(true);            // clears and shows leds

  // --- Show top-left 4 pixels by (row,col) using the map ---
  FastLED.clear();
  leds[xy(0,0)] = CRGB::Red;      // top-left
  leds[xy(0,1)] = CRGB::Red;
  leds[xy(1,0)] = CRGB::Red;
  leds[xy(1,1)] = CRGB::Red;
  FastLED.show();
  Serial.println("Shown: 2x2 block at top-left via xy()");
  delay(2500);

  // --- First row chase so you can confirm serpentine direction ---
  FastLED.clear(true);            // clears and shows leds
}

void loop() {
  static int last_state   = HIGH;
  static int lastReading  = HIGH;
  static int stableState  = HIGH;
  static bool on_flag     = false;
  static unsigned long lastChangeMs     = 0;
  const unsigned long debouncePressMs   = 30;
  const unsigned long debounceReleaseMs = 80;
  
  unsigned long now     = millis();
  int reading           = digitalRead(BUTTON_PIN);
  

  if (reading != lastReading) {
    lastReading = reading;
    lastChangeMs = now;
  }

  if (reading != stableState) {
    unsigned long needStable = (stableState == HIGH && reading == LOW)
                               ? debouncePressMs      // falling (press)
                               : debounceReleaseMs;   // rising  (release)
    
    if ((now - lastChangeMs) >= needStable) {
      stableState = reading;

      if (stableState == LOW) {
        // PRESSED -> turn ON
        Serial.println("On");
        fill_solid(leds, NUM_LEDS, CRGB::Green);
        FastLED.show();
      } else {
        // RELEASED -> turn OFF
        Serial.println("Off");
        FastLED.clear(true); // clear + show immediately
      }
    }
  }
  // Simple cycle through RBG
  // static uint8_t hue = 0;  // 0–255 hue value
  // fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255)); // fill entire array with same hue
  // FastLED.show();
  // hue++;                    // increment hue (wraps around automatically at 255)
  // delay(5);                // adjust speed: lower = faster

  // Simple moving pixel across the whole matrix
  // for (int r = 0; r < MATRIX_H; ++r) {
  //   for (int c = 0; c < MATRIX_W; ++c) {
  //     FastLED.clear();
  //     leds[xy(c, r)] = CRGB(0, 80, 90);
  //     FastLED.show();
  //     delay(50);
  //   }
  // }
}
#include <FastLED.h>

// ---------- LED CONFIG ----------
#define DATA_PIN    13
#define BRIGHTNESS  25
#define NUM_TILES   9
#define TILE_SIZE   256
#define NUM_LEDS    (TILE_SIZE * NUM_TILES)

CRGB leds[NUM_LEDS];

// ---------- SENSOR CONFIG ----------
#define MUX_SIG 34
#define MUX_S0  14
#define MUX_S1  25
#define MUX_S2  26
#define MUX_S3  27

int sensorValues[NUM_TILES];
const bool PRESS_IS_LOWER = true;  // true = value drops when pressed

// ---------- INDIVIDUAL THRESHOLDS ----------
// Tune these per tile after checking Serial Monitor values.
// Example:  lower value = more sensitive; higher value = less sensitive.
int thresholds[NUM_TILES] = {
  25, // Tile 1
  25, // Tile 2
  25, // Tile 3
  25, // Tile 4
  100, // Tile 5
  100, // Tile 6
  100, // Tile 7
  100, // Tile 8
  100,  // Tile 9
};

// ---------- LED INDEX MAP (daisy-chain order) ----------
// Chain order: 9 → 4 → 3 → 2 → 5 → 8 → 7 → 6 → 1
const int tileStart[NUM_TILES] = {
  0,             // Tile 9
  1 * TILE_SIZE, // Tile 4
  2 * TILE_SIZE, // Tile 3
  3 * TILE_SIZE, // Tile 2
  4 * TILE_SIZE, // Tile 5
  5 * TILE_SIZE, // Tile 8
  6 * TILE_SIZE, // Tile 7
  7 * TILE_SIZE, // Tile 6
  8 * TILE_SIZE  // Tile 1
};

int tileEnd(int t) {
  if (t == NUM_TILES - 1) return NUM_LEDS;
  return tileStart[t + 1];
}

// ---------- SENSOR → TILE MAP ----------
const int sensorToTile[NUM_TILES] = {
  8, // C0 → Tile 1
  3, // C1 → Tile 2
  2, // C2 → Tile 3
  1, // C3 → Tile 4
  4, // C4 → Tile 5
  7, // C5 → Tile 6
  6, // C6 → Tile 7
  5, // C7 → Tile 8
  0  // C8 → Tile 9
};

// ---------- FIXED COLORS ----------
CRGB tileColors[NUM_TILES] = {
  CRGB(0, 120, 255),   // Tile 9 - Light Blue
  CRGB(255, 0, 0),     // Tile 4 - Red
  CRGB(0, 255, 0),     // Tile 3 - Green
  CRGB(255, 90, 0),    // Tile 2 - Orange
  CRGB(0, 0, 180),     // Tile 5 - Dark Blue
  CRGB(255, 90, 0),    // Tile 8 - Orange
  CRGB(0, 150, 0),     // Tile 7 - Dark Green
  CRGB(0, 255, 180),   // Tile 6 - Mint/Teal
  CRGB(180, 0, 255)    // Tile 1 - Light Purple
};

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);

  Serial.println("Sequential lighting in daisy-chain order...");
  delay(500);

  // Step 1: Light each tile sequentially in chain order (9→4→3→2→5→8→7→6→1)
  for (int t = 0; t < NUM_TILES; t++) {
    int start = tileStart[t];
    int end   = tileEnd(t);
    for (int i = start; i < end; i++) leds[i] = tileColors[t];
    FastLED.show();
    delay(400);
  }

  // Step 2: Turn off all
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  Serial.println("Ready for pressure input...");
}

// ---------- MUX SELECT ----------
void selectMuxChannel(uint8_t ch) {
  digitalWrite(MUX_S0, (ch & 0x01));
  digitalWrite(MUX_S1, (ch & 0x02));
  digitalWrite(MUX_S2, (ch & 0x04));
  digitalWrite(MUX_S3, (ch & 0x08));
  delayMicroseconds(200);
}

inline bool isPressed(int v, int threshold) {
  return PRESS_IS_LOWER ? (v < threshold) : (v > threshold);
}

// ---------- LOOP ----------
void loop() {
  // Read all sensors
  for (int ch = 0; ch < NUM_TILES; ch++) {
    selectMuxChannel(ch);
    delay(3);
    sensorValues[ch] = analogRead(MUX_SIG);
  }

  // Update LEDs
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int ch = 0; ch < NUM_TILES; ch++) {
    int tileIndex = sensorToTile[ch];
    int start = tileStart[tileIndex];
    int end   = tileEnd(tileIndex);

    if (isPressed(sensorValues[ch], thresholds[ch])) {
      CRGB color = tileColors[tileIndex];
      for (int i = start; i < end; i++) leds[i] = color;
    }
  }

  FastLED.show();

  // Debug print
  Serial.print("Sensors: ");
  for (int i = 0; i < NUM_TILES; i++) Serial.printf("%4d ", sensorValues[i]);
  Serial.println();

  delay(50);
}

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
// (Keep these — they are your per-channel calibrations: C0..C8)
int thresholds[NUM_TILES] = {
  300, // Tile 1 (C0)
  50,  // Tile 2 (C1)
  300, // Tile 3 (C2)
  300, // Tile 4 (C3)
  300, // Tile 5 (C4)
  100, // Tile 6 (C5)
  300, // Tile 7 (C6)
  300, // Tile 8 (C7)
  500, // Tile 9 (C8)
};

// ---------- LED INDEX MAP (daisy-chain order) ----------
// Chain order: 9 → 4 → 3 → 2 → 5 → 8 → 7 → 6 → 1
const int tileStart[NUM_TILES] = {
  0,             // internal 0 = Tile label 9
  1 * TILE_SIZE, // internal 1 = label 4
  2 * TILE_SIZE, // internal 2 = label 3
  3 * TILE_SIZE, // internal 3 = label 2
  4 * TILE_SIZE, // internal 4 = label 5
  5 * TILE_SIZE, // internal 5 = label 8
  6 * TILE_SIZE, // internal 6 = label 7
  7 * TILE_SIZE, // internal 7 = label 6
  8 * TILE_SIZE  // internal 8 = label 1
};
int tileEnd(int t) { return (t == NUM_TILES - 1) ? NUM_LEDS : tileStart[t + 1]; }

// ---------- SENSOR → TILE MAP (by mux channel C0..C8) ----------
const int sensorToTile[NUM_TILES] = {
  8, // C0 → label 1  (internal 8)
  3, // C1 → label 2  (internal 3)
  2, // C2 → label 3  (internal 2)
  1, // C3 → label 4  (internal 1)
  4, // C4 → label 5  (internal 4)
  7, // C5 → label 6  (internal 7)
  6, // C6 → label 7  (internal 6)
  5, // C7 → label 8  (internal 5)
  0  // C8 → label 9  (internal 0)
};

// ---------- FIXED COLORS (used for preview only) ----------
CRGB tileColors[NUM_TILES] = {
  CRGB(0, 120, 255),   // internal 0  = label 9 - Light Blue
  CRGB(255, 0, 0),     // internal 1  = label 4 - Red
  CRGB(0, 255, 0),     // internal 2  = label 3 - Green
  CRGB(255, 90, 0),    // internal 3  = label 2 - Orange
  CRGB(0, 0, 180),     // internal 4  = label 5 - Dark Blue
  CRGB(255, 90, 0),    // internal 5  = label 8 - Orange
  CRGB(0, 150, 0),     // internal 6  = label 7 - Dark Green
  CRGB(0, 255, 180),   // internal 7  = label 6 - Mint/Teal
  CRGB(180, 0, 255)    // internal 8  = label 1 - Light Purple
};

// =========================================================
// ===============  GAME: variable-size sets  ==============
// =========================================================

#define SHOW_MS 2500  // preview time before waiting for input

// Map labels (1..9) to internal indices (0..8) for LEDs
const int8_t labelToIndex[10] = {
  -1, // 0 unused
   8, // 1 → internal 8
   3, // 2 → internal 3
   2, // 3 → internal 2
   1, // 4 → internal 1
   4, // 5 → internal 4
   7, // 6 → internal 7
   6, // 7 → internal 6
   5, // 8 → internal 5
   0  // 9 → internal 0
};

// Define sets using labels (variable count per set)
const uint8_t SEQ_LABELS0[] = {3, 5, 1};       // 3 tiles
const uint8_t SEQ_LABELS1[] = {9, 4};          // 2 tiles
const uint8_t SEQ_LABELS2[] = {2, 5, 8, 7};    // 4 tiles
const uint8_t SEQ_LABELS3[] = {1, 2, 3};       // 3 tiles
const uint8_t SEQ_LABELS4[] = {9, 5, 8};       // 3 tiles

// Pointers + counts (so each set can be a different length)
const uint8_t* SEQ_PTRS[] = { SEQ_LABELS0, SEQ_LABELS1, SEQ_LABELS2, SEQ_LABELS3, SEQ_LABELS4 };
const uint8_t  SEQ_COUNT[] = { 3,            2,            4,            3,            3 };
const uint8_t  SEQ_LEN = sizeof(SEQ_PTRS) / sizeof(SEQ_PTRS[0]);

// ---------- State machine ----------
enum GameState { SHOW_SET, WAIT_INPUT, WRONG_HOLD };
GameState state = SHOW_SET;
uint32_t  stateStart = 0;
uint8_t   seqIndex   = 0;

uint16_t requiredMask = 0;   // bits for tiles required in current set
uint16_t progressMask = 0;   // bits for required tiles that have been stepped on at least once
int      wrongTile    = -1;  // track any wrong tile while held

bool pressedNow [NUM_TILES] = {0};
bool pressedPrev[NUM_TILES] = {0};

// ---------- Helpers ----------
inline void selectMuxChannel(uint8_t ch) {
  // Keep your original mux toggling (don’t alter calibration behavior)
  digitalWrite(MUX_S0, (ch & 0x01));
  digitalWrite(MUX_S1, (ch & 0x02));
  digitalWrite(MUX_S2, (ch & 0x04));
  digitalWrite(MUX_S3, (ch & 0x08));
  delayMicroseconds(200);
}
inline bool isPressed(int v, int threshold) {
  return PRESS_IS_LOWER ? (v < threshold) : (v > threshold);
}
inline void setTileColor(int t, const CRGB &c) {
  int s = tileStart[t], e = tileEnd(t);
  for (int i = s; i < e; i++) leds[i] = c;
}
inline void clearAll() { fill_solid(leds, NUM_LEDS, CRGB::Black); }

uint16_t labelsToMask(const uint8_t* labels, uint8_t count) {
  uint16_t m = 0;
  for (uint8_t i = 0; i < count; i++) {
    int idx = labelToIndex[labels[i]];
    if (idx >= 0) m |= (1u << idx);
  }
  return m;
}
void loadRequiredMask() {
  requiredMask = labelsToMask(SEQ_PTRS[seqIndex], SEQ_COUNT[seqIndex]);
  progressMask = 0;
}
void showCurrentSetPreview() {
  clearAll();
  // Light required tiles using their fixed per-tile colors
  for (uint8_t i = 0; i < SEQ_COUNT[seqIndex]; i++) {
    int idx = labelToIndex[SEQ_PTRS[seqIndex][i]];
    if (idx >= 0) setTileColor(idx, tileColors[idx]);
  }
  FastLED.show();
}
void startShowPhase() {
  loadRequiredMask();
  showCurrentSetPreview();
  stateStart = millis();
  state = SHOW_SET;
}
void advanceSequence() {
  seqIndex = (seqIndex + 1) % SEQ_LEN;
  startShowPhase();
}
void resetGame() {
  seqIndex = 0;
  wrongTile = -1;
  startShowPhase();
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  clearAll();
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
  clearAll();
  FastLED.show();
  Serial.println("Ready for memory game...");

  // initialize press tracking
  for (int i = 0; i < NUM_TILES; i++) pressedNow[i] = pressedPrev[i] = false;

  // begin game
  startShowPhase();
}

// ---------- LOOP ----------
void loop() {
  // -------- Read all sensors (keep your calibration scheme) --------
  for (int ch = 0; ch < NUM_TILES; ch++) {
    selectMuxChannel(ch);
    delay(3);
    sensorValues[ch] = analogRead(MUX_SIG);

    // Map to internal tile index & update press edges
    int t = sensorToTile[ch];
    pressedPrev[t] = pressedNow[t];
    pressedNow[t]  = isPressed(sensorValues[ch], thresholds[ch]);
  }

  // -------- Game state machine --------
  switch (state) {
    case SHOW_SET: {
      if (millis() - stateStart >= SHOW_MS) {
        clearAll();
        FastLED.show();
        state = WAIT_INPUT;
      }
    } break;

    case WAIT_INPUT: {
      bool completed = false;

      clearAll();
      // Visual feedback while waiting:
      // pressed required -> GREEN, pressed not-required -> RED, others OFF
      for (int t = 0; t < NUM_TILES; t++) {
        bool isReq = (requiredMask & (1u << t));
        if (pressedNow[t]) {
          setTileColor(t, isReq ? CRGB::Green : CRGB::Red);
        }
        // progress on rising edge for required tiles
        if (pressedNow[t] && !pressedPrev[t]) {
          if (isReq) {
            progressMask |= (1u << t);
            if ((progressMask & requiredMask) == requiredMask) completed = true;
          } else {
            wrongTile = t;
            state = WRONG_HOLD;
          }
        }
      }
      FastLED.show();

      if (completed) {
        delay(120);       // small grace against bounce
        advanceSequence();
      }
    } break;

    case WRONG_HOLD: {
      // Keep wrong presses RED and required presses GREEN while held.
      bool anyPressed = false;
      clearAll();
      for (int t = 0; t < NUM_TILES; t++) {
        if (pressedNow[t]) {
          anyPressed = true;
          bool isReq = (requiredMask & (1u << t));
          setTileColor(t, isReq ? CRGB::Green : CRGB::Red);
        }
      }
      FastLED.show();

      // Reset once all feet are lifted (prevents instant flicker resets)
      if (!anyPressed) {
        resetGame();
      }
    } break;
  }

  // -------- Debug print (unchanged format) --------
  Serial.print("Sensors: ");
  for (int i = 0; i < NUM_TILES; i++) Serial.printf("%4d ", sensorValues[i]);
  Serial.println();

  // Keep your small pacing delay
  delay(50);
}

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

int  sensorValues[NUM_TILES];  // raw ADC (for debug)
bool pressedNow [NUM_TILES];   // debounced + hysteresis presses
bool pressedPrev[NUM_TILES];

const bool PRESS_IS_LOWER = true;  // true = value drops when pressed

// ---------- SENSITIVITY / FILTER CONFIG ----------

// How big the drop from idle to count as "pressed" (since lower = pressed)
const float PRESS_FACTOR   = 0.70f;   // 70% of idle → press
const float RELEASE_FACTOR = 0.78f;   // 78% of idle → release (hysteresis)

// EMA smoothing factor (0..1). Smaller = smoother, less twitchy.
const float EMA_ALPHA = 0.25f;

// Debounce timings (ms)
const uint16_t MIN_PRESS_MS   = 60;   // must stay in "press" region this long
const uint16_t MIN_RELEASE_MS = 60;   // must stay in "release" region this long

// ---------- BASELINES + THRESHOLDS + FILTER STATE ----------
int baseline[NUM_TILES];     // idle values (no weight)
int thrPress[NUM_TILES];     // "go pressed" threshold
int thrRelease[NUM_TILES];   // "go released" threshold
int filtValue[NUM_TILES];    // smoothed ADC value

bool      stablePressed[NUM_TILES];  // internal stable state
uint32_t  lastEdgeMs[NUM_TILES];     // last time state change was considered

// ---------- LED INDEX MAP (daisy-chain order) ----------
// Chain order: 9 → 4 → 3 → 2 → 5 → 8 → 7 → 6 → 1
const int tileStart[NUM_TILES] = {
  0,             // index 0 = label 9
  1 * TILE_SIZE, // index 1 = label 4
  2 * TILE_SIZE, // index 2 = label 3
  3 * TILE_SIZE, // index 3 = label 2
  4 * TILE_SIZE, // index 4 = label 5
  5 * TILE_SIZE, // index 5 = label 8
  6 * TILE_SIZE, // index 6 = label 7
  7 * TILE_SIZE, // index 7 = label 6
  8 * TILE_SIZE  // index 8 = label 1
};
int tileEnd(int t) { return (t == NUM_TILES - 1) ? NUM_LEDS : tileStart[t + 1]; }

// ---------- SENSOR → TILE MAP (C0..C8 to internal tile index 0..8) ----------
const int sensorToTile[NUM_TILES] = {
  8, // C0 → Tile label 1 (internal 8)
  3, // C1 → label 2  (internal 3)
  2, // C2 → label 3  (internal 2)
  1, // C3 → label 4  (internal 1)
  4, // C4 → label 5  (internal 4)
  7, // C5 → label 6  (internal 7)
  6, // C6 → label 7  (internal 6)
  5, // C7 → label 8  (internal 5)
  0  // C8 → label 9  (internal 0)
};

// ---------- FIXED COLORS (only used for PREVIEW) ----------
CRGB tileColors[NUM_TILES] = {
  CRGB(0, 120, 255),   // internal 0  = label 9
  CRGB(255, 0, 0),     // internal 1  = label 4
  CRGB(0, 255, 0),     // internal 2  = label 3
  CRGB(255, 90, 0),    // internal 3  = label 2
  CRGB(0, 0, 180),     // internal 4  = label 5
  CRGB(255, 90, 0),    // internal 5  = label 8
  CRGB(0, 150, 0),     // internal 6  = label 7
  CRGB(0, 255, 180),   // internal 7  = label 6
  CRGB(180, 0, 255)    // internal 8  = label 1
};

// ---------- GAME CONFIG ----------
#define SHOW_MS 2500  // how long the 3 tiles preview stays lit before waiting for input

// Map from human labels (1..9) to internal indices (0..8)
const int8_t labelToIndex[10] = {
  -1, // 0 unused
   8, // label 1 → internal 8
   3, // label 2 → internal 3
   2, // label 3 → internal 2
   1, // label 4 → internal 1
   4, // label 5 → internal 4
   7, // label 6 → internal 7
   6, // label 7 → internal 6
   5, // label 8 → internal 5
   0  // label 9 → internal 0
};

// Define your fixed “random-looking” triplets here using labels (1..9).
const uint8_t SEQ_LABELS[][3] = {
  {3, 5, 1},   // first set
  {9, 4, 2},
  {8, 6, 7},
  {1, 2, 3},
  {9, 5, 8}
};
const uint8_t SEQ_LEN = sizeof(SEQ_LABELS) / sizeof(SEQ_LABELS[0]);

// ---------- STATE MACHINE ----------
enum GameState { SHOW_SET, WAIT_INPUT, WRONG_HOLD };
GameState state = SHOW_SET;
uint32_t stateStart = 0;
uint8_t  seqIndex   = 0;

uint16_t requiredMask = 0;   // which tiles are required (3 bits)
uint16_t progressMask = 0;   // which required tiles have been stepped at least once
int      wrongTile    = -1;  // track a wrong tile while held

// ---------- HELPERS ----------
inline void selectMuxChannel(uint8_t ch) {
  digitalWrite(MUX_S0, (ch >> 0) & 0x01);
  digitalWrite(MUX_S1, (ch >> 1) & 0x01);
  digitalWrite(MUX_S2, (ch >> 2) & 0x01);
  digitalWrite(MUX_S3, (ch >> 3) & 0x01);
  delayMicroseconds(200);
}

inline void setTileColor(int t, const CRGB &c) {
  int s = tileStart[t], e = tileEnd(t);
  for (int i = s; i < e; i++) leds[i] = c;
}

inline void clearAll() { fill_solid(leds, NUM_LEDS, CRGB::Black); }

uint16_t labelsToMask(const uint8_t triplet[3]) {
  uint16_t m = 0;
  for (int i = 0; i < 3; i++) {
    int idx = labelToIndex[triplet[i]];
    if (idx >= 0) m |= (1u << idx);
  }
  return m;
}

void loadRequiredMask() {
  requiredMask = labelsToMask(SEQ_LABELS[seqIndex]);
  progressMask = 0;
}

void showCurrentSetPreview() {
  clearAll();
  for (int t = 0; t < NUM_TILES; t++) {
    if (requiredMask & (1u << t)) {
      setTileColor(t, tileColors[t]); // preview with per-tile color
    }
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

// ---- Calibration: measure idle values and build thresholds ----
void calibrateThresholds() {
  Serial.println("=== Sensor calibration ===");
  Serial.println("Make sure NO ONE is standing on the tiles...");
  delay(2500);

  const int samples = 20;

  for (int ch = 0; ch < NUM_TILES; ch++) {
    selectMuxChannel(ch);
    delay(5); // settle MUX

    long sum = 0;
    // discard first read after mux switch
    analogRead(MUX_SIG);
    delay(2);

    for (int i = 0; i < samples; i++) {
      sum += analogRead(MUX_SIG);
      delay(2);
    }

    int idle = sum / samples;
    baseline[ch]  = idle;
    filtValue[ch] = idle;   // start EMA at idle

    if (PRESS_IS_LOWER) {
      thrPress[ch]   = (int)(idle * PRESS_FACTOR);     // press when BELOW
      thrRelease[ch] = (int)(idle * RELEASE_FACTOR);   // release when ABOVE
      if (thrPress[ch]   < 0)    thrPress[ch]   = 0;
      if (thrRelease[ch] < 0)    thrRelease[ch] = 0;
    } else {
      thrPress[ch]   = (int)(idle / PRESS_FACTOR);     // press when ABOVE
      thrRelease[ch] = (int)(idle / RELEASE_FACTOR);   // release when BELOW
      if (thrPress[ch]   > 4095) thrPress[ch]   = 4095;
      if (thrRelease[ch] > 4095) thrRelease[ch] = 4095;
    }

    stablePressed[ch] = false;
    lastEdgeMs[ch]    = millis();

    Serial.printf("Ch %d idle=%4d  thrP=%4d  thrR=%4d\n",
                  ch, idle, thrPress[ch], thrRelease[ch]);
  }

  Serial.println("Calibration done.");
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

  // init press arrays
  for (int i = 0; i < NUM_TILES; i++) {
    pressedNow[i] = pressedPrev[i] = false;
    stablePressed[i] = false;
    lastEdgeMs[i] = 0;
    baseline[i] = 0;
    thrPress[i] = thrRelease[i] = 0;
    filtValue[i] = 0;
  }

  // ----- CALIBRATE SENSORS -----
  calibrateThresholds();

  Serial.println("Sequential lighting in daisy-chain order...");
  delay(500);

  // Step 1: Light each tile sequentially in chain order (9→4→3→2→5→8→7→6→1)
  for (int t = 0; t < NUM_TILES; t++) {
    int s = tileStart[t], e = tileEnd(t);
    for (int i = s; i < e; i++) leds[i] = tileColors[t];
    FastLED.show();
    delay(400);
  }

  // Step 2: Turn off all
  clearAll();
  FastLED.show();
  Serial.println("Ready for memory game...");

  // begin game
  startShowPhase();
}

// ---------- LOOP ----------
void loop() {
  // -------- read sensors & update pressed states (EMA + hysteresis + debounce) --------
  for (int ch = 0; ch < NUM_TILES; ch++) {
    selectMuxChannel(ch);
    delayMicroseconds(150);
    int raw = analogRead(MUX_SIG);
    sensorValues[ch] = raw;

    // EMA smoothing
    filtValue[ch] = (int)(filtValue[ch] + EMA_ALPHA * (raw - filtValue[ch]));

    int t = sensorToTile[ch];  // map mux channel to tile index
    bool wantPressed;

    if (PRESS_IS_LOWER) {
      if (!stablePressed[t]) {
        // currently "not pressed" → only go pressed if below thrPress
        wantPressed = (filtValue[ch] < thrPress[ch]);
      } else {
        // currently pressed → stay pressed until clearly above thrRelease
        wantPressed = (filtValue[ch] < thrRelease[ch]);
      }
    } else {
      if (!stablePressed[t]) {
        wantPressed = (filtValue[ch] > thrPress[ch]);
      } else {
        wantPressed = (filtValue[ch] > thrRelease[ch]);
      }
    }

    uint32_t now = millis();
    bool aboutToFlip = (wantPressed != stablePressed[t]);

    if (aboutToFlip) {
      uint16_t needMs = wantPressed ? MIN_PRESS_MS : MIN_RELEASE_MS;
      if (now - lastEdgeMs[t] >= needMs) {
        // Commit the flip
        stablePressed[t] = wantPressed;
        lastEdgeMs[t]    = now;
      }
    } else {
      // If nothing is trying to change, keep timer aligned
      lastEdgeMs[t] = now;
    }

    // Expose stable state to the game logic
    pressedPrev[t] = pressedNow[t];
    pressedNow[t]  = stablePressed[t];
  }

  // -------- state machine --------
  switch (state) {
    case SHOW_SET: {
      // keep preview on for SHOW_MS, then go dark and wait for input
      if (millis() - stateStart >= SHOW_MS) {
        clearAll();
        FastLED.show();
        state = WAIT_INPUT;
      }
    } break;

    case WAIT_INPUT: {
      bool completed = false;

      // draw: pressed required -> green; pressed not-required -> red; else off
      clearAll();
      for (int t = 0; t < NUM_TILES; t++) {
        bool isReq = (requiredMask & (1u << t));
        if (pressedNow[t]) {
          setTileColor(t, isReq ? CRGB::Green : CRGB::Red);
        }

        // rising edge logic (use debounced presses)
        if (pressedNow[t] && !pressedPrev[t]) {
          if (isReq) {
            progressMask |= (1u << t);
            if ((progressMask & requiredMask) == requiredMask) {
              completed = true;
            }
          } else {
            wrongTile = t;
            state = WRONG_HOLD;
          }
        }
      }
      FastLED.show();

      if (completed) {
        // tiny grace to avoid immediate bounce
        delay(120);
        advanceSequence();
      }
    } break;

    case WRONG_HOLD: {
      // while any wrong or other tiles are pressed: required pressed = green, others = red
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

      // when all feet lift, hard reset to very first set
      if (!anyPressed) {
        resetGame();
      }
    } break;
  }

  // -------- optional debug --------
  /*
  static uint32_t last = 0;
  if (millis() - last > 300) {
    last = millis();
    Serial.print("Raw: ");
    for (int i = 0; i < NUM_TILES; i++) Serial.printf("%4d ", sensorValues[i]);
    Serial.print(" | Filt: ");
    for (int i = 0; i < NUM_TILES; i++) Serial.printf("%4d ", filtValue[i]);
    Serial.print(" | Pressed: ");
    for (int t = 0; t < NUM_TILES; t++) Serial.print(pressedNow[t] ? '1' : '0');
    Serial.println();
    last = millis();
  }
  */
}

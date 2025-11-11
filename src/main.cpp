#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>

// =========================================================
// ===============  LED / SENSOR CONFIG  ===================
// =========================================================

#define DATA_PIN    13
#define BRIGHTNESS  25
#define NUM_TILES   9
#define TILE_SIZE   256
#define NUM_LEDS    (TILE_SIZE * NUM_TILES)

CRGB leds[NUM_LEDS];

#define MUX_SIG 34
#define MUX_S0  14
#define MUX_S1  25
#define MUX_S2  26
#define MUX_S3  27

int  sensorValues[NUM_TILES];
const bool PRESS_IS_LOWER = true;

// Per-channel thresholds (C0..C8)
int thresholds[NUM_TILES] = {
  300, // Tile 1 (C0)
  50,  // Tile 2 (C1)
  300, // Tile 3 (C2)
  300, // Tile 4 (C3)
  300, // Tile 5 (C4)
  100, // Tile 6 (C5)
  300, // Tile 7 (C6)
  300, // Tile 8 (C7)
  500  // Tile 9 (C8)
};

// LED index map (daisy-chain order)
const int tileStart[NUM_TILES] = {
  0,
  1 * TILE_SIZE,
  2 * TILE_SIZE,
  3 * TILE_SIZE,
  4 * TILE_SIZE,
  5 * TILE_SIZE,
  6 * TILE_SIZE,
  7 * TILE_SIZE,
  8 * TILE_SIZE
};

int tileEnd(int t) {
  return (t == NUM_TILES - 1) ? NUM_LEDS : tileStart[t + 1];
}

// Sensor → tile map (by mux channel C0..C8)
const int sensorToTile[NUM_TILES] = {
  8,
  3,
  2,
  1,
  4,
  7,
  6,
  5,
  0
};

// Fixed colors (for preview)
CRGB tileColors[NUM_TILES] = {
  CRGB(0, 120, 255),
  CRGB(255, 0, 0),
  CRGB(0, 255, 0),
  CRGB(255, 90, 0),
  CRGB(0, 0, 180),
  CRGB(255, 90, 0),
  CRGB(0, 150, 0),
  CRGB(0, 255, 180),
  CRGB(180, 0, 255)
};

// =========================================================
// ===============  GAME: RANDOM TILE SETS  ================
// =========================================================

#define SHOW_MS 2500  // preview time before waiting for input

// Map labels (1..9) to internal indices (0..8)
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

// Current random set (labels 1..9)
uint8_t currentLabels[NUM_TILES];
uint8_t currentCount = 0;

// 👉 tweak these to control difficulty (min/max tiles per set)
const uint8_t MIN_TILES_IN_SET = 2;
const uint8_t MAX_TILES_IN_SET = 4;

enum GameState { SHOW_SET, WAIT_INPUT };
GameState state = SHOW_SET;
uint32_t  stateStart = 0;

uint16_t requiredMask = 0;
uint16_t progressMask = 0;
int      wrongTile    = -1;

bool pressedNow [NUM_TILES] = {0};
bool pressedPrev[NUM_TILES] = {0};

// Forward declarations
void clearAll();
void resetGame();
void game1Step();
void fadeRedGroup(const int *tiles, int count, uint16_t durationMs);
void successAnimation();
void wrongAnimation();

// =========================================================
// ===================== GAME HELPERS ======================
// =========================================================

inline void selectMuxChannel(uint8_t ch) {
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

inline void clearAll() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

uint16_t labelsToMask(const uint8_t* labels, uint8_t count) {
  uint16_t m = 0;
  for (uint8_t i = 0; i < count; i++) {
    int idx = labelToIndex[labels[i]];
    if (idx >= 0) m |= (1u << idx);
  }
  return m;
}

// Generate a new random set of labels (1..9), all unique in that set
void generateRandomSet() {
  bool used[10];
  for (int i = 0; i < 10; i++) used[i] = false;

  currentCount = random(MIN_TILES_IN_SET, MAX_TILES_IN_SET + 1); // [min,max]

  for (uint8_t i = 0; i < currentCount; i++) {
    uint8_t label;
    do {
      label = random(1, 10); // labels 1..9
    } while (used[label]);
    used[label] = true;
    currentLabels[i] = label;
  }
}

void loadRequiredMask() {
  requiredMask = labelsToMask(currentLabels, currentCount);
  progressMask = 0;
}

void showCurrentSetPreview() {
  clearAll();
  for (uint8_t i = 0; i < currentCount; i++) {
    int idx = labelToIndex[currentLabels[i]];
    if (idx >= 0) setTileColor(idx, tileColors[idx]);
  }
  FastLED.show();
}

void startShowPhase() {
  generateRandomSet();
  loadRequiredMask();
  showCurrentSetPreview();
  stateStart = millis();
  state = SHOW_SET;
}

void resetGame() {
  wrongTile = -1;
  startShowPhase();
}

// One “tick” of the game logic
void game1Step() {
  // Read all sensors
  for (int ch = 0; ch < NUM_TILES; ch++) {
    selectMuxChannel(ch);
    delay(3);
    sensorValues[ch] = analogRead(MUX_SIG);

    int t = sensorToTile[ch];
    pressedPrev[t] = pressedNow[t];
    pressedNow[t]  = isPressed(sensorValues[ch], thresholds[ch]);
  }

  // Game state machine
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
      for (int t = 0; t < NUM_TILES; t++) {
        bool isReq = (requiredMask & (1u << t));

        if (pressedNow[t]) {
          setTileColor(t, isReq ? CRGB::Green : CRGB::Red);
        }

        // detect new press
        if (pressedNow[t] && !pressedPrev[t]) {
          if (isReq) {
            progressMask |= (1u << t);
            if ((progressMask & requiredMask) == requiredMask) {
              completed = true;
            }
          } else {
            // WRONG TILE: red animation, then new random set
            wrongTile = t;
            wrongAnimation();
            resetGame();
            FastLED.show();
            return;  // exit this frame
          }
        }
      }
      FastLED.show();

      if (completed) {
        // Correct set: green celebration, then a new random pattern
        successAnimation();
        startShowPhase();
      }
    } break;
  }

  // Debug
  // Serial.print("Sensors: ");
  // for (int i = 0; i < NUM_TILES; i++) {
  //   Serial.printf("%4d ", sensorValues[i]);
  // }
  // Serial.println();

  delay(50);
}

// ---------- Fade helpers ----------

// Fast startup fade-in per tile
void fadeInTile(int t, const CRGB &baseColor, uint16_t durationMs) {
  const int steps = 5;  // fewer steps = faster
  int start = tileStart[t];
  int end   = tileEnd(t);

  for (int s = 0; s <= steps; s++) {
    uint8_t scale = (255 * s) / steps;  // 0 → 255
    CRGB c = baseColor;
    c.nscale8_video(scale);

    for (int i = start; i < end; i++) {
      leds[i] = c;
    }

    FastLED.show();
    delay(durationMs / steps);
  }
}

// Fast red fade in/out for a group of tiles (used on GAME1_STOP)
void fadeRedGroup(const int *tiles, int count, uint16_t durationMs) {
  const int steps = 5;
  // Fade in
  for (int s = 0; s <= steps; s++) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Red;
    c.nscale8_video(scale);

    for (int idx = 0; idx < count; idx++) {
      int t = tiles[idx];
      int start = tileStart[t];
      int end   = tileEnd(t);
      for (int i = start; i < end; i++) {
        leds[i] = c;
      }
    }
    FastLED.show();
    delay(durationMs / steps);
  }
  // Fade out
  for (int s = steps; s >= 0; s--) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Red;
    c.nscale8_video(scale);

    for (int idx = 0; idx < count; idx++) {
      int t = tiles[idx];
      int start = tileStart[t];
      int end   = tileEnd(t);
      for (int i = start; i < end; i++) {
        leds[i] = c;
      }
    }
    FastLED.show();
    delay(durationMs / steps);
  }
}

// Green "correct" animation on ALL tiles
void successAnimation() {
  const int steps = 5;
  const uint16_t durationMs = 120;  // lower = faster

  // Fade in green
  for (int s = 0; s <= steps; s++) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Green;
    c.nscale8_video(scale);

    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(durationMs / steps);
  }
  // Fade out green
  for (int s = steps; s >= 0; s--) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Green;
    c.nscale8_video(scale);

    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(durationMs / steps);
  }

  clearAll();
  FastLED.show();
}

// Red "wrong" animation on ALL tiles
void wrongAnimation() {
  const int steps = 5;
  const uint16_t durationMs = 120;

  // Fade in red
  for (int s = 0; s <= steps; s++) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Red;
    c.nscale8_video(scale);

    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(durationMs / steps);
  }
  // Fade out red
  for (int s = steps; s >= 0; s--) {
    uint8_t scale = (255 * s) / steps;
    CRGB c = CRGB::Red;
    c.nscale8_video(scale);

    fill_solid(leds, NUM_LEDS, c);
    FastLED.show();
    delay(durationMs / steps);
  }

  clearAll();
  FastLED.show();
}

// =========================================================
// ===============  WIFI / WEB SERVER  =====================
// =========================================================

const char* AP_SSID     = "StepTech-AP";
const char* AP_PASSWORD = "steptech123";

WebServer server(80);

// Flags controlled by the app
bool systemOn     = false;
bool game1Running = false;

// HTML UI (same as before)
const char INDEX_HTML[] PROGMEM = R"====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <title>StepTech Control</title>
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <style>
    *{box-sizing:border-box;margin:0;padding:0;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}
    body{background:#111827;color:#e5e7eb;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px}
    .app{width:100%;max-width:400px;background:#020617;border-radius:20px;padding:20px 18px 24px;box-shadow:0 15px 40px rgba(0,0,0,.5);border:1px solid #1f2933}
    .header{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:16px}
    .title{font-size:1.25rem;font-weight:700}
    .subtitle{font-size:.8rem;color:#9ca3af}
    .status-bar{margin-bottom:14px;padding:10px 12px;border-radius:12px;background:#030712;border:1px solid #111827;display:flex;justify-content:space-between;align-items:center;font-size:.85rem}
    .status-dot{width:10px;height:10px;border-radius:999px;margin-right:6px}
    .status-ok{background:#22c55e}
    .status-warn{background:#facc15}
    .status-off{background:#6b7280}
    .status-label{display:flex;align-items:center}
    .screen{display:none;margin-top:4px}
    .screen.active{display:block}
    .btn-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:10px}
    .btn{padding:10px 12px;border-radius:12px;border:none;font-size:.95rem;font-weight:600;cursor:pointer;transition:transform .08s ease,box-shadow .08s ease,background .1s ease;width:100%}
    .btn:active{transform:scale(.97);box-shadow:none}
    .btn-primary{background:#2563eb;color:#fff;box-shadow:0 8px 18px rgba(37,99,235,.35)}
    .btn-primary:hover{background:#1d4ed8}
    .btn-danger{background:#b91c1c;color:#fff;box-shadow:0 8px 18px rgba(185,28,28,.35)}
    .btn-danger:hover{background:#991b1b}
    .btn-secondary{background:#111827;color:#e5e7eb;border:1px solid #1f2937}
    .btn-ghost{background:transparent;color:#9ca3af;border:1px dashed #4b5563}
    .section-title{font-size:.9rem;font-weight:600;color:#9ca3af;margin-top:6px;margin-bottom:4px}
    .log{margin-top:12px;padding:10px;border-radius:10px;background:#020617;border:1px solid #111827;font-size:.8rem;max-height:120px;overflow-y:auto;color:#9ca3af;white-space:pre-wrap}
    .footer{margin-top:8px;text-align:center;font-size:.75rem;color:#4b5563}
    .tag{display:inline-flex;align-items:center;font-size:.7rem;padding:3px 8px;border-radius:999px;background:#111827;border:1px solid #1f2937;gap:4px}
    .tag-dot{width:6px;height:6px;border-radius:999px;background:#22c55e}
  </style>
</head>
<body>
  <div class="app">
    <div class="header">
      <div>
        <div class="title">StepTech Control</div>
        <div class="subtitle">Tile board controller</div>
      </div>
      <div class="tag">
        <span class="tag-dot"></span>
        <span>ESP32</span>
      </div>
    </div>

    <div class="status-bar">
      <div class="status-label">
        <div id="status-dot" class="status-dot status-off"></div>
        <span id="status-text">System stopped</span>
      </div>
      <span id="connection-text">Not connected</span>
    </div>

    <div id="screen-menu" class="screen active">
      <div class="section-title">System Control</div>
      <div class="btn-grid">
        <button class="btn btn-primary" onclick="systemStart()">▶ Start System</button>
        <button class="btn btn-danger" onclick="systemStop()">⏹ Stop System</button>
      </div>

      <div class="section-title" style="margin-top: 12px;">Games</div>
      <div class="btn-grid">
        <button class="btn btn-secondary" onclick="showScreen('game1')">🎮 Game 1</button>
        <button class="btn btn-ghost" onclick="alert('Game 2 not implemented yet');">🎮 Game 2</button>
      </div>

      <div class="section-title" style="margin-top: 12px;">Other</div>
      <div class="btn-grid">
        <button class="btn btn-ghost" onclick="alert('Settings coming soon');">⚙ Settings</button>
        <button class="btn btn-ghost" onclick="clearLog()">🧹 Clear Log</button>
      </div>
    </div>

    <div id="screen-game1" class="screen">
      <div class="section-title">Game 1 Controls</div>
      <div class="btn-grid">
        <button class="btn btn-primary" onclick="sendCommand('GAME1_START')">▶ Start Game 1</button>
        <button class="btn btn-danger" onclick="sendCommand('GAME1_STOP')">⏹ Stop Game 1</button>
      </div>
      <div class="btn-grid" style="margin-top: 12px;">
        <button class="btn btn-secondary" onclick="sendCommand('GAME1_RESET')">🔄 Reset Game 1</button>
        <button class="btn btn-ghost" onclick="showScreen('menu')">← Back to Menu</button>
      </div>
    </div>

    <div class="section-title">Event Log</div>
    <div id="log" class="log"></div>
    <div class="footer">v1.0 – menus, start / stop, Game 1</div>
  </div>

  <script>
    const API_BASE = "";

    function showScreen(name){
      const screens=document.querySelectorAll('.screen');
      screens.forEach(s=>s.classList.remove('active'));
      const screen=document.getElementById(name==='menu'?'screen-menu':`screen-${name}`);
      if(screen){screen.classList.add('active');log(`Switched to screen: ${name}`);}
    }
    function setStatus(mode){
      const dot=document.getElementById('status-dot');
      const text=document.getElementById('status-text');
      dot.classList.remove('status-ok','status-warn','status-off');
      if(mode==='running'){dot.classList.add('status-ok');text.textContent='System running';}
      else if(mode==='starting'){dot.classList.add('status-warn');text.textContent='Starting...';}
      else{dot.classList.add('status-off');text.textContent='System stopped';}
    }
    function setConnection(text){document.getElementById('connection-text').textContent=text;}
    function log(msg){
      const logEl=document.getElementById('log');
      const ts=new Date().toLocaleTimeString();
      logEl.textContent=`[${ts}] ${msg}\n`+logEl.textContent;
    }
    function clearLog(){document.getElementById('log').textContent="";}

    async function sendCommand(cmd){
      log(`Sending command: ${cmd}`);
      const url=API_BASE+"/cmd?c="+encodeURIComponent(cmd);
      try{
        const res=await fetch(url,{method:"GET"});
        if(!res.ok)throw new Error("HTTP "+res.status);
        const text=await res.text();
        setConnection("OK");
        log(`Response: ${text}`);
      }catch(err){
        setConnection("Error");
        log(`ERROR sending command: ${err.message}`);
      }
    }

    function systemStart(){
      setStatus('starting');
      sendCommand('SYSTEM_START').then(()=>{setStatus('running');});
    }
    function systemStop(){
      setStatus('stopped');
      sendCommand('SYSTEM_STOP');
    }
    window.addEventListener('load',()=>{log("UI loaded. Ready.");});
  </script>
</body>
</html>
)====";

// HTTP handlers
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCmd() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "Missing c param");
    return;
  }

  String cmd = server.arg("c");
  Serial.print("Got command: ");
  Serial.println(cmd);

  if (cmd == "SYSTEM_START") {
    systemOn = true;
  } else if (cmd == "SYSTEM_STOP") {
    systemOn = false;
    game1Running = false;
    clearAll();
    FastLED.show();
  } else if (cmd == "GAME1_START") {
    if (!systemOn) systemOn = true;
    game1Running = true;
    resetGame();
  } else if (cmd == "GAME1_STOP") {
    // fast red 3-at-a-time fade
    game1Running = false;

    const int group0[3] = {0, 1, 2};
    const int group1[3] = {3, 4, 5};
    const int group2[3] = {6, 7, 8};

    fadeRedGroup(group0, 3, 40);
    fadeRedGroup(group1, 3, 40);
    fadeRedGroup(group2, 3, 40);

    clearAll();
    FastLED.show();
  } else if (cmd == "GAME1_RESET") {
    if (systemOn) {
      resetGame();
      game1Running = true;
    }
  }

  server.send(200, "text/plain", "OK: " + cmd);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// =========================================================
// ====================== SETUP / LOOP =====================
// =========================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  // Seed RNG using floating analog pin (through mux signal)
  randomSeed(analogRead(MUX_SIG));

  // LEDs
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  clearAll();
  FastLED.show();

  // Mux pins
  pinMode(MUX_S0, OUTPUT);
  pinMode(MUX_S1, OUTPUT);
  pinMode(MUX_S2, OUTPUT);
  pinMode(MUX_S3, OUTPUT);

  // Fast startup fade animation
  Serial.println("Sequential fast fade-in in daisy-chain order...");
  delay(200);

  clearAll();
  FastLED.show();

  for (int t = 0; t < NUM_TILES; t++) {
    fadeInTile(t, tileColors[t], 30); // 30ms per tile
  }

  delay(100);
  clearAll();
  FastLED.show();
  Serial.println("Ready. Waiting for app commands...");

  // Initialize press tracking
  for (int i = 0; i < NUM_TILES; i++) {
    pressedNow[i] = pressedPrev[i] = false;
  }

  // WiFi AP
  Serial.println("Starting StepTech AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP SSID: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASSWORD);
  Serial.print("Open: http://");
  Serial.println(ip);

  // HTTP server
  server.on("/", handleRoot);
  server.on("/cmd", handleCmd);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();

  if (systemOn && game1Running) {
    game1Step();
  } else {
    delay(10);
  }
}

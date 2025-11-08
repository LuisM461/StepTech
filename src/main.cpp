#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ---------- Wi-Fi config ----------
const char* AP_SSID     = "ESP32_TILE";
const char* AP_PASSWORD = "steptech123";   // change this if you want

WebServer server(80);

// A simple "state" variable we'll read/change from the phone
int tileMode = 0;  // 0 = idle, 1 = game, etc.

// ---------- HTML page ----------
String htmlPage() {
  String page =
"<!DOCTYPE html>\n"
"<html>\n"
"  <head>\n"
"    <meta charset=\"UTF-8\">\n"
"    <title>ESP32 Tile Control</title>\n"
"    <style>\n"
"      body { font-family: sans-serif; background:#111; color:#eee; text-align:center; }\n"
"      h1 { margin-top: 30px; }\n"
"      button {\n"
"        padding: 12px 20px;\n"
"        margin: 10px;\n"
"        font-size: 16px;\n"
"        border-radius: 8px;\n"
"        border: none;\n"
"        cursor: pointer;\n"
"      }\n"
"      .idle { background:#444; }\n"
"      .game { background:#2e8b57; }\n"
"    </style>\n"
"  </head>\n"
"  <body>\n"
"    <h1>ESP32 Tile Control</h1>\n"
"    <p>Current mode: <span id=\"mode\">?</span></p>\n"
"    <button class=\"idle\" onclick=\"setMode(0)\">Idle</button>\n"
"    <button class=\"game\" onclick=\"setMode(1)\">Game</button>\n"
"\n"
"    <script>\n"
"      function setMode(m) {\n"
"        fetch(\"/setMode?value=\" + m)\n"
"          .then(r => r.text())\n"
"          .then(t => {\n"
"            console.log(\"Response:\", t);\n"
"            updateMode();\n"
"          });\n"
"      }\n"
"\n"
"      function updateMode() {\n"
"        fetch(\"/getMode\")\n"
"          .then(r => r.text())\n"
"          .then(t => {\n"
"            document.getElementById(\"mode\").innerText = t;\n"
"          });\n"
"      }\n"
"\n"
"      // Load on page open\n"
"      updateMode();\n"
"      // Auto-refresh every 2 seconds\n"
"      setInterval(updateMode, 2000);\n"
"    </script>\n"
"  </body>\n"
"</html>\n";

  return page;
}

// ---------- HTTP handlers ----------
void handleRoot() {
  server.send(200, "text/html", htmlPage());
}

void handleGetMode() {
  String s = String(tileMode);
  server.send(200, "text/plain", s);
}

void handleSetMode() {
  if (server.hasArg("value")) {
    tileMode = server.arg("value").toInt();
    Serial.print("Mode set from phone: ");
    Serial.println(tileMode);
    // TODO: here you can trigger LED behavior based on tileMode
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing 'value' parameter");
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  delay(200);

  // Start Wi-Fi Access Point
  Serial.println("Starting Wi-Fi AP...");
  WiFi.mode(WIFI_AP);
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD);
  if (!apStarted) {
    Serial.println("Failed to start AP!");
  } else {
    Serial.print("AP started. SSID: ");
    Serial.println(AP_SSID);

    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);   // typically 192.168.4.1
  }

  // Setup routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/getMode", HTTP_GET, handleGetMode);
  server.on("/setMode", HTTP_GET, handleSetMode);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Handle incoming HTTP requests
  server.handleClient();

  // Here you would use tileMode to control LEDs, etc.
  // Example:
  // if (tileMode == 0) showIdlePattern();
  // else if (tileMode == 1) showGamePattern();
}

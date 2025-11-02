#include <Adafruit_NeoPixel.h>

#define LED_PIN     13
#define LED_COUNT   256
#define SENSOR_PIN  34   // analog pin
#define THRESHOLD   0 // adjust based on your readings

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  strip.begin();
  strip.setBrightness(25);
  strip.show();
}

void loop() {
  int sensorValue = analogRead(SENSOR_PIN);
  Serial.println(sensorValue);

  if (sensorValue > THRESHOLD) {
    // not pressed: tile off
    strip.clear();
  } else {
    //pressed: turn on
     strip.fill(strip.Color(148, 0, 211));  // purple
  }
  strip.show();
  delay(50);
}
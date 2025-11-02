#include <Arduino.h>
#include <FastLED.h>

#define LED_PIN     13
#define LED_COUNT   256
#define SENSOR_PIN  34   // analog pin
#define THRESHOLD   0 // adjust based on your readings

CRGB leds[LED_COUNT];

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, LED_COUNT);
  FastLED.setBrightness(25);
  FastLED.clear(true);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  int sensorValue = analogRead(SENSOR_PIN);
  Serial.println(sensorValue);

  if (sensorValue > THRESHOLD) {
    // not pressed: tile off
    FastLED.clear();
  } 
  else {
    //pressed: turn on
    fill_solid(leds, LED_COUNT, CRGB(148, 0, 211)); // or CRGB::Purple
  }
  FastLED.show();
  delay(50);
}
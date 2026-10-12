// Lloyd AC Wi-Fi bridge for ESP32.
//
// The ESP32 sits in the room with an IR LED pointed at the AC and acts like the
// physical remote. Control it from the phone web app (http://lloyd-ac.local),
// the Apple Home app / Siri (HomeKit), or plain HTTP calls (iOS Shortcuts).
#include <Arduino.h>

#include "ac.h"
#include "homekit.h"
#include "web.h"

// Wiring (see README): IR LED driver on GPIO4, IR receiver (VS1838B/TSOP38238)
// data pin on GPIO14.
#ifndef IR_SEND_PIN
#define IR_SEND_PIN 4
#endif
#ifndef IR_RECV_PIN
#define IR_RECV_PIN 14
#endif

void setup() {
  Serial.begin(115200);
  Ac::begin(IR_SEND_PIN, IR_RECV_PIN);
  HomeKit::begin(Web::begin);
}

void loop() {
  HomeKit::loop();
  Web::loop();
  Ac::loop();
}

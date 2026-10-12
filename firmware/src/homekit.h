#pragma once

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "1.0.0"
#endif

namespace HomeKit {
// Starts HomeSpan, which also owns Wi-Fi (setup hotspot, reconnects).
// onWifi runs once the first time the network is up.
void begin(void (*onWifi)());
void loop();
}  // namespace HomeKit

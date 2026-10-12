// AC state model + IR transport (send, detect, learn).
#pragma once

#include <Arduino.h>
#include <IRremoteESP8266.h>

#include <vector>

// Fan levels as shown in the UI. Mapped to stdAc::fanspeed_t when sending.
enum FanLevel : uint8_t { FAN_AUTO = 0, FAN_LOW = 1, FAN_MED = 2, FAN_HIGH = 3 };

// Modes as shown in the UI. Values match stdAc::opmode_t.
enum AcMode : uint8_t { MODE_AUTO = 0, MODE_COOL = 1, MODE_DRY = 3, MODE_FAN = 4 };

static const uint8_t kMinTemp = 16;
static const uint8_t kMaxTemp = 30;

struct AcSettings {
  bool power = false;
  uint8_t mode = MODE_COOL;
  uint8_t temp = 24;
  uint8_t fan = FAN_AUTO;
  bool swingV = false;
  bool swingH = false;
  bool turbo = false;
  bool eco = false;
  bool sleep = false;
  bool quiet = false;
  bool display = true;
};

struct LearnedButton {
  uint8_t id;
  String name;
};

struct Detection {
  decode_type_t protocol = decode_type_t::UNKNOWN;
  int16_t model = -1;
  bool acSupported = false;   // IRac can drive this protocol
  uint32_t atMs = 0;          // millis() when seen, 0 = never
  String summary;             // short human readable description
};

namespace Ac {

void begin(uint8_t sendPin, uint8_t recvPin);
void loop();

// Current desired settings. Mutate via apply().
const AcSettings &settings();
// Sends the given settings to the AC and stores them. Returns false if no
// protocol is configured (nothing could be sent).
bool apply(const AcSettings &next);

// Bumped whenever settings change (from web, HomeKit, physical remote, timer).
uint32_t version();

// Protocol configuration.
decode_type_t protocol();
int16_t model();
bool setProtocol(decode_type_t p, int16_t model);
// Fire a test frame (power on, cool, 24C, auto fan) with a candidate protocol.
bool testProtocol(decode_type_t p, int16_t model, bool power);

const Detection &lastDetection();
// Full dump of the last captured IR frame (for diagnostics / adding support).
String lastCaptureDump();

// Countdown timers (seconds remaining, 0 = not set).
void setOffTimer(uint32_t minutes);
void setOnTimer(uint32_t minutes);
uint32_t offTimerRemaining();
uint32_t onTimerRemaining();

// Learned raw buttons (fallback for unknown protocols / special keys).
void startLearning(const String &name);
void cancelLearning();
bool isLearning();
String learningName();
bool pressButton(uint8_t id);
bool deleteButton(uint8_t id);
std::vector<LearnedButton> buttons();

}  // namespace Ac

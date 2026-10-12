#include "ac.h"

#include <FS.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRsend.h>
#include <IRutils.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace {

const uint16_t kCaptureBufferSize = 1024;  // AC frames are long
const uint8_t kCaptureTimeoutMs = 50;      // gap that ends a frame
const uint16_t kMinUnknownSize = 12;       // ignore short noise bursts
const uint32_t kIgnoreAfterSendMs = 400;   // don't decode our own echo
const uint32_t kSaveDelayMs = 3000;        // batch flash writes
const uint8_t kSettingsSchema = 1;
const char *kButtonDir = "/btn";

IRac *irac = nullptr;
IRsend *irsend = nullptr;
IRrecv *irrecv = nullptr;
decode_results results;
Preferences prefs;

AcSettings cur;
uint32_t ver = 1;
decode_type_t proto = decode_type_t::UNKNOWN;
int16_t mdl = -1;

stdAc::state_t prevSent;
bool havePrev = false;

Detection det;
String lastDump;

uint32_t offAtMs = 0, onAtMs = 0;
uint32_t ignoreUntilMs = 0;
uint32_t dirtyAtMs = 0;

bool learning = false;
String learnName;

stdAc::fanspeed_t toFanspeed(uint8_t f) {
  switch (f) {
    case FAN_LOW: return stdAc::fanspeed_t::kLow;
    case FAN_MED: return stdAc::fanspeed_t::kMedium;
    case FAN_HIGH: return stdAc::fanspeed_t::kHigh;
    default: return stdAc::fanspeed_t::kAuto;
  }
}

uint8_t fromFanspeed(stdAc::fanspeed_t f) {
  switch (f) {
    case stdAc::fanspeed_t::kMin:
    case stdAc::fanspeed_t::kLow: return FAN_LOW;
    case stdAc::fanspeed_t::kMedium:
    case stdAc::fanspeed_t::kMediumHigh: return FAN_MED;
    case stdAc::fanspeed_t::kHigh:
    case stdAc::fanspeed_t::kMax: return FAN_HIGH;
    default: return FAN_AUTO;
  }
}

stdAc::state_t toState(const AcSettings &s, decode_type_t p, int16_t m) {
  stdAc::state_t st;
  st.protocol = p;
  st.model = m;
  st.power = s.power;
  st.mode = static_cast<stdAc::opmode_t>(s.mode);
  st.degrees = s.temp;
  st.celsius = true;
  st.fanspeed = toFanspeed(s.fan);
  st.swingv = s.swingV ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  st.swingh = s.swingH ? stdAc::swingh_t::kAuto : stdAc::swingh_t::kOff;
  st.quiet = s.quiet;
  st.turbo = s.turbo;
  st.econo = s.eco;
  st.light = s.display;
  st.sleep = s.sleep ? 0 : -1;
  st.beep = true;
  return st;
}

void fromState(const stdAc::state_t &st, AcSettings *s) {
  s->power = st.power;
  switch (st.mode) {
    case stdAc::opmode_t::kAuto: s->mode = MODE_AUTO; break;
    case stdAc::opmode_t::kDry: s->mode = MODE_DRY; break;
    case stdAc::opmode_t::kFan: s->mode = MODE_FAN; break;
    case stdAc::opmode_t::kOff: s->power = false; break;  // keep last mode
    default: s->mode = MODE_COOL; break;                   // cool (no heat)
  }
  s->temp = constrain(static_cast<int>(st.degrees + 0.5f), kMinTemp, kMaxTemp);
  s->fan = fromFanspeed(st.fanspeed);
  s->swingV = st.swingv != stdAc::swingv_t::kOff;
  s->swingH = st.swingh != stdAc::swingh_t::kOff;
  s->quiet = st.quiet;
  s->turbo = st.turbo;
  s->eco = st.econo;
  s->display = st.light;
  s->sleep = st.sleep >= 0;
}

void markChanged() {
  ver++;
  dirtyAtMs = millis() | 1;  // never 0
}

void saveNow() {
  prefs.putUChar("schema", kSettingsSchema);
  prefs.putBytes("settings", &cur, sizeof(cur));
  dirtyAtMs = 0;
}

void load() {
  if (prefs.getUChar("schema", 0) == kSettingsSchema &&
      prefs.getBytesLength("settings") == sizeof(cur)) {
    prefs.getBytes("settings", &cur, sizeof(cur));
  }
  proto = static_cast<decode_type_t>(prefs.getInt("proto", decode_type_t::UNKNOWN));
  mdl = prefs.getShort("model", -1);
  if (!IRac::isProtocolSupported(proto)) proto = decode_type_t::UNKNOWN;
}

String buttonPath(uint8_t id) { return String(kButtonDir) + "/" + id; }

void saveLearned(const decode_results &r) {
  uint16_t *raw = resultToRawArray(&r);
  uint16_t len = getCorrectedRawLength(&r);
  uint8_t id = 0;
  for (uint8_t i = 1; i < 250; i++) {
    if (!LittleFS.exists(buttonPath(i))) { id = i; break; }
  }
  if (id) {
    File f = LittleFS.open(buttonPath(id), "w");
    String name = learnName.substring(0, 40);
    uint8_t nameLen = name.length();
    f.write(&nameLen, 1);
    f.write(reinterpret_cast<const uint8_t *>(name.c_str()), nameLen);
    f.write(reinterpret_cast<const uint8_t *>(&len), sizeof(len));
    f.write(reinterpret_cast<const uint8_t *>(raw), len * sizeof(uint16_t));
    f.close();
  }
  delete[] raw;
  learning = false;
  learnName = "";
}

bool readButton(uint8_t id, String *name, std::vector<uint16_t> *raw) {
  File f = LittleFS.open(buttonPath(id), "r");
  if (!f) return false;
  uint8_t nameLen = 0;
  f.read(&nameLen, 1);
  char buf[41] = {0};
  f.read(reinterpret_cast<uint8_t *>(buf), min<uint8_t>(nameLen, 40));
  if (name) *name = buf;
  if (raw) {
    uint16_t len = 0;
    f.read(reinterpret_cast<uint8_t *>(&len), sizeof(len));
    raw->resize(len);
    f.read(reinterpret_cast<uint8_t *>(raw->data()), len * sizeof(uint16_t));
  }
  f.close();
  return true;
}

void handleCapture() {
  if (!irrecv->decode(&results)) return;
  if (millis() < ignoreUntilMs) {
    irrecv->resume();
    return;
  }

  lastDump = resultToHumanReadableBasic(&results);
  String acDesc = IRAcUtils::resultAcToString(&results);
  if (acDesc.length()) lastDump += "Mesg Desc.: " + acDesc + "\n";
  lastDump += resultToSourceCode(&results);

  if (learning) {
    saveLearned(results);
    irrecv->resume();
    return;
  }

  decode_type_t type = results.decode_type;
  if (type != decode_type_t::UNKNOWN || results.rawlen >= 100) {
    det.protocol = type;
    det.model = -1;
    det.acSupported = IRac::isProtocolSupported(type);
    det.atMs = millis() | 1;
    det.summary = acDesc.length() ? acDesc
                                  : typeToString(type) + " (" + results.rawlen + " pulses)";

    stdAc::state_t st;
    if (det.acSupported &&
        IRAcUtils::decodeToState(&results, &st, havePrev ? &prevSent : nullptr)) {
      det.model = st.model;
      // The physical remote was used: mirror its state so the app stays in sync.
      if (type == proto) {
        fromState(st, &cur);
        prevSent = st;
        havePrev = true;
        markChanged();
      }
    }
  }
  irrecv->resume();
}

}  // namespace

namespace Ac {

void begin(uint8_t sendPin, uint8_t recvPin) {
  prefs.begin("lloyd-ac", false);
  load();
  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  if (!LittleFS.exists(kButtonDir)) LittleFS.mkdir(kButtonDir);

  irac = new IRac(sendPin);
  irsend = new IRsend(sendPin);
  irsend->begin();
  irrecv = new IRrecv(recvPin, kCaptureBufferSize, kCaptureTimeoutMs, true);
  irrecv->setUnknownThreshold(kMinUnknownSize);
  irrecv->enableIRIn();
}

void loop() {
  uint32_t now = millis();
  if (offAtMs && (int32_t)(now - offAtMs) >= 0) {
    offAtMs = 0;
    AcSettings s = cur;
    s.power = false;
    apply(s);
  }
  if (onAtMs && (int32_t)(now - onAtMs) >= 0) {
    onAtMs = 0;
    AcSettings s = cur;
    s.power = true;
    apply(s);
  }
  handleCapture();
  if (dirtyAtMs && millis() - dirtyAtMs > kSaveDelayMs) saveNow();
}

const AcSettings &settings() { return cur; }
uint32_t version() { return ver; }

bool apply(const AcSettings &next) {
  cur = next;
  cur.temp = constrain(cur.temp, kMinTemp, kMaxTemp);
  markChanged();
  if (proto == decode_type_t::UNKNOWN) return false;

  stdAc::state_t st = toState(cur, proto, mdl);
  bool ok = irac->sendAc(st, havePrev ? &prevSent : nullptr);
  ignoreUntilMs = millis() + kIgnoreAfterSendMs;
  prevSent = st;
  havePrev = true;
  return ok;
}

decode_type_t protocol() { return proto; }
int16_t model() { return mdl; }

bool setProtocol(decode_type_t p, int16_t m) {
  if (p != decode_type_t::UNKNOWN && !IRac::isProtocolSupported(p)) return false;
  proto = p;
  mdl = m;
  havePrev = false;
  prefs.putInt("proto", p);
  prefs.putShort("model", m);
  ver++;
  return true;
}

bool testProtocol(decode_type_t p, int16_t m, bool power) {
  if (!IRac::isProtocolSupported(p)) return false;
  AcSettings s;
  s.power = power;
  s.mode = MODE_COOL;
  s.temp = 24;
  s.fan = FAN_AUTO;
  stdAc::state_t st = toState(s, p, m);
  bool ok = irac->sendAc(st, nullptr);
  ignoreUntilMs = millis() + kIgnoreAfterSendMs;
  return ok;
}

const Detection &lastDetection() { return det; }
String lastCaptureDump() { return lastDump; }

void setOffTimer(uint32_t minutes) {
  offAtMs = minutes ? (millis() + minutes * 60000UL) | 1 : 0;
  ver++;
}

void setOnTimer(uint32_t minutes) {
  onAtMs = minutes ? (millis() + minutes * 60000UL) | 1 : 0;
  ver++;
}

static uint32_t remaining(uint32_t atMs) {
  if (!atMs) return 0;
  int32_t left = (int32_t)(atMs - millis());
  return left > 0 ? (left + 999) / 1000 : 0;
}

uint32_t offTimerRemaining() { return remaining(offAtMs); }
uint32_t onTimerRemaining() { return remaining(onAtMs); }

void startLearning(const String &name) {
  learning = true;
  learnName = name.length() ? name : String("Button");
  ver++;
}

void cancelLearning() {
  learning = false;
  learnName = "";
  ver++;
}

bool isLearning() { return learning; }
String learningName() { return learnName; }

bool pressButton(uint8_t id) {
  std::vector<uint16_t> raw;
  if (!readButton(id, nullptr, &raw) || raw.empty()) return false;
  irsend->sendRaw(raw.data(), raw.size(), 38);
  ignoreUntilMs = millis() + kIgnoreAfterSendMs;
  return true;
}

bool deleteButton(uint8_t id) {
  bool ok = LittleFS.remove(buttonPath(id));
  ver++;
  return ok;
}

std::vector<LearnedButton> buttons() {
  std::vector<LearnedButton> out;
  for (uint8_t i = 1; i < 250; i++) {
    String name;
    if (LittleFS.exists(buttonPath(i)) && readButton(i, &name, nullptr)) {
      out.push_back({i, name});
    }
  }
  return out;
}

}  // namespace Ac

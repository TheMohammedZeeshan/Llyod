// HTTP API + phone web app (served from flash, gzip'd at build time).
#include "web.h"

#include <ESPmDNS.h>
#include <IRac.h>
#include <IRutils.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ac.h"
#include "homekit.h"
#include "ui_gz.h"

namespace {

WebServer server(80);
bool started = false;

// Protocols used by OEM remotes common in Indian-market split ACs. Shown first
// in the "find my protocol" list; everything else IRac supports follows.
const char *kSuggested[] = {
    "COOLIX", "MIDEA", "GREE", "KELON", "KELON168", "TCL112AC", "HAIER_AC",
    "HAIER_AC_YRW02", "HAIER_AC176", "ELECTRA_AC", "VOLTAS", "WHIRLPOOL_AC",
    "CARRIER_AC64", "TECO", "LG2", "LG", "DAIKIN", "MITSUBISHI_AC",
    "PANASONIC_AC", "SAMSUNG_AC", "HITACHI_AC", "TOSHIBA_AC", "SHARP_AC",
    "AIRWELL", "TROTEC", "TRANSCOLD", "BOSCH144", "YORK", "KELVINATOR"};

String esc(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) break;
        out += c;
    }
  }
  return out;
}

const char *b(bool v) { return v ? "true" : "false"; }

String statusJson() {
  const AcSettings &s = Ac::settings();
  String j;
  j.reserve(1024);
  j += "{\"s\":{";
  j += "\"power\":"; j += b(s.power);
  j += ",\"mode\":"; j += s.mode;
  j += ",\"temp\":"; j += s.temp;
  j += ",\"fan\":"; j += s.fan;
  j += ",\"swingV\":"; j += b(s.swingV);
  j += ",\"swingH\":"; j += b(s.swingH);
  j += ",\"turbo\":"; j += b(s.turbo);
  j += ",\"eco\":"; j += b(s.eco);
  j += ",\"sleep\":"; j += b(s.sleep);
  j += ",\"quiet\":"; j += b(s.quiet);
  j += ",\"display\":"; j += b(s.display);
  j += "},\"protocol\":\"";
  j += Ac::protocol() == decode_type_t::UNKNOWN ? String("") : typeToString(Ac::protocol());
  j += "\",\"model\":"; j += Ac::model();

  const Detection &d = Ac::lastDetection();
  j += ",\"detected\":";
  if (d.atMs) {
    j += "{\"protocol\":\""; j += typeToString(d.protocol);
    j += "\",\"model\":"; j += d.model;
    j += ",\"supported\":"; j += b(d.acSupported);
    j += ",\"ago\":"; j += (millis() - d.atMs) / 1000;
    j += ",\"summary\":\""; j += esc(d.summary); j += "\"}";
  } else {
    j += "null";
  }

  j += ",\"timers\":{\"off\":"; j += Ac::offTimerRemaining();
  j += ",\"on\":"; j += Ac::onTimerRemaining(); j += "}";

  j += ",\"learning\":";
  if (Ac::isLearning()) { j += "\""; j += esc(Ac::learningName()); j += "\""; }
  else j += "null";

  j += ",\"buttons\":[";
  bool first = true;
  for (const LearnedButton &btn : Ac::buttons()) {
    if (!first) j += ",";
    first = false;
    j += "{\"id\":"; j += btn.id; j += ",\"name\":\""; j += esc(btn.name); j += "\"}";
  }
  j += "]";

  j += ",\"version\":"; j += Ac::version();
  j += ",\"fw\":\"" FIRMWARE_VERSION "\"";
  j += ",\"rssi\":"; j += WiFi.RSSI();
  j += ",\"uptime\":"; j += millis() / 1000;
  j += "}";
  return j;
}

void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

void sendStatus() { sendJson(200, statusJson()); }

void sendError(int code, const char *msg) {
  sendJson(code, String("{\"error\":\"") + msg + "\"}");
}

bool argBool(const char *name, bool fallback) {
  if (!server.hasArg(name)) return fallback;
  String v = server.arg(name);
  v.toLowerCase();
  if (v == "toggle") return !fallback;
  return v == "1" || v == "true" || v == "on" || v == "yes";
}

int argInt(const char *name, int fallback) {
  return server.hasArg(name) ? server.arg(name).toInt() : fallback;
}

uint8_t parseMode(const String &v, uint8_t fallback) {
  if (v == "auto" || v == "0") return MODE_AUTO;
  if (v == "cool" || v == "1") return MODE_COOL;
  if (v == "dry" || v == "3") return MODE_DRY;
  if (v == "fan" || v == "4") return MODE_FAN;
  return fallback;
}

uint8_t parseFan(const String &v, uint8_t fallback) {
  if (v == "auto" || v == "0") return FAN_AUTO;
  if (v == "low" || v == "1") return FAN_LOW;
  if (v == "med" || v == "medium" || v == "2") return FAN_MED;
  if (v == "high" || v == "3") return FAN_HIGH;
  return fallback;
}

// /api/set?power=1&mode=cool&temp=24&fan=auto&swingV=1 ... any subset.
// temp also accepts "+1" / "-1" for relative changes (handy for Shortcuts).
void handleSet() {
  AcSettings s = Ac::settings();
  s.power = argBool("power", s.power);
  if (server.hasArg("mode")) s.mode = parseMode(server.arg("mode"), s.mode);
  if (server.hasArg("temp")) {
    String t = server.arg("temp");
    int v = t.toInt();
    s.temp = constrain((t.startsWith("+") || t.startsWith("-")) ? s.temp + v : v,
                       kMinTemp, kMaxTemp);
  }
  if (server.hasArg("fan")) s.fan = parseFan(server.arg("fan"), s.fan);
  s.swingV = argBool("swingV", s.swingV);
  s.swingH = argBool("swingH", s.swingH);
  s.turbo = argBool("turbo", s.turbo);
  s.eco = argBool("eco", s.eco);
  s.sleep = argBool("sleep", s.sleep);
  s.quiet = argBool("quiet", s.quiet);
  s.display = argBool("display", s.display);
  if (!Ac::apply(s)) return sendError(409, "No remote protocol configured yet. Open Setup.");
  sendStatus();
}

void handleTimer() {
  uint32_t minutes = argInt("minutes", 0);
  String type = server.arg("type");
  if (type == "off") Ac::setOffTimer(minutes);
  else if (type == "on") Ac::setOnTimer(minutes);
  else return sendError(400, "type must be on or off");
  sendStatus();
}

void handleProtocols() {
  String j = "[";
  bool first = true;
  auto add = [&](decode_type_t p, bool suggested) {
    if (!first) j += ",";
    first = false;
    j += "{\"name\":\""; j += typeToString(p);
    j += "\",\"suggested\":"; j += b(suggested); j += "}";
  };
  for (const char *name : kSuggested) {
    decode_type_t p = strToDecodeType(name);
    if (IRac::isProtocolSupported(p)) add(p, true);
  }
  for (int i = 1; i <= kLastDecodeType; i++) {
    decode_type_t p = static_cast<decode_type_t>(i);
    if (!IRac::isProtocolSupported(p)) continue;
    bool suggested = false;
    for (const char *name : kSuggested) suggested |= strToDecodeType(name) == p;
    if (!suggested) add(p, false);
  }
  j += "]";
  sendJson(200, j);
}

void handleProtocol() {
  String name = server.arg("name");
  decode_type_t p = name.length() ? strToDecodeType(name.c_str()) : decode_type_t::UNKNOWN;
  if (name.length() && p == decode_type_t::UNKNOWN) return sendError(400, "unknown protocol");
  if (!Ac::setProtocol(p, argInt("model", -1))) return sendError(400, "protocol not supported");
  sendStatus();
}

void handleTest() {
  decode_type_t p = strToDecodeType(server.arg("name").c_str());
  if (!Ac::testProtocol(p, argInt("model", -1), argBool("power", true)))
    return sendError(400, "protocol not supported");
  sendJson(200, "{\"ok\":true}");
}

void handleLearn() {
  if (argBool("cancel", false)) Ac::cancelLearning();
  else Ac::startLearning(server.arg("name"));
  sendStatus();
}

void handlePress() {
  if (!Ac::pressButton(argInt("id", 0))) return sendError(404, "no such button");
  sendJson(200, "{\"ok\":true}");
}

void handleDelete() {
  Ac::deleteButton(argInt("id", 0));
  sendStatus();
}

void handleIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html", reinterpret_cast<const char *>(kUiGz), kUiGzLen);
}

const char kUpdatePage[] PROGMEM = R"(<!doctype html><meta name=viewport content="width=device-width">
<body style="font-family:system-ui;padding:24px"><h3>Firmware update</h3>
<form method=POST action=/update enctype=multipart/form-data>
<input type=file name=fw accept=.bin> <button>Upload</button></form>
<p>Use <code>firmware.bin</code> (not the factory image).</p>)";

void handleUpdateDone() {
  bool ok = !Update.hasError();
  server.sendHeader("Connection", "close");
  server.send(200, "text/plain", ok ? "Update OK, rebooting..." : "Update FAILED");
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    Update.begin(UPDATE_SIZE_UNKNOWN);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    Update.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    Update.end(true);
  }
}

}  // namespace

namespace Web {

void begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_ANY, sendStatus);
  server.on("/api/set", HTTP_ANY, handleSet);
  server.on("/api/timer", HTTP_ANY, handleTimer);
  server.on("/api/protocols", HTTP_GET, handleProtocols);
  server.on("/api/protocol", HTTP_ANY, handleProtocol);
  server.on("/api/test", HTTP_ANY, handleTest);
  server.on("/api/learn", HTTP_ANY, handleLearn);
  server.on("/api/button/press", HTTP_ANY, handlePress);
  server.on("/api/button/delete", HTTP_ANY, handleDelete);
  server.on("/api/lastcode", HTTP_GET, []() {
    server.send(200, "text/plain", Ac::lastCaptureDump());
  });
  server.on("/update", HTTP_GET, []() { server.send_P(200, "text/html", kUpdatePage); });
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound([]() { sendError(404, "not found"); });
  server.begin();
  MDNS.addService("http", "tcp", 80);
  started = true;
}

void loop() {
  if (started) server.handleClient();
}

}  // namespace Web

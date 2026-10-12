// Apple Home (HomeKit) bridge: exposes the AC as a HeaterCooler so it shows
// up in the Home app, Control Centre and works with Siri.
#include "homekit.h"

#include <HomeSpan.h>

#include "ac.h"

namespace {

struct LloydCooler : Service::HeaterCooler {
  SpanCharacteristic *active;
  SpanCharacteristic *current;
  SpanCharacteristic *target;
  SpanCharacteristic *roomTemp;
  SpanCharacteristic *coolTemp;
  SpanCharacteristic *swing;
  uint32_t seenVersion = 0;

  LloydCooler() : Service::HeaterCooler() {
    const AcSettings &s = Ac::settings();
    active = new Characteristic::Active(s.power);
    current = new Characteristic::CurrentHeaterCoolerState(s.power ? 3 : 0);
    // No heat pump on this unit: only AUTO (0) and COOL (2).
    target = (new Characteristic::TargetHeaterCoolerState(s.mode == MODE_AUTO ? 0 : 2))
                 ->setValidValues(2, 0, 2);
    // There is no room sensor; mirror the set-point so Home shows something sane.
    roomTemp = new Characteristic::CurrentTemperature(s.temp);
    coolTemp = (new Characteristic::CoolingThresholdTemperature(s.temp))
                   ->setRange(kMinTemp, kMaxTemp, 1);
    swing = new Characteristic::SwingMode(s.swingV);
    seenVersion = Ac::version();
  }

  boolean update() override {
    AcSettings s = Ac::settings();
    if (active->updated()) s.power = active->getNewVal();
    if (target->updated()) s.mode = target->getNewVal() == 0 ? MODE_AUTO : MODE_COOL;
    if (coolTemp->updated()) s.temp = (uint8_t)(coolTemp->getNewVal<float>() + 0.5f);
    if (swing->updated()) s.swingV = swing->getNewVal();
    Ac::apply(s);
    seenVersion = Ac::version();
    current->setVal(s.power ? 3 : 0);
    roomTemp->setVal((float)s.temp);
    return true;
  }

  // Push changes made elsewhere (web app, physical remote, timers) to HomeKit.
  void loop() override {
    if (Ac::version() == seenVersion) return;
    seenVersion = Ac::version();
    const AcSettings &s = Ac::settings();
    if (active->getVal() != (int)s.power) active->setVal(s.power);
    if (current->getVal() != (s.power ? 3 : 0)) current->setVal(s.power ? 3 : 0);
    int t = s.mode == MODE_AUTO ? 0 : 2;
    if (target->getVal() != t) target->setVal(t);
    if ((int)coolTemp->getVal<float>() != s.temp) coolTemp->setVal((float)s.temp);
    if ((int)roomTemp->getVal<float>() != s.temp) roomTemp->setVal((float)s.temp);
    if (swing->getVal() != (int)s.swingV) swing->setVal(s.swingV);
  }
};

}  // namespace

namespace HomeKit {

void begin(void (*onWifi)()) {
  homeSpan.setPortNum(1201);  // keep port 80 free for the web app
  homeSpan.setHostNameSuffix("");
  homeSpan.setApSSID("Lloyd-AC-Setup");
  homeSpan.setApPassword("lloydsetup");
  homeSpan.enableAutoStartAP();
  homeSpan.setSketchVersion(FIRMWARE_VERSION);
  homeSpan.setWifiCallback(onWifi);
  homeSpan.begin(Category::AirConditioners, "Lloyd AC", "lloyd-ac", "GLS18I5FWGEA");

  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Manufacturer("Lloyd (DIY bridge)");
  new Characteristic::Model("GLS18I5FWGEA");
  new Characteristic::FirmwareRevision(FIRMWARE_VERSION);
  new LloydCooler();
}

void loop() { homeSpan.poll(); }

}  // namespace HomeKit

# Lloyd AC Wi-Fi control (DIY, no cloud app)

Control a **Lloyd GLS18I5FWGEA** (1.5 t, 5★ inverter split, 2024) from an iPhone without
the official app. It should also work with most other split ACs.

A ₹500 ESP32 board sits in the room and acts as the AC's remote. It sends IR, the same as
the physical remote, so it doesn't depend on Lloyd/Havells servers or the AC's own Wi-Fi
module. You can control it from:

- **The web app** on your phone at `http://lloyd-ac.local`. Add it to your Home Screen and it opens like a normal app.
- **Apple Home / Siri / Control Centre** (HomeKit), e.g. *"Hey Siri, set the AC to 24"*.
- **iOS Shortcuts / automations** through simple URLs, e.g. "when I leave work, turn the AC on".

What you can control: power, temperature (16–30 °C), mode (Cool/Auto/Dry/Fan), fan speed,
vertical/horizontal swing, Turbo, Eco, Sleep, Silent, Display light, and on/off countdown
timers that run on the bridge. Any other remote button (5-in-1, My Mode, Humidity…) can
be **learned** and added as an extra button.

If someone uses the physical remote, the app picks up the change, provided the bridge's IR
receiver can see the remote.

---

## 1. Parts (~₹500–700)

| Part | Notes |
|---|---|
| ESP32 DevKit (ESP32-WROOM-32, 30 or 38 pin) | Any "ESP32 DevKit V1" |
| 940 nm IR LED (5 mm), ideally 2 | Range is better with two LEDs in series |
| NPN transistor 2N2222 / BC547 / S8050 | Drives the LED harder than a GPIO can |
| Resistors: 1 kΩ (base), 33–47 Ω (LED) | |
| IR receiver **VS1838B** or **TSOP38238** | Used to detect and learn your remote |
| USB cable + 5 V phone charger | Powers it permanently |

A ready-made "IR transmitter module" (KY-005) also works, but its range is short (1–2 m).
Use the transistor circuit if the bridge will sit across the room.

## 2. Wiring

```
ESP32 GPIO4 ──[1kΩ]──► base   (NPN 2N2222 / BC547)
                        emitter ──► GND
5V (VIN) ──[39Ω]──►|IR LED|──►|IR LED|──► collector       (LED long leg = +)

VS1838B receiver (bump facing you, pins left→right):
  OUT ──► GPIO14
  GND ──► GND
  VCC ──► 3V3
```

Point the IR LED(s) at the AC's indoor unit. The display panel on the right is where its
receiver is. A shelf or wall across the room works. The receiver dome needs to be somewhere
you can point the Lloyd remote at.

To use other pins, change `IR_SEND_PIN` / `IR_RECV_PIN` in `firmware/src/main.cpp`.

## 3. Flash the firmware (one-time, from a laptop)

**Easiest: no tools to install**
1. On GitHub open **Actions → Build firmware**, click the latest green run, and download
   **lloyd-ac-firmware** (a zip containing `lloyd-ac-factory.bin`).
2. On a laptop, open **https://web.esphome.io** in Chrome or Edge and plug the ESP32 in by USB.
3. Click **Connect**, pick the serial port, then **Install** and choose `lloyd-ac-factory.bin`.

**Or with PlatformIO:**
```bash
cd firmware
pip install platformio
pio run -t upload
```

Later updates can go over Wi-Fi: open `http://lloyd-ac.local/update` and upload
`lloyd-ac-update.bin`. Don't upload the factory image there.

## 4. Connect it to your Wi-Fi (iPhone)

1. Power the ESP32. It creates a hotspot called **`Lloyd-AC-Setup`** (password `lloydsetup`).
2. Join it from the iPhone and a setup page opens (if it doesn't, open `http://192.168.4.1`).
3. Choose your **2.4 GHz** Wi-Fi network and enter its password. Also enter an 8-digit
   HomeKit setup code you'll remember, e.g. `46637726`.
4. The bridge restarts and joins your Wi-Fi.

## 5. Teach it your remote (one minute)

Open **`http://lloyd-ac.local`** in Safari (iPhone and bridge on the same Wi-Fi), then tap ⚙︎ **Setup**.

- **Detect:** point the Lloyd remote at the bridge's receiver and press **Power**. If the
  code is recognised, tap **Use this protocol**. That's it.
- **If it isn't recognised:** under "Try protocols one by one", tap **On** for each protocol in
  turn, top to bottom, until the AC beeps and starts cooling at 24 °C. Then tap **Use**.
- **Special buttons** (5-in-1, My Mode, Humidity): on the main screen tap **+ Learn**, give
  it a name, then press that button on the remote. To delete a learned button, long-press it.

If no protocol works, open *Setup → Last IR capture* after pressing a remote button and
send the text to me (or open an issue). That is enough to add a dedicated Lloyd decoder.

## 6. Make it feel like an app

- **Home Screen app:** in Safari tap **Share → Add to Home Screen**. It opens full-screen.
- **Apple Home + Siri:** open the **Home** app → **+** → **Add Accessory** → **More options** →
  **Lloyd AC**, then enter the setup code from step 4. The AC appears as an air conditioner tile.
  Siri, Control Centre and Home automations then work. With a HomePod or Apple TV as your
  home hub, this also **works away from home**.

### Shortcuts / automations

Any of these URLs can go in a Shortcuts **Get Contents of URL** action:

| Action | URL |
|---|---|
| On at 24 °C, cool | `http://lloyd-ac.local/api/set?power=1&mode=cool&temp=24` |
| Off | `http://lloyd-ac.local/api/set?power=0` |
| Warmer / cooler by 1 | `http://lloyd-ac.local/api/set?temp=%2B1` / `temp=-1` |
| Fan high + turbo | `http://lloyd-ac.local/api/set?fan=high&turbo=1` |
| Toggle power | `http://lloyd-ac.local/api/set?power=toggle` |
| Turn off in 2 hours | `http://lloyd-ac.local/api/timer?type=off&minutes=120` |
| Current state (JSON) | `http://lloyd-ac.local/api/status` |

Parameters for `/api/set`: `power`, `mode` (`cool|auto|dry|fan`), `temp` (16–30, `+1`, `-1`),
`fan` (`auto|low|med|high`), `swingV`, `swingH`, `turbo`, `eco`, `sleep`, `quiet`, `display`
(`1/0/toggle`).

If `lloyd-ac.local` doesn't resolve on some network, use the bridge's IP address. You can
find it in your router's client list.

---

## Notes and limits

- **IR is one-way.** The bridge remembers what it last sent, so the app shows that rather than
  what the AC is actually doing. If the AC is changed while the bridge can't see it (blocked
  remote, power cut), press any button in the app to bring them back in sync.
- The app has **no password**, so anyone on your home Wi-Fi can use it. Don't port-forward it to
  the internet. Use Apple Home (via a home hub) for remote access.
- Why not use the AC's built-in Wi-Fi? This model's Wi-Fi module talks only to the Havells
  Sync cloud through an undocumented protocol. IR works with any AC and keeps working if that
  cloud changes or goes away.

## Project layout

```
firmware/
  platformio.ini        ESP32 build config (Arduino, IRremoteESP8266, HomeSpan)
  src/ac.*              AC state, IR send/receive, protocol detection, learned buttons, timers
  src/web.*             HTTP API + serves the phone app
  src/homekit.*         Apple Home accessory (HeaterCooler)
  web/index.html        The phone app (embedded into the firmware at build time)
  tools/embed_ui.py     Gzips the web app into a C header during build
.github/workflows/      CI build producing flashable .bin files
```

To preview the web app without hardware, open `firmware/web/index.html` in a browser. It
runs in demo mode.

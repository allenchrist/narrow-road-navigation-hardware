# Narrow Road Navigation: hardware

The car's own hardware for [Narrow Road Navigation](https://github.com/allenchrist/narrow-road-navigation). It replaces the two stand-ins the project uses today:

| Today | In hardware | Folder |
|---|---|---|
| Android app ([VehicleSensorModule](https://github.com/allenchrist/VehicleSensorModule)) | **Main unit**: ESP32-S3, GPS, compass/IMU | `main-unit/` |
| Web dashboard | **Display unit**: 5-inch ESP32-S3 touch screen | `display-unit/` |
| (new) | **Camera unit**: measures the road's width | `camera-unit/` |

It talks to the **same server, unchanged**. To the server, the main unit is just another phone, and the screen is just another dashboard. The app and website keep working alongside it.

```
                         In the car
  ┌──────────────────────────────────────────────────────────┐
  │  Camera unit ── road widths (UDP broadcast) ──┐           │
  │  ESP32-CAM, or webcam / phone + width.py      ▼           │
  │                                         Main unit         │
  │                                     ESP32-S3, GPS, IMU    │
  │  Display unit (5-inch screen)                 │           │
  └──────────────┬────────────────────────────────┼───────────┘
                 │            Wi-Fi: phone hotspot │
                 └──────────────┬─────────────────┘
                                ▼
                 Server (Render): same as today
```

No wires between the units: all three join the same phone hotspot, and each needs only power.

![The car screen at an entrance, told to wait](docs/screens/entry_wait.png)

## How the parts talk

| From → to | How | What |
|---|---|---|
| Main unit → server | `POST /api/auth/login`, `POST /api/auth/device` | Logs in, links this device to the account (as the app does) |
| Main unit → server | WebSocket `/vehicle`, with `Authorization: Bearer` | `register`, then telemetry about once a second and `heading` on turns: the app's exact messages |
| Main unit → server | `POST /api/narrow-road-suggestions` | A narrow road the camera found, for the admin to review |
| Display → server | `POST /api/auth/login`, then Socket.IO | `session:identify`, then `session:assigned` and `vehicles:update`: what the website receives |
| Camera → main unit and display | UDP broadcast, port 4210 | `{"type":"width","width":3.2,"wide":false,"distance":6.0,"confidence":0.8}` |

The main unit and display log in with **the same account**, made in the Android app. Linking the main unit moves that account off any phone that was using it, just as logging in on a new phone does.

## Parts

| Part | Model the code is written for | Used for |
|---|---|---|
| Main controller | ESP32-S3 DevKitC-1 (any ESP32-S3 board works) | Runs the main unit |
| GPS | u-blox NEO-M8N or NEO-M9N (any NMEA GPS works) | Position, speed, direction of travel |
| IMU | BNO085 (or BNO055, chosen in `config.h`) | Heading, acceleration, rotation |
| Display | 5-inch 800×480 ESP32-S3 board, **ESP32-8048S050** (capacitive touch) | The car screen |
| Camera | ESP32-CAM (AI-Thinker), or a USB webcam or phone camera with `width.py` | Road width |
| Power | Automotive 12 V to 5 V 3 A buck converter, fuse | Power from the car socket |

## Wiring

**Main unit (ESP32-S3 DevKitC-1).** Pins are set in `main-unit/include/config.h`.

| Part | Pin on the part | ESP32-S3 pin |
|---|---|---|
| GPS | VCC | 5V (or 3V3 if the module says 3.3 V only) |
| GPS | GND | GND |
| GPS | TX | GPIO 18 |
| GPS | RX | GPIO 17 |
| BNO085 / BNO055 | VIN | 3V3 |
| BNO085 / BNO055 | GND | GND |
| BNO085 / BNO055 | SDA | GPIO 8 |
| BNO085 / BNO055 | SCL | GPIO 9 |

**Display unit.** Nothing to wire: power it through its USB-C port. If yours is a different board, put its pins in `display-unit/include/board.h`; nothing else changes.

**ESP32-CAM.** 5V and GND only. It has no USB port: upload through an ESP32-CAM-MB board (it plugs underneath), or a USB-serial adapter with GPIO 0 joined to GND while it resets.

**Power in the car.** 12 V socket → fuse → buck converter → 5 V to each unit. A car's "12 V" swings from 9 to 16 V with spikes when the engine starts, so use an automotive-rated converter.

## Setting up

You need [VS Code](https://code.visualstudio.com) with the **PlatformIO** extension, and the phone hotspot on.

For **each** of `main-unit`, `display-unit` and `camera-unit/esp32-cam`:

1. Copy `include/secrets.example.h` to `include/secrets.h` and fill in the hotspot name and password (and, for the main unit and display, the account's username and password). `secrets.h` is never uploaded to GitHub.
2. Open the folder in VS Code (File > Open Folder). PlatformIO downloads everything it needs the first time.
3. Connect the board by USB, then **Upload** and **Monitor** in the PlatformIO sidebar.

The first build downloads the ESP32 tools and takes several minutes.

Everything you might change is in each unit's `include/config.h`: the server address, pins, which IMU, timings, and the narrow-road rules. To use a server on your laptop instead of Render, set `SERVER_USE_TLS 0`, `SERVER_HOST` to the laptop's address on the hotspot, and `SERVER_PORT 5000`.

### What you should see

**Main unit**, in the serial monitor every 5 seconds:

```
[Server] Logged in as allen
[Server] Device esp32-a1b2c3d4e5f6 linked to allen
[Server] Registered as VEHICLE_004
[Status] server: connected VEHICLE_004 | GPS: fix, 9 sats | IMU: ok, heading 87 (raw 87) | camera: 3.2 m, 0s ago
```

The car appears on the admin page's Live map, like a phone. GPS needs open sky the first time and can take a minute or two to get a fix.

**Display**: "Waiting for the main unit to connect" until the main unit is sending, then the map and alerts.

**ESP32-CAM**: it prints its address. Open `http://that-address/` in a browser on the hotspot to see the road width live, and the picture with what it measured marked.

### Calibrating the heading

The IMU's 0° must be north. In the main unit's serial monitor, the status line shows `heading 87 (raw 87)`.

1. Park facing north (check with a phone compass app), away from other cars and metal.
2. Note the raw value, say 37. Set `HEADING_OFFSET_DEG` to minus that (`-37.0f`) in `config.h` and upload again.
3. Turn the car right. The heading should go up. If it goes down, set `HEADING_REVERSED 1` and repeat step 2.

Without an IMU, the main unit uses the GPS direction of travel instead, which only works while moving.

## The camera

### How it measures

The camera looks at the road ahead. After calibration, it knows which pixel row shows the ground 5, 6 and 7 m ahead, and how many metres each pixel spans there. On each of those rows it starts at the road straight ahead, scans left and right until the picture stops looking like that road (a wall, kerb or verge), and turns the two edges into metres. The reading is the middle one of the three, smoothed over the last five pictures.

If a scan reaches the edge of the picture without finding a side, the road is wider than the camera sees, and it says "wide": certainly not narrow.

The main unit places each reading on the map a few metres ahead of the car. When readings say 4 m or less along at least 25 m (70% of the readings), the same rules the server's `findNarrowRoads.js` uses, it sends that stretch as a suggestion. The admin approves or rejects it as usual. A road it has already sent isn't sent again.

The screen also shows the latest reading under **Road ahead (camera)**.

### Choosing a camera

| Camera | How | Notes |
|---|---|---|
| **ESP32-CAM, measure mode** (default) | Measures on the ESP32-CAM itself | Cheapest, nothing else needed. Simple method, so bright sun and deep shadow can confuse it |
| **ESP32-CAM, stream mode** | Sends video; `width.py` on a laptop measures | Set `CAMERA_MODE_STREAM` in its `config.h`. Use `--source http://CAMERA:81/stream` |
| **USB webcam** | `width.py --source 0` on a laptop or Raspberry Pi | Easy to try at home first |
| **Phone camera** | `width.py` with the phone's video stream | Install **IP Webcam** (Android): `http://PHONE:8080/video`, or **DroidCam** (Android/iPhone): `http://PHONE:4747/video`. The app shows the address |

Whichever you use, it broadcasts the same message, so the main unit and screen don't care which.

`width.py` needs Python 3.10 or newer: `pip install -r camera-unit/python/requirements.txt`. The laptop or Pi must be on the same hotspot.

### Mounting and calibrating

Mount the camera firmly, centred, looking straight ahead and slightly down, so the road 4 to 8 m ahead is in the lower half of the picture. If it moves, calibrate again.

1. On flat ground, put four markers (stones, tape or chalk crosses) at the corners of a rectangle centred on the car's line: **2 m wide, from 4 m to 8 m ahead** of the camera, measured along the ground from directly below it.
2. Run, from `camera-unit/python`:
   ```
   python calibrate.py --source http://CAMERA-ADDRESS/frame.bmp     # ESP32-CAM
   python calibrate.py --source 0                                    # USB webcam
   python calibrate.py --source http://PHONE:8080/video              # phone
   ```
3. Click the markers in order: near-left, near-right, far-right, far-left. Press Enter.
4. It saves `calibration.json` (used by `width.py`) and prints a line like `#define CAM_IMAGE_TO_GROUND {...}`. For the ESP32-CAM, paste it over the one in `camera-unit/esp32-cam/include/config.h` and upload again.

Check it: stand in a lane, measure its width with a tape, and compare with the reading. Measuring 10 to 15 real lanes like this gives you an accuracy figure for the project report.

`calibrate.py --help` also shows how to calibrate without a window, or roughly from the camera's height and tilt without markers.

## Testing without the hardware

| What | How | Needs |
|---|---|---|
| Your server accepts the hardware's messages | `python tools/protocol_check.py --server https://YOUR-SERVER --username you --password ...` | `pip install websocket-client`. Moves that account to a test device |
| Narrow-road finding (main unit) | `g++ -std=c++17 -Imain-unit/src main-unit/host-tests/test_narrow_detector.cpp main-unit/src/narrow_detector.cpp -o t && ./t` | g++ |
| The screen reads the server's messages, worded like the website | See the top of `display-unit/host-tests/test_display_logic.cpp` | g++, ArduinoJson (PlatformIO downloads it) |
| Preview the screen on your laptop | `display-unit/host-tests/preview/preview.sh` | g++, after one PlatformIO build of the display |
| Camera width on synthetic lanes | `python camera-unit/python/tests/test_width.py` | numpy, opencv-python |
| ESP32-CAM's C++ gives the same answers as Python | See the top of `camera-unit/esp32-cam/host-tests/test_width_core.cpp` | g++, Python |
| Try `width.py` on a photo | `python camera-unit/python/width.py --source lane.jpg` | A photo of a lane, after calibrating |

GitHub runs the firmware builds and these tests on every push (`.github/workflows/checks.yml`).

## What was checked, and what wasn't

Checked before this was handed over:

- All three firmwares **compile** for their boards (Arduino core 3.0.7), both IMU choices and both camera modes.
- `tools/protocol_check.py` passed **15 of 15** against the real server code with a temporary database and `REQUIRE_VEHICLE_TOKEN` on: login, device link, register, telemetry, headings, the Socket.IO session, the road outline reaching the screen, a suggestion, and both refusal messages the firmware looks for.
- The screen's reading of `vehicles:update` was tested on **real messages recorded from the server** while its own simulators drove through 14 of the 15 alerts. Its wording matched what the website's `NarrowRoadAlert` component shows for the same messages, word for word.
- The camera measured synthetic lanes of 2.5 to 6 m to within 9 cm, and the ESP32-CAM's C++ gives the same answer as Python on every test picture.
- The screen layouts in `docs/screens/` were drawn by the real screen code from those recorded messages.

Not checked, because it needs the physical parts:

- **The display's pins** come from the graphics library's own example for the 4.3-inch ESP32-8048S043, which uses the same pins as the 5-inch board. If the screen stays dark or shows noise, compare `board.h` with the seller's demo code. If taps land in the wrong corner, change `TOUCH_ROTATION`.
- **The BNO085's heading direction** depends on how the board is mounted, which is why calibrating the heading is a setup step.
- **Real roads**: lighting, shadows and parked vehicles will make the camera less accurate than on synthetic pictures. Test in real lanes and report the accuracy you measure.

## Limits

- **The phone hotspot must stay on** while driving. Some phones turn the hotspot off when nothing is using it; turn that setting off.
- **Same limits as the app**: GPS error, only vehicles using the system count, alerts are advice only. See the main project's README.
- **Camera**: one camera can't see through parked vehicles or round bends, and a lane edged by open ground rather than walls is harder to measure.
- **The screen's map** draws roads and vehicles on a plain background. There are no street tiles.
- **No sound**: danger alerts flash their border instead.

## Folders

```
main-unit/                 ESP32-S3 firmware (PlatformIO)
  include/config.h         server, pins, IMU choice, narrow-road rules
  src/server_link.cpp      login, device link, /vehicle WebSocket, suggestions
  src/narrow_detector.cpp  camera widths + GPS → narrow-road suggestions
  host-tests/              tests that run on a laptop
display-unit/              5-inch screen firmware (PlatformIO, LVGL)
  include/board.h          the board's pins
  src/server_session.cpp   login and Socket.IO, as the website does
  src/alert_text.cpp       each alert's words, ported from the website
  src/ui.cpp               the screen: map, alert card, side panel
  lib/WebSockets/          arduinoWebSockets with its message limit raised
  host-tests/              tests on recorded server messages; screen preview
camera-unit/
  esp32-cam/               ESP32-CAM firmware (PlatformIO)
  python/                  width.py, calibrate.py for webcams and phones
tools/protocol_check.py    checks a server accepts the hardware's messages
docs/screens/              the screen, drawn from recorded server messages
```

**Libraries**: ArduinoJson, arduinoWebSockets (LGPL 2.1, local copy in `display-unit/lib`), TinyGPSPlus, Adafruit BNO08x and BNO055, LVGL, GFX Library for Arduino, TAMC_GT911, OpenCV.

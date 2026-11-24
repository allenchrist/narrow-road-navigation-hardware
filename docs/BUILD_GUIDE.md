# Building the car unit: step by step

From the bag of parts to your car on the live map. Every step is in order, for someone who has never wired anything before.

**Reading this in VS Code:** press `Ctrl+Shift+V` to see it formatted. Tick boxes as you go by changing `[ ]` to `[x]` in the file.

If a step doesn't turn out as described, stop there and send the serial monitor text to Claude.

---

## Stage 0: what you have, and what each part does

| Part | Gets code? | What it does |
|---|---|---|
| **ESP32 DevKit** | **Yes**, the only one | A tiny computer with Wi-Fi built in. It reads the GPS and compass and sends everything to the Render server |
| **NEO-6M GPS** | No | Listens to satellites and works out where it is. It sends its position to the ESP32 by itself, once a second |
| **GY-271 compass** | No | Measures the earth's magnetic field, which tells which way the car faces. The ESP32 sets it up each time it starts |
| **Breadboard** | No | A board full of holes that joins wires together without soldering |
| **Jumper wires** | No | Short wires with pins on the ends. **Male** end = a pin that goes into a hole. **Female** end = a socket that slides over a pin |
| **4 × Li-ion cells** | No | **Leave these in the box for now.** They need a charger board first. See Stage 16 |

**You also need:**
- Your **laptop**.
- A **USB data cable** that fits the ESP32: micro-USB on most boards, USB-C on some. Many phone cables only charge and won't work, so try another if one fails.
- Your **phone**, with mobile data, for the hotspot.
- Your Narrow Road **account** username and password.
- Optional: a **USB power bank**, to take the unit outdoors without the laptop.

---

## Stage 1: words you'll see

| Word | Meaning |
|---|---|
| **Pin** | One of the little metal legs on a board. Each has a name printed next to it |
| **VCC / 3V3** | Power in. `3V3` = 3.3 volts. `VCC` on a module is where its power goes in |
| **VIN** | The ESP32's 5-volt pin, straight from USB. Not used in this build |
| **GND** | Ground: the "minus" side of power. Every part must share the same ground, or nothing works |
| **TX / RX** | Transmit (talk) and receive (listen). One part's TX always goes to the other part's RX |
| **SDA / SCL** | The two compass wires (called I2C). SDA = data, SCL = clock. SDA goes to SDA, SCL to SCL |
| **GPIO 16** | A numbered ESP32 pin. It's printed on the board as `D16` or `RX2` |
| **Firmware** | The program that runs on the ESP32: the code in the `main-unit` folder |
| **Upload** | Copying the firmware from the laptop into the ESP32 over USB |
| **Serial monitor** | A window in VS Code where you read what the ESP32 prints, and type commands to it |
| **Fix** | The GPS has heard enough satellites to know where it is |

---

## Stage 2: prepare the parts

- [ ] **Check the pin strips are soldered on.** Look at the GPS and the compass.
  - If each has a row of metal pins fixed to it, go on.
  - If the pins came loose in a little bag, they must be **soldered** first. Wires just pushed through the holes touch on and off.
  - Any electronics or mobile-repair shop can do it in a few minutes. Ask for the pins on the **underside**, so the board can sit in the breadboard.
- [ ] **Fit the GPS antenna.** The square beige ceramic block on a thin cable is the antenna.
  - Press its tiny round plug straight down onto the small gold socket on the GPS board until it clicks. Press, don't twist.
  - The beige side faces the sky when in use.
- [ ] **Clear a table.** Use a wood, plastic or cardboard surface, not metal, which can short the pins. Keep the USB cable **unplugged** until Stage 6 says to plug it in.

---

## Stage 3: how a breadboard works

Inside, metal strips join some holes together:

```
   + rail  ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ●   ← all joined along the length
   − rail  ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ●   ← all joined along the length

   row:    1 2 3 4 5 6 7 8 ...
      a   [●]● ● ● ● ● ● ●
      b   [●]● ● ● ● ● ● ●       a, b, c, d, e of row 1 are joined  [ ]
      c   [●]● ● ● ● ● ● ●       (each numbered row is its own group)
      d   [●]● ● ● ● ● ● ●
      e   [●]● ● ● ● ● ● ●
          ─ ─ middle gap ─ ─ ─   (nothing crosses the gap)
      f    ● ● ● ● ● ● ● ●
      g    ● ● ● ● ● ● ● ●       f, g, h, i, j of row 1 are another group
      h    ● ● ● ● ● ● ● ●
      i    ● ● ● ● ● ● ● ●
      j    ● ● ● ● ● ● ● ●
   − rail  ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ●
   + rail  ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ● ●
```

- Two wires in the **same group** are connected.
- Boards like the ESP32 sit **across the middle gap**, so their two rows of pins don't touch.
- Some long breadboards have their rails **split halfway**. If the red or blue line along the edge has a break, keep everything on one half.
- A module's pin strip goes **along** the board, each pin in a **different numbered row**. If two pins share a row, they're shorted together.

---

## Stage 4: find the pins

Every pin has its name printed beside it. If you can't see the names on top, turn the board over. Always go by the **printed name**, not by counting positions.

### ESP32 DevKit (30 pins)

Hold it with the **USB port at the bottom** and the metal can (with the zig-zag antenna) at the top. You'll use **only the right-hand side**: the side whose bottom pin is `3V3`.

```
              ┌───── antenna ─────┐
              │   ┌───────────┐   │
              │   │   ESP32   │   │
              │   │ WROOM-32  │   │
              │   └───────────┘   │
         EN ● │                   │ ● D23
         VP ● │                   │ ● D22   ← compass SCL  (white)
         VN ● │                   │ ● TX0     leave empty
        D34 ● │                   │ ● RX0     leave empty
        D35 ● │                   │ ● D21   ← compass SDA  (blue)
        D32 ● │                   │ ● D19
        D33 ● │                   │ ● D18
        D25 ● │                   │ ● D5
        D26 ● │                   │ ● TX2   ← GPS RX       (green)   = GPIO 17
        D27 ● │                   │ ● RX2   ← GPS TX       (yellow)  = GPIO 16
        D14 ● │                   │ ● D4
        D12 ● │                   │ ● D2
        D13 ● │                   │ ● D15
        GND ● │                   │ ● GND   ← to − rail    (black)
        VIN ● │                   │ ● 3V3   ← to + rail    (red)
              │      [ USB ]      │
              └───────────────────┘
```

- **38-pin board, or a different order?** Look for these printed names instead: `D22`, `D21`, `RX2` or `D16`, `TX2` or `D17`, `GND` and `3V3`. Some boards print `G22` or `IO22` for `D22`; they mean the same pin.
- **Read the label on the metal can.**
  - **ESP32-WROOM-32:** carry on.
  - **ESP32-WROVER:** GPIO 16 and 17 are used inside the module, so the GPS won't work there. Stop and ask Claude to move the GPS to other pins.

### NEO-6M GPS: 4 pins

`VCC` power in · `RX` listens · `TX` talks · `GND` ground

### GY-271 compass: 5 pins

`VCC` power in · `GND` ground · `SCL` clock · `SDA` data · `DRDY` not used

---

## Stage 5: connect the wires (about 20 minutes)

> **USB unplugged the whole time.** Never add or move a wire while the ESP32 has power.

### The picture

```
     ESP32 (right side)        + rail  − rail          modules
                                 │       │
                                 │       │      ┌──────────────────┐
                                 ├───────┼──red─┤ VCC              │
                                 │       ├─blk──┤ GND    GY-271    │
       D22 ●───────white─────────┼───────┼──────┤ SCL    compass   │
       D21 ●───────blue──────────┼───────┼──────┤ SDA       ──► X  │
                                 │       │      │ DRDY (not used)  │
                                 │       │      └──────────────────┘
                                 │       │         (at least 10 cm apart)
                                 │       │      ┌──────────────────┐
                                 ├───────┼──red─┤ VCC              │
       TX2 ●───────green─────────┼───────┼──────┤ RX     NEO-6M    │──antenna
       RX2 ●───────yellow────────┼───────┼──────┤ TX     GPS       │
                                 │       ├─blk──┤ GND              │
                                 │       │      └──────────────────┘
       GND ●───────black─────────┼───────┤
       3V3 ●───────red───────────┤       │
```

A wire drawn across a rail just passes over it. It only connects where it touches the rail line with `┤` or `├`.

### Where to place each board

- [ ] **ESP32 across the middle gap, at one end of the breadboard.**
  - Turn it so the side with `3V3` and `D22` is the side that has a **free row of holes** next to its pins. The ESP32 is wide, so the other side has no free holes, which is fine because you don't use that side.
  - Line the pins up with the holes, then press down evenly on both ends until the black plastic strips touch the breadboard. Don't press on the metal can.
- [ ] **GPS a little further along the breadboard,** its 4 pins each in a different numbered row. The antenna block lies flat, beige side up.
- [ ] **Compass at the far end,** at least **10 cm** (a hand's width) from the GPS and its antenna.
  - Its 5 pins go in 5 different numbered rows.
  - It must lie **flat**.
  - The **X arrow** printed on it will point to the front of the car later.

### The 10 wires

"+ rail" and "− rail" mean any hole along those long strips on the **same side as the ESP32's 3V3 pin**. To connect to a pin, put the wire in a hole of the **same numbered row, same side of the gap**, as that pin.

| # | From | To | Colour | Why |
|---|---|---|---|---|
| 1 | ESP32 **3V3** | **+ rail** | red | Puts 3.3 V power on the + rail |
| 2 | ESP32 **GND** (right side) | **− rail** | black | Puts ground on the − rail |
| 3 | GPS **VCC** | **+ rail** | red | Powers the GPS |
| 4 | GPS **GND** | **− rail** | black | GPS ground |
| 5 | Compass **VCC** | **+ rail** | red | Powers the compass |
| 6 | Compass **GND** | **− rail** | black | Compass ground |
| 7 | GPS **TX** | ESP32 **RX2** (D16) | yellow | GPS talks → ESP32 listens |
| 8 | GPS **RX** | ESP32 **TX2** (D17) | green | ESP32 talks → GPS listens |
| 9 | Compass **SDA** | ESP32 **D21** | blue | Compass data |
| 10 | Compass **SCL** | ESP32 **D22** | white | Compass clock |

The colours are only suggestions. If your wires come in other colours, keep **red for power and black for ground**, and write down which colour went where.

**Why the GPS is on 3V3, not 5 V:** the NEO-6M board accepts 3.3 V to 5 V, and its chip runs on 3.3 V anyway. Using 3V3 keeps every wire on the one side of the ESP32 that has free holes. **Never** put 5 V on the compass.

### Check before power

- [ ] No wire joins the + rail to the − rail, and 3V3 doesn't share a row with GND.
- [ ] TX goes to RX and RX goes to TX: wires 7 and 8 cross over.
- [ ] Tug each wire gently. None comes out, and each is in the right row.
- [ ] No battery is connected anywhere. Power comes from USB only.

---

## Stage 6: install the software (30–40 minutes, once)

- [ ] **VS Code:** you already have it (you're reading this in it).
- [ ] **Install PlatformIO.**
  1. In VS Code, click the **Extensions** icon on the left (four small squares), or press `Ctrl+Shift+X`.
  2. Search for **PlatformIO IDE** and click **Install**. It takes a few minutes.
  3. When asked, click **Reload** or restart VS Code. An **ant-head icon** appears on the left: that's PlatformIO.
- [ ] **Plug in the ESP32.**
  - Connect the USB cable from the laptop to the ESP32. A small **red LED** on the ESP32 should light.
  - If you smell something hot, or a part gets warm within a few seconds, **unplug at once** and recheck Stage 5.
- [ ] **Check that Windows sees the board.**
  1. Press `Windows+X` and click **Device Manager**.
  2. Open **Ports (COM & LPT)**. You should see a line like `Silicon Labs CP210x USB to UART Bridge (COM5)` or `USB-SERIAL CH340 (COM6)`. The COM number can be anything.
  3. If there's no "Ports" section, or you see an "Unknown device" with a yellow triangle:
     - Look at the small chip next to the ESP32's USB socket. If it reads **CP2102**, search "CP210x driver Silicon Labs" and install it. If it reads **CH340**, search "CH340 driver" and install it.
     - If nothing appears at all, not even an unknown device, the **cable** is charge-only. Try another.

---

## Stage 7: open the project and fill in your settings

- [ ] **Open the main-unit folder.**
  - In VS Code: **File → Open Folder…**, choose `D:\allen\narrow-road-hardware\narrow-road-hardware\main-unit`, and click **Select Folder**.
  - If it asks "Do you trust the authors?", click **Yes**.
  - The first time, PlatformIO downloads the ESP32 tools: 5 to 10 minutes, shown at the bottom right. Wait until it finishes.
- [ ] **Fill in `include/secrets.h`.** In the file list on the left, open **include**, then **secrets.h**. Change only the text **between the quotes**:

  ```c
  #define WIFI_SSID "your-hotspot-name"          // your phone's hotspot name
  #define WIFI_PASSWORD "your-hotspot-password"  // its password
  #define ACCOUNT_USERNAME "your-username"       // your Narrow Road account
  #define ACCOUNT_PASSWORD "your-password"       // its password
  ```

  - Capitals, spaces and symbols must match exactly. `Allen Phone` and `allen phone` are different names.
  - Keep both quote marks, with no extra spaces inside them.
  - To find the hotspot name on the phone: **Settings → Hotspot & tethering → Wi-Fi hotspot**. The names vary by phone.
  - Save with `Ctrl+S`.
  - This file never goes to GitHub. Don't paste it into chats or screenshots.
- [ ] **Check `include/config.h`.** You shouldn't need to change anything. It should already have:

  ```c
  #define SERVER_HOST "narrow-road-navigation-ckml.onrender.com"
  #define IMU_COMPASS          // no // in front of this line
  // #define IMU_BNO085        // these two keep their //
  // #define IMU_BNO055
  ```

  Under "Ordinary ESP32 DevKit": `GPS_RX_PIN 16`, `GPS_TX_PIN 17`, `IMU_SDA_PIN 21`, `IMU_SCL_PIN 22`.
- [ ] **Set up the phone hotspot.**
  - Turn the hotspot **on**.
  - If there's an **AP band** setting, choose **2.4 GHz**. The ESP32 can't see 5 GHz.
  - Turn **off** "Turn off hotspot automatically" or the timeout.
  - Make sure **mobile data** is on.

---

## Stage 8: load the code onto the ESP32 (2–5 minutes)

- [ ] **Open the PlatformIO panel.** Click the **ant-head icon** on the left. Under **Project Tasks**, open **esp32dev** (the ordinary ESP32), then **General**.
- [ ] **Click Upload.** A terminal opens at the bottom. It builds the program first, 1 to 2 minutes the first time. Then:

  ```
  Connecting....
  Writing at 0x00010000... (3 %)
  ...
  Writing at 0x0012c000... (100 %)
  Hard resetting via RTS pin...
  ========== [SUCCESS] ==========
  ```

  **SUCCESS** means the code is in the ESP32. It stays there even when unplugged.
  - **Stuck at `Connecting.....___.....`?** Press and hold the **BOOT** button on the ESP32 (next to the USB socket) until "Writing" appears, then let go.
  - **`could not open port` or `Access is denied`?** Something else is using the port. Close the serial monitor (the bin icon on its terminal), then Upload again.
- [ ] **Click Monitor** (same list, under **esp32dev → General**). The terminal now shows what the ESP32 prints. Press the **EN** button on the ESP32 (the other small button) to restart it and see its messages from the beginning.

**When do you need to upload again?** Only when you change a file: `secrets.h`, `config.h` or the code. Every other time, just give the ESP32 power and it runs the program by itself.

---

## Stage 9: read the messages

Indoors, a good start looks like this:

```
Narrow Road Navigation: main unit
Device ID: esp32-a1b2c3d4e5f6
[IMU] Compass QMC5883L ready
[IMU] QMC5883L | north offset 0.0, reversed no | not calibrated: type cal
Type help for the heading calibration commands
[Server] logging in
[Server] Logged in as allenchrist
[Server] Device esp32-a1b2c3d4e5f6 linked to allenchrist
[Server] Registered as VEHICLE_009
[Status] server: connected VEHICLE_009 | GPS: no fix, 0 sats, 4821 chars | IMU: QMC5883L ok, heading 213 (raw 213)
```

A `[Status]` line comes every 5 seconds:

| You see | Meaning | If it's wrong |
|---|---|---|
| `Compass QMC5883L ready` | Compass wiring works (wires 5, 6, 9, 10) | If it says `Compass not found`, read the `Devices answering on I2C` line under it. `none` means a power or ground wire is loose, or SDA and SCL are swapped |
| `chars` going up | The ESP32 hears the GPS (wire 7). No fix indoors is normal | If it stays near 0 and you see `No data from the GPS`: swap wires 7 and 8 (USB unplugged first) |
| `server: connected` | Wi-Fi, internet and your account all work | See the next table |
| `heading 213` | The compass is giving readings. They're wrong until you calibrate in Stage 11 | |

| Server part says | What to check |
|---|---|
| `waiting for Wi-Fi` | Hotspot on? Set to 2.4 GHz? Name and password in `secrets.h` exactly right? Did you Upload again after changing it? |
| `logging in` | Normal for up to a minute: Render may be waking up |
| `retrying soon` | Read the line above it. `Not authorized` means the account username or password is wrong. Otherwise it's the phone's mobile data |
| `connected VEHICLE_…` | All good |

**Typing in the monitor:** click inside the terminal, type a command such as `help`, and press `Enter`. The letters show as you type.

---

## Stage 10: first GPS fix outdoors (5–10 minutes)

- [ ] **Take it outside, under open sky.** Carry the laptop with the ESP32 still plugged in, keeping the breadboard flat. A terrace or open ground is best. GPS doesn't work indoors, and barely by a window. Antenna **beige side up**.
- [ ] **Wait 2 to 5 minutes the first time.** The status line changes from `GPS: no fix, 0 sats` to `GPS: fix, 6 sats`. A small LED on the GPS board starts **blinking once a second** when it has a fix. Later starts take about 30 seconds.
- [ ] **Look for 5 or more satellites.** 4 is the minimum, and 7 to 10 is a good outdoor result.

---

## Stage 11: calibrate the compass (5 minutes)

Magnets and steel near the compass bend its readings, including the GPS, the USB cable and the car. Calibrating measures that bend and removes it. It's saved in the ESP32, so you only redo it if you move the compass.

- [ ] **Stand away from metal.** Not at a steel table, not next to a car. Keep phones and other laptops away from the breadboard.
- [ ] **Type `cal` and press Enter.** Over the next **30 seconds**, slowly turn the whole breadboard round **2 or 3 full circles**, keeping it flat like a plate on a table, about 10 seconds per circle.

  ```
  [Cal] 24 s left | X range 41 uT, Y range 38 uT (each should reach about 60 to 90)
  [Cal] Done and saved. Now point the car north and type north
  ```

  If it says `Not saved: the sensor wasn't turned through full circles`, type `cal` again and turn more slowly, all the way round.
- [ ] **Point the X arrow north, then type `north`.**
  1. Open a compass app on your phone, and put the phone a little away from the board.
  2. Turn the breadboard until the **X arrow** on the GY-271 points north.
  3. Type `north` and press Enter. The status line should now say `heading 0` (or 359, which is the same).
- [ ] **Turn it a quarter circle to the right (clockwise).** The X arrow now points east, and the heading should read about **90**. If it reads about **270**, type `reverse`, then do the `north` step again.
- [ ] **Check all four directions:** north ≈ 0, east ≈ 90, south ≈ 180, west ≈ 270. Within 10 to 15 degrees is good.

| Command | What it does |
|---|---|
| `cal` | 30-second calibration while you turn the board round |
| `north` | "The X arrow points north right now" |
| `reverse` | Flips the direction, if turning right makes the heading go down |
| `reset` | Forgets all of it, to start again |
| `watch` | Shows the heading 5 times a second as you turn it; press Enter to stop |
| `help` | Lists these commands |

In the car, faster than about 7 km/h, the unit uses the GPS's direction of travel instead. The compass covers when you're stopped or slow. Once the board is fixed in the car, run `cal` and `north` again there.

---

## Stage 12: see your car on the website

- [ ] **Open the website** on the laptop or phone: `narrow-road-navigation-ckml.onrender.com`. Log in with the **same account** as in `secrets.h`.
- [ ] **Find the car.** On the dashboard (or the admin page's **Live map**), a car appears at your GPS position. Turn the breadboard and its arrow turns too.
- [ ] **Optional: test an alert.** Walk the unit into one of your narrow roads on the map (such as "PARKING"). The dashboard shows the narrow-road alert.

Your phone app gets logged out. That's expected: one account drives one car device, so the ESP32 has taken over and the phone only provides the hotspot.

---

## Stage 13: add the phone camera (do this last)

Only after Stages 9 to 12 work.

- [ ] **Install the camera program's parts (once).**
  1. In VS Code, **File → Open Folder** and choose the top `narrow-road-hardware` folder.
  2. Open **Terminal → New Terminal** and type:

     ```
     pip install -r camera-unit/python/requirements.txt
     ```
- [ ] **Install IP Webcam on the phone** (Play Store). Open it, scroll to the bottom and tap **Start server**. It shows an address like `http://192.168.43.1:8080`. Write it down.
- [ ] **Join the laptop to the phone hotspot.** If IP Webcam's address doesn't work: in a terminal, type `ipconfig` and find the Wi-Fi adapter's **Default Gateway**. That number is the phone; use `http://THAT-NUMBER:8080`.
- [ ] **Mount the phone firmly** in the car: centred, looking straight ahead and slightly down, so the road 4 to 8 m ahead is in the lower half of the picture. A windscreen phone holder works. If it moves, calibrate again.
- [ ] **Calibrate the camera once.**
  1. On flat ground, put four markers (stones or chalk crosses) at the corners of a rectangle **2 m wide**, from **4 m to 8 m ahead** of the camera, centred on the car. Measure along the ground from directly below the phone.
  2. Run:

     ```
     cd camera-unit/python
     python calibrate.py --source http://192.168.43.1:8080/video
     ```

  3. In the window, click the markers in this order: **near-left, near-right, far-right, far-left**, then press `Enter`. It saves `calibration.json`.
- [ ] **Run it while driving:**

  ```
  python width.py --source http://192.168.43.1:8080/video
  ```

  If Windows asks whether Python may use the network, click **Allow**. The ESP32's status line then ends with `camera: 3.2 m, 0s ago`. Press `q` in the picture window to stop.

---

## Stage 14: every time after the first setup

No uploading, no calibrating:

1. **Phone:** hotspot and mobile data on. For the camera, also **IP Webcam → Start server**.
2. **ESP32:** give it USB power (laptop, power bank, or USB car charger). It connects in 10 to 60 seconds.
3. **Laptop:** join the hotspot. For the camera, run `python width.py --source http://…:8080/video`.
4. **Browser:** open the dashboard and log in.

---

## Stage 15: when something's wrong

| Problem | Most likely cause | Fix |
|---|---|---|
| No red LED on the ESP32 | Cable or USB port | Try another cable and port |
| LED goes off when you plug in a wire | Short circuit: 3V3 touching GND | Unplug USB at once and recheck wires 1–6 |
| No COM port in Device Manager | Charge-only cable, or missing driver | Stage 6 |
| Upload stuck at "Connecting…" | Board not in upload mode | Hold **BOOT** during "Connecting" |
| Monitor shows strange symbols | Wrong speed | Close and reopen Monitor from PlatformIO (it uses 115200) |
| `Compass not found`, scan says `none` | Compass power, ground, SDA or SCL loose | Check wires 5, 6, 9, 10. Try swapping 9 and 10 |
| Scan finds an address, but not 0x0D, 0x2C or 0x1E | A different compass chip | Send the address to Claude |
| `No data from the GPS` | TX and RX swapped, or no power | Swap wires 7 and 8. Check the GPS LED lights |
| GPS chars go up but never a fix | Can't see the sky | Fully outdoors, antenna up, wait 5 minutes |
| Heading jumps around | Metal nearby, or not calibrated | Move away from metal and run `cal` again |
| `waiting for Wi-Fi` forever | Hotspot name or password, or 5 GHz | Stage 7. Upload again after changing `secrets.h` |
| `Not authorized` | Account username or password | Fix `secrets.h`, then Upload |
| ESP32 keeps restarting | Weak USB power or bad cable | Use a laptop USB port directly. Try another cable |

For anything else: select the serial monitor text, copy it with `Ctrl+C`, and send it to Claude. Replace any password with `****` first.

---

## Stage 16: safety

**Leave the Li-ion cells in the box for now.** Without a charger and protection board they can overheat or catch fire if shorted or wired wrongly.

- **Never** connect a cell straight to 3V3 (a full cell is 4.2 V; that pin takes at most 3.6 V), or to VIN (3.7 V is too little; the board restarts at random).
- **Never** join the four cells in series (up to 16.8 V). It can burn out the ESP32.
- Never let a cell's two ends touch metal at the same time (keys, coins, wires).

**Later, the safe one-cell setup:** one cell in a proper 18650 holder → a **TP4056 charger board with protection** (pins B+ B− OUT+ OUT−) → an **MT3608 boost board** set to exactly **5.0 V** with a multimeter before connecting → the ESP32's **VIN** and **GND**. Unplug the battery before plugging in USB. Details are in the README's "Power" section.

- Always unplug USB before changing wires.
- Keep the boards off metal surfaces.
- In the car, use a USB car charger. Never wire into the car's 12 V directly.
